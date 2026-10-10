#include "Remote.hpp"
#include "Objects.hpp"
#include "Refs.hpp"
#include <arpa/inet.h>
#include <curl/curl.h>
#include <iomanip>
#include <iostream>
#include <openssl/sha.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

extern std::vector<uint8_t> zlib_compress(const std::vector<uint8_t> &data);

size_t WriteCallbackRaw(void *contents, size_t size, size_t nmemb,
                        void *userp) {
  size_t realsize = size * nmemb;
  std::vector<uint8_t> *mem = static_cast<std::vector<uint8_t> *>(userp);
  mem->insert(mem->end(), static_cast<uint8_t *>(contents),
              static_cast<uint8_t *>(contents) + realsize);
  return realsize;
}

std::vector<uint8_t> _http_raw(const std::string &url,
                               const std::string &username,
                               const std::string &password,
                               const std::vector<uint8_t> &data,
                               const std::string &content_type) {
  CURL *curl = curl_easy_init();
  std::vector<uint8_t> response;
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (!username.empty())
      curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
    if (!password.empty())
      curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackRaw);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    struct curl_slist *headers = NULL;
    if (!content_type.empty()) {
      headers =
          curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
      headers = curl_slist_append(headers, "User-Agent: git/fit");
    }
    if (!data.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS,
                       reinterpret_cast<const char *>(data.data()));
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());
    } else if (!content_type.empty()) {
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    }
    if (headers)
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      curl_easy_cleanup(curl);
      throw std::runtime_error("CURL request failed");
    }
    curl_easy_cleanup(curl);
    if (headers)
      curl_slist_free_all(headers);
  }
  return response;
}

std::vector<std::string> pkt_read_lines(const std::vector<uint8_t> &data) {
  std::vector<std::string> lines;
  size_t i = 0;
  while (i < data.size()) {
    if (i + 4 > data.size())
      break;
    std::string raw_len(data.begin() + i, data.begin() + i + 4);
    int line_len = std::stoul(raw_len, nullptr, 16);
    if (line_len == 0) {
      lines.push_back("");
      i += 4;
    } else {
      lines.push_back(
          std::string(data.begin() + i + 4, data.begin() + i + line_len));
      i += line_len;
    }
  }
  return lines;
}

std::vector<uint8_t> pkt_build_lines(const std::vector<std::string> &lines) {
  std::vector<uint8_t> ret;
  for (auto &line : lines) {
    if (line.empty()) {
      ret.insert(ret.end(), {'0', '0', '0', '0'});
    } else {
      std::stringstream ss;
      ss << std::setw(4) << std::setfill('0') << std::hex << (line.size() + 4);
      std::string hdr = ss.str();
      ret.insert(ret.end(), hdr.begin(), hdr.end());
      ret.insert(ret.end(), line.begin(), line.end());
    }
  }
  return ret;
}

std::string parse_ref_advertisement(const std::vector<std::string> &lines,
                                    std::string &out_branch) {
  for (auto &line : lines) {
    if (line.empty() || line[0] == '#')
      continue;
    size_t spc = line.find(' ');
    if (spc == std::string::npos)
      continue;
    std::string sha = line.substr(0, spc);
    size_t null_or_nl = line.find_first_of("\n\0", spc + 1);
    std::string ref = line.substr(spc + 1, null_or_nl - spc - 1);
    if (ref == "refs/heads/master") {
      out_branch = "master";
      return sha;
    }
    if (ref == "refs/heads/main") {
      out_branch = "main";
      return sha;
    }
  }
  out_branch = "master";
  return "";
}

void collect_objects(const FitRepository &repo, const std::string &sha,
                     std::set<std::string> &objs) {
  if (sha.empty() || objs.count(sha))
    return;
  auto obj = object_read(repo, sha);
  if (!obj)
    return;
  objs.insert(sha);
  if (obj->get_fmt() == "commit") {
    auto c = std::dynamic_pointer_cast<FitCommit>(obj);
    if (c->kvlm.count("tree"))
      collect_objects(repo, c->kvlm["tree"][0], objs);
    if (c->kvlm.count("parent")) {
      for (auto &p : c->kvlm["parent"])
        collect_objects(repo, p, objs);
    }
  } else if (obj->get_fmt() == "tree") {
    auto t = std::dynamic_pointer_cast<FitTree>(obj);
    for (auto &item : t->items)
      collect_objects(repo, item.sha, objs);
  }
}

std::vector<std::string> find_missing_objects(const FitRepository &repo,
                                              const std::string &local,
                                              const std::string &remote) {
  std::set<std::string> local_objs, remote_objs;
  collect_objects(repo, local, local_objs);
  collect_objects(repo, remote, remote_objs);
  std::vector<std::string> missing;
  for (auto &o : local_objs) {
    if (!remote_objs.count(o))
      missing.push_back(o);
  }
  return missing;
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
    PackObjectType t;
    if (obj->get_fmt() == "commit")
      t = PackObjectType::commit;
    else if (obj->get_fmt() == "tree")
      t = PackObjectType::tree;
    else if (obj->get_fmt() == "blob")
      t = PackObjectType::blob;
    else
      t = PackObjectType::tag;
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

std::string get_remote_master_hash(const std::string &git_url,
                                   const std::string &username,
                                   const std::string &password,
                                   std::string &out_branch) {
  std::vector<uint8_t> empty_data;
  auto res = _http_raw(git_url + "/info/refs?service=git-receive-pack",
                       username, password, empty_data, "");
  return parse_ref_advertisement(pkt_read_lines(res), out_branch);
}

void push(const FitRepository &repo, const std::string &git_url,
          const std::string &username, const std::string &password) {
  std::string local_sha = ref_resolve(repo, "refs/heads/master");
  if (local_sha.empty())
    local_sha = ref_resolve(repo, "refs/heads/main");
  if (local_sha.empty())
    throw std::runtime_error("Nothing to push.");
  std::cout << "Fetching remote state from " << git_url << "...\n";
  std::string branch;
  std::string remote_sha =
      get_remote_master_hash(git_url, username, password, branch);
  if (remote_sha == local_sha) {
    std::cout << "Already up to date.\n";
    return;
  }
  std::cout << "Local  : " << local_sha
            << "\nRemote : " << (remote_sha.empty() ? "(empty)" : remote_sha)
            << "\n";
  auto missing = find_missing_objects(repo, local_sha, remote_sha);
  std::cout << "Packing " << missing.size() << " object(s) ...\n";
  std::string old_sha = remote_sha.empty() ? std::string(40, '0') : remote_sha;
  std::string ref_line = old_sha + " " + local_sha + " refs/heads/" + branch +
                         '\0' + " report-status\n";
  std::vector<uint8_t> body = pkt_build_lines({ref_line});
  body.insert(body.end(), {'0', '0', '0', '0'});
  std::vector<uint8_t> pack = create_pack(repo, missing);
  body.insert(body.end(), pack.begin(), pack.end());
  std::cout << "Pushing ...\n";
  auto res = _http_raw(git_url + "/git-receive-pack", username, password, body,
                       "application/x-git-receive-pack-request");
  auto status_lines = pkt_read_lines(res);
  for (auto &l : status_lines)
    std::cout << l << "\n";
}
