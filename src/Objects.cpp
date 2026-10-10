#include "Objects.hpp"
#include "Refs.hpp"
#include "Utils.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <openssl/sha.h>
#include <stdexcept>
#include <zlib.h>

std::vector<uint8_t> zlib_compress(const std::vector<uint8_t> &data) {
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  deflateInit(&zs, Z_BEST_COMPRESSION);
  zs.next_in = const_cast<Bytef *>(data.data());
  zs.avail_in = data.size();
  int ret;
  std::vector<uint8_t> out;
  uint8_t outbuffer[32768];
  do {
    zs.next_out = outbuffer;
    zs.avail_out = sizeof(outbuffer);
    ret = deflate(&zs, Z_FINISH);
    out.insert(out.end(), outbuffer,
               outbuffer + (sizeof(outbuffer) - zs.avail_out));
  } while (ret == Z_OK);
  deflateEnd(&zs);
  return out;
}

std::vector<uint8_t> zlib_decompress(const std::vector<uint8_t> &data) {
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  inflateInit(&zs);
  zs.next_in = const_cast<Bytef *>(data.data());
  zs.avail_in = data.size();
  int ret;
  std::vector<uint8_t> out;
  uint8_t outbuffer[32768];
  do {
    zs.next_out = outbuffer;
    zs.avail_out = sizeof(outbuffer);
    ret = inflate(&zs, Z_SYNC_FLUSH);
    out.insert(out.end(), outbuffer,
               outbuffer + (sizeof(outbuffer) - zs.avail_out));
  } while (ret == Z_OK);
  inflateEnd(&zs);
  if (ret != Z_STREAM_END)
    throw std::runtime_error("Corrupt zlib stream");
  return out;
}

std::string sha1_hex(const std::vector<uint8_t> &data) {
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(data.data(), data.size(), hash);
  return bytes_to_hex(hash, SHA_DIGEST_LENGTH);
}

void FitCommit::deserialize(const std::vector<uint8_t> &data) {
  std::string s(data.begin(), data.end());
  size_t pos = 0;
  while (pos < s.size()) {
    size_t spc = s.find(' ', pos);
    size_t nl = s.find('\n', pos);
    if (spc == std::string::npos || nl < spc) {
      if (s[pos] != '\n')
        throw std::runtime_error("Malformed commit: header without a value");
      kvlm[""].push_back(s.substr(pos + 1));
      break;
    }
    std::string key = s.substr(pos, spc - pos);
    size_t end = pos;
    while (true) {
      end = s.find('\n', end + 1);
      if (end == std::string::npos || end + 1 >= s.size() || s[end + 1] != ' ')
        break;
    }
    if (end == std::string::npos)
      throw std::runtime_error(
          "Malformed commit: no blank line before message");
    std::string val = s.substr(spc + 1, end - spc - 1);
    size_t found;
    while ((found = val.find("\n ")) != std::string::npos)
      val.replace(found, 2, "\n");
    kvlm[key].push_back(val);
    pos = end + 1;
  }
}

std::vector<uint8_t> FitCommit::serialize() {
  // Git's own field order for commits and tags (they never share keys). The
  // map's alphabetical order would produce different bytes, hence a
  // different hash than real git for the same content.
  static const std::vector<std::string> git_order = {
      "object", "type",   "tag",    "tagger",
      "tree",   "parent", "author", "committer"};
  std::vector<std::string> keys = git_order;
  for (auto &pair : kvlm)
    if (!pair.first.empty() && std::find(git_order.begin(), git_order.end(),
                                         pair.first) == git_order.end())
      keys.push_back(pair.first);

  std::string ret;
  for (auto &key : keys) {
    auto it = kvlm.find(key);
    if (it == kvlm.end())
      continue;
    for (auto &v : it->second) {
      std::string val = v;
      size_t found = 0;
      while ((found = val.find('\n', found)) != std::string::npos) {
        val.replace(found, 1, "\n ");
        found += 2;
      }
      ret += key + " " + val + "\n";
    }
  }
  auto msg = kvlm.find("");
  ret +=
      "\n" + (msg == kvlm.end() || msg->second.empty() ? "" : msg->second[0]);
  return std::vector<uint8_t>(ret.begin(), ret.end());
}

void FitTree::deserialize(const std::vector<uint8_t> &data) {
  auto pos = data.begin();
  while (pos != data.end()) {
    auto spc = std::find(pos, data.end(), ' ');
    auto nul = std::find(spc, data.end(), '\0');
    if (nul == data.end() || data.end() - nul < 21)
      throw std::runtime_error("Malformed tree entry");
    std::string mode(pos, spc);
    if (mode.size() == 5)
      mode = "0" + mode;
    items.push_back(
        {mode, std::string(spc + 1, nul), bytes_to_hex(&*(nul + 1), 20)});
    pos = nul + 21;
  }
}

std::vector<uint8_t> FitTree::serialize() {
  std::sort(items.begin(), items.end(),
            [](const FitTreeLeaf &a, const FitTreeLeaf &b) {
              std::string ap =
                  a.mode.rfind("04", 0) == 0 ? a.path + "/" : a.path;
              std::string bp =
                  b.mode.rfind("04", 0) == 0 ? b.path + "/" : b.path;
              return ap < bp;
            });
  std::vector<uint8_t> ret;
  for (auto &item : items) {
    std::string m = item.mode;
    if (m.size() == 6 && m[0] == '0')
      m = m.substr(1);
    ret.insert(ret.end(), m.begin(), m.end());
    ret.push_back(' ');
    ret.insert(ret.end(), item.path.begin(), item.path.end());
    ret.push_back('\0');
    for (auto byte : hex_to_bytes(item.sha))
      ret.push_back(byte);
  }
  return ret;
}

std::shared_ptr<FitObject> object_read(const FitRepository &repo,
                                       const std::string &sha) {
  fs::path p = repo.fitdir / "objects" / sha.substr(0, 2) / sha.substr(2);
  if (!fs::exists(p))
    return nullptr;
  std::ifstream f(p, std::ios::binary);
  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
  std::vector<uint8_t> data = zlib_decompress(raw);
  auto spc = std::find(data.begin(), data.end(), ' ');
  auto nul = std::find(spc, data.end(), '\0');
  if (nul == data.end())
    throw std::runtime_error("Malformed object header: " + sha);
  std::string fmt(data.begin(), spc);
  std::string size_str(spc + 1, nul);
  if (size_str.empty() ||
      size_str.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("Malformed object size: " + sha);
  size_t size = std::stoul(size_str);
  std::vector<uint8_t> content(nul + 1, data.end());
  if (content.size() != size)
    throw std::runtime_error("Malformed object");
  std::shared_ptr<FitObject> obj;
  if (fmt == "blob")
    obj = std::make_shared<FitBlob>();
  else if (fmt == "commit")
    obj = std::make_shared<FitCommit>();
  else if (fmt == "tree")
    obj = std::make_shared<FitTree>();
  else if (fmt == "tag")
    obj = std::make_shared<FitTag>();
  else
    throw std::runtime_error("Unknown object type: " + fmt);
  obj->deserialize(content);
  return obj;
}

// "<type> <size>\0<content>": the bytes that are hashed and compressed.
static std::vector<uint8_t> object_encode(FitObject &obj) {
  std::vector<uint8_t> raw = obj.serialize();
  std::string hdr = obj.get_fmt() + " " + std::to_string(raw.size());
  std::vector<uint8_t> full(hdr.begin(), hdr.end());
  full.push_back('\0');
  full.insert(full.end(), raw.begin(), raw.end());
  return full;
}

std::string object_sha(FitObject &obj) { return sha1_hex(object_encode(obj)); }

std::string object_write(FitObject &obj, const FitRepository &repo) {
  std::vector<uint8_t> full = object_encode(obj);
  std::string sha = sha1_hex(full);
  fs::path p = repo.fitdir / "objects" / sha.substr(0, 2) / sha.substr(2);
  if (fs::exists(p)) // content-addressed: same name means same bytes
    return sha;
  fs::create_directories(p.parent_path());
  write_file_atomic(p, zlib_compress(full));
  return sha;
}

std::string object_hash(std::istream &fd, const std::string &fmt,
                        const FitRepository &repo, bool write) {
  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(fd)),
                           std::istreambuf_iterator<char>());
  std::shared_ptr<FitObject> obj;
  if (fmt == "blob")
    obj = std::make_shared<FitBlob>();
  else if (fmt == "commit")
    obj = std::make_shared<FitCommit>();
  else if (fmt == "tree")
    obj = std::make_shared<FitTree>();
  else if (fmt == "tag")
    obj = std::make_shared<FitTag>();
  else
    throw std::runtime_error("Unknown object type: " + fmt);
  obj->deserialize(raw);
  return write ? object_write(*obj, repo) : object_sha(*obj);
}

std::string object_find(const FitRepository &repo, const std::string &name,
                        const std::string &fmt) {
  // A name that resolves to the wrong type is an error rather than "not
  // found". Annotated tags peel to their object, and commits to their tree,
  // until the wanted type is reached.
  auto typed = [&](std::string sha) {
    while (!sha.empty() && !fmt.empty()) {
      auto obj = object_read(repo, sha);
      if (!obj)
        throw std::runtime_error("Object " + sha + " is missing");
      if (obj->get_fmt() == fmt)
        break;
      auto kv = std::dynamic_pointer_cast<FitCommit>(obj); // commits and tags
      const char *next = obj->get_fmt() == "tag" ? "object"
                         : obj->get_fmt() == "commit" && fmt == "tree"
                             ? "tree"
                             : nullptr;
      if (!kv || !next || !kv->kvlm.count(next))
        throw std::runtime_error(name + " is a " + obj->get_fmt() + ", not a " +
                                 fmt);
      sha = kv->kvlm[next][0];
    }
    return sha;
  };

  if (name == "HEAD")
    return typed(ref_resolve(repo, "HEAD"));
  if (name.size() >= 4 && name.size() <= 40 &&
      name.find_first_not_of("0123456789abcdef") == std::string::npos) {
    fs::path p = repo.fitdir / "objects" / name.substr(0, 2);
    std::vector<std::string> matches, exact_type;
    if (fs::exists(p)) {
      std::string rem = name.substr(2);
      for (auto &entry : fs::directory_iterator(p)) {
        std::string fname = entry.path().filename().string();
        if (fname.size() == 38 && fname.rfind(rem, 0) == 0) {
          matches.push_back(name.substr(0, 2) + fname);
          if (!fmt.empty() &&
              object_read(repo, matches.back())->get_fmt() == fmt)
            exact_type.push_back(matches.back());
        }
      }
    }
    // Like git, the wanted type disambiguates a short SHA.
    if (!exact_type.empty())
      matches = exact_type;
    if (matches.size() > 1)
      throw std::runtime_error("Short SHA " + name + " is ambiguous");
    if (matches.size() == 1)
      return typed(matches[0]);
  }
  for (const char *prefix : {"refs/tags/", "refs/heads/"}) {
    std::string sha = ref_resolve(repo, prefix + name);
    if (!sha.empty())
      return typed(sha);
  }
  return "";
}
