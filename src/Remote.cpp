#include "Remote.hpp"
#include "Objects.hpp"
#include "Refs.hpp"
#include <arpa/inet.h>
#include <curl/curl.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <openssl/sha.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

static size_t WriteCallbackRaw(void *contents, size_t size, size_t nmemb,
                               void *userp) {
  size_t realsize = size * nmemb;
  std::vector<uint8_t> *mem = static_cast<std::vector<uint8_t> *>(userp);
  mem->insert(mem->end(), static_cast<uint8_t *>(contents),
              static_cast<uint8_t *>(contents) + realsize);
  return realsize;
}

static std::vector<uint8_t> _http_raw(const std::string &url,
                                      const std::string &username,
                                      const std::string &password,
                                      const std::vector<uint8_t> &data,
                                      const std::string &content_type) {
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
                                                           curl_easy_cleanup);
  if (!curl)
    throw std::runtime_error("Could not initialize libcurl");
  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
      nullptr, curl_slist_free_all);
  auto add_header = [&](const std::string &h) {
    curl_slist *grown = curl_slist_append(headers.get(), h.c_str());
    if (!grown)
      throw std::runtime_error("Could not build HTTP headers");
    headers.release();
    headers.reset(grown);
  };
  std::vector<uint8_t> response;

  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  if (!username.empty())
    curl_easy_setopt(curl.get(), CURLOPT_USERNAME, username.c_str());
  if (!password.empty())
    curl_easy_setopt(curl.get(), CURLOPT_PASSWORD, password.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallbackRaw);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
  add_header("User-Agent: git/fit");
  if (!content_type.empty()) {
    add_header("Content-Type: " + content_type);
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS,
                     reinterpret_cast<const char *>(data.data()));
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(data.size()));
  }
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

  CURLcode res = curl_easy_perform(curl.get());
  if (res != CURLE_OK)
    throw std::runtime_error(std::string("HTTP request failed: ") +
                             curl_easy_strerror(res));
  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  if (status >= 400)
    throw std::runtime_error(
        "HTTP " + std::to_string(status) + " from " + url +
        (status == 401 || status == 403 ? " (check username and token)" : ""));
  return response;
}

std::vector<PktLine> pkt_read_lines(const std::vector<uint8_t> &data) {
  std::vector<PktLine> lines;
  size_t i = 0;
  while (i < data.size()) {
    if (data.size() - i < 4)
      throw std::runtime_error("Truncated pkt-line length");
    std::string raw_len(data.begin() + i, data.begin() + i + 4);
    if (raw_len.find_first_not_of("0123456789abcdefABCDEF") !=
        std::string::npos)
      throw std::runtime_error("Bad pkt-line length '" + raw_len + "'");
    size_t line_len = std::stoul(raw_len, nullptr, 16);
    if (line_len == 0) {
      lines.push_back(std::nullopt);
      i += 4;
      continue;
    }
    if (line_len < 4) // 0001-0003: protocol v2 delimiters, not spoken here
      throw std::runtime_error("Unsupported pkt-line " + raw_len);
    if (line_len > data.size() - i)
      throw std::runtime_error("Truncated pkt-line");
    lines.emplace_back(
        std::string(data.begin() + i + 4, data.begin() + i + line_len));
    i += line_len;
  }
  return lines;
}

static constexpr size_t kMaxPktPayload = 65516; // 65520 minus the length

std::vector<uint8_t> pkt_build_lines(const std::vector<PktLine> &lines) {
  std::vector<uint8_t> ret;
  for (auto &line : lines) {
    if (!line) {
      ret.insert(ret.end(), {'0', '0', '0', '0'});
      continue;
    }
    if (line->size() > kMaxPktPayload)
      throw std::runtime_error("pkt-line too long");
    std::stringstream ss;
    ss << std::setw(4) << std::setfill('0') << std::hex << (line->size() + 4);
    std::string hdr = ss.str();
    ret.insert(ret.end(), hdr.begin(), hdr.end());
    ret.insert(ret.end(), line->begin(), line->end());
  }
  return ret;
}

std::string parse_ref_advertisement(const std::vector<PktLine> &lines,
                                    const std::string &ref) {
  for (auto &line : lines) {
    if (!line || line->empty() || (*line)[0] == '#')
      continue;
    size_t spc = line->find(' ');
    if (spc == std::string::npos)
      continue;
    // The first ref line carries "\0<capabilities>" after the name; the
    // two-character length makes find_first_of look for the NUL too.
    size_t end = line->find_first_of(std::string("\n\0", 2), spc + 1);
    if (line->substr(spc + 1, end - spc - 1) == ref)
      return line->substr(0, spc);
  }
  return "";
}

std::string get_remote_ref_hash(const std::string &git_url,
                                const std::string &username,
                                const std::string &password,
                                const std::string &ref) {
  auto res = _http_raw(git_url + "/info/refs?service=git-receive-pack",
                       username, password, {}, "");
  return parse_ref_advertisement(pkt_read_lines(res), ref);
}

void collect_objects(const FitRepository &repo, const std::string &sha,
                     std::set<std::string> &objs,
                     const std::set<std::string> *known) {
  // Iterative, so a long history can't overflow the call stack.
  std::vector<std::string> todo = {sha};
  while (!todo.empty()) {
    std::string cur = std::move(todo.back());
    todo.pop_back();
    if (cur.empty() || objs.count(cur) || (known && known->count(cur)))
      continue;
    auto obj = object_read(repo, cur);
    if (!obj)
      throw std::runtime_error("Missing object " + cur);
    objs.insert(cur);
    if (auto c = std::dynamic_pointer_cast<FitCommit>(obj)) {
      for (const char *key : {"tree", "parent", "object"})
        if (c->kvlm.count(key))
          for (auto &next : c->kvlm[key])
            todo.push_back(next);
    } else if (auto t = std::dynamic_pointer_cast<FitTree>(obj)) {
      for (auto &item : t->items)
        if (item.mode != "160000") // submodule commit lives in another repo
          todo.push_back(item.sha);
    }
  }
}

std::vector<std::string> find_missing_objects(const FitRepository &repo,
                                              const std::string &local,
                                              const std::string &remote) {
  std::set<std::string> remote_objs, missing;
  collect_objects(repo, remote, remote_objs);
  // Anything the remote has, it has with its whole history, so the local
  // walk stops there.
  collect_objects(repo, local, missing, &remote_objs);
  return {missing.begin(), missing.end()};
}

std::vector<uint8_t> encode_pack_object_header(PackObjectType type,
                                               size_t size) {
  std::vector<uint8_t> ret;
  uint8_t t = static_cast<uint8_t>(type);
  uint8_t byte = (t << 4) | (size & 15);
  size >>= 4;
  while (size > 0) {
    ret.push_back(byte | 0x80);
    byte = size & 0x7F;
    size >>= 7;
  }
  ret.push_back(byte);
  return ret;
}

std::vector<uint8_t> create_pack(const FitRepository &repo,
                                 const std::vector<std::string> &objects) {
  std::vector<uint8_t> pack;
  pack.insert(pack.end(), {'P', 'A', 'C', 'K'});
  uint32_t version = htonl(2);
  uint32_t count = htonl(objects.size());
  pack.insert(pack.end(), reinterpret_cast<uint8_t *>(&version),
              reinterpret_cast<uint8_t *>(&version) + 4);
  pack.insert(pack.end(), reinterpret_cast<uint8_t *>(&count),
              reinterpret_cast<uint8_t *>(&count) + 4);
  for (auto &sha : objects) {
    auto obj = object_read(repo, sha);
    if (!obj)
      throw std::runtime_error("Missing object " + sha);
    PackObjectType t;
    if (obj->get_fmt() == "commit")
      t = PackObjectType::commit;
    else if (obj->get_fmt() == "tree")
      t = PackObjectType::tree;
    else if (obj->get_fmt() == "blob")
      t = PackObjectType::blob;
    else if (obj->get_fmt() == "tag")
      t = PackObjectType::tag;
    else
      throw std::runtime_error("Cannot pack object type " + obj->get_fmt());
    std::vector<uint8_t> raw = obj->serialize();
    std::vector<uint8_t> header = encode_pack_object_header(t, raw.size());
    pack.insert(pack.end(), header.begin(), header.end());
    std::vector<uint8_t> compressed = zlib_compress(raw);
    pack.insert(pack.end(), compressed.begin(), compressed.end());
  }
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(pack.data(), pack.size(), hash);
  pack.insert(pack.end(), hash, hash + SHA_DIGEST_LENGTH);
  return pack;
}

void push(const FitRepository &repo, const std::string &git_url,
          const std::string &username, const std::string &password) {
  std::string ref = ref_head_target(repo);
  if (ref.empty())
    throw std::runtime_error("HEAD is detached; switch to a branch to push.");
  std::string local_sha = ref_resolve(repo, ref);
  if (local_sha.empty())
    throw std::runtime_error("Nothing to push.");
  std::cout << "Fetching remote state from " << git_url << "...\n";
  std::string remote_sha =
      get_remote_ref_hash(git_url, username, password, ref);
  if (remote_sha == local_sha) {
    std::cout << "Already up to date.\n";
    return;
  }
  if (!remote_sha.empty() && !object_read(repo, remote_sha))
    throw std::runtime_error("Rejected: " + ref +
                             " on the remote has commits that are not present "
                             "locally (fetch is not supported).");
  std::cout << "Local  : " << local_sha
            << "\nRemote : " << (remote_sha.empty() ? "(empty)" : remote_sha)
            << "\n";
  auto missing = find_missing_objects(repo, local_sha, remote_sha);
  std::cout << "Packing " << missing.size() << " object(s) ...\n";
  std::string old_sha = remote_sha.empty() ? std::string(40, '0') : remote_sha;
  // Capabilities share the first command line, after a NUL.
  std::string ref_line =
      old_sha + " " + local_sha + " " + ref + '\0' + " report-status\n";
  std::vector<uint8_t> body = pkt_build_lines({ref_line, std::nullopt});
  std::vector<uint8_t> pack = create_pack(repo, missing);
  body.insert(body.end(), pack.begin(), pack.end());
  std::cout << "Pushing ...\n";
  auto res = _http_raw(git_url + "/git-receive-pack", username, password, body,
                       "application/x-git-receive-pack-request");

  // report-status: "unpack ok", then "ok <ref>" or "ng <ref> <reason>".
  bool unpack_ok = false, ref_ok = false;
  std::string failure;
  for (auto &line : pkt_read_lines(res)) {
    if (!line)
      continue;
    std::string l = *line;
    if (!l.empty() && l.back() == '\n')
      l.pop_back();
    std::cout << l << "\n";
    if (l == "unpack ok")
      unpack_ok = true;
    else if (l.rfind("unpack ", 0) == 0 || l.rfind("ng " + ref + " ", 0) == 0)
      failure = l;
    else if (l == "ok " + ref)
      ref_ok = true;
  }
  if (!unpack_ok || !ref_ok)
    throw std::runtime_error("Push rejected: " +
                             (failure.empty() ? "no status report" : failure));
}
