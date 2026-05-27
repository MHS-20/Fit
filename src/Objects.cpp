#include "Objects.hpp"
#include "Refs.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <openssl/sha.h>
#include <sstream>
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
  return out;
}

std::string sha1_hex(const std::vector<uint8_t> &data) {
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(data.data(), data.size(), hash);
  std::stringstream ss;
  for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
  return ss.str();
}

void FitCommit::deserialize(const std::vector<uint8_t> &data) {
  std::string s(data.begin(), data.end());
  size_t pos = 0;
  while (pos < s.size()) {
    size_t spc = s.find(' ', pos);
    size_t nl = s.find('\n', pos);
    if (spc == std::string::npos || nl < spc) {
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
    std::string val = s.substr(spc + 1, end - spc - 1);
    size_t found;
    while ((found = val.find("\n ")) != std::string::npos)
      val.replace(found, 2, "\n");
    kvlm[key].push_back(val);
    pos = end + 1;
  }
}

std::vector<uint8_t> FitCommit::serialize() {
  std::string ret;
  for (auto &pair : kvlm) {
    if (pair.first.empty())
      continue;
    for (auto &v : pair.second) {
      std::string val = v;
      size_t found = 0;
      while ((found = val.find('\n', found)) != std::string::npos) {
        val.replace(found, 1, "\n ");
        found += 2;
      }
      ret += pair.first + " " + val + "\n";
    }
  }
  ret += "\n" + (kvlm[""].empty() ? "" : kvlm[""][0]);
  return std::vector<uint8_t>(ret.begin(), ret.end());
}

void FitTree::deserialize(const std::vector<uint8_t> &data) {
  size_t pos = 0;
  while (pos < data.size()) {
    size_t x = 0;
    for (size_t i = pos; i < data.size(); ++i) {
      if (data[i] == ' ') {
        x = i;
        break;
      }
    }
    std::string mode(data.begin() + pos, data.begin() + x);
    if (mode.size() == 5)
      mode = "0" + mode;
    size_t y = 0;
    for (size_t i = x; i < data.size(); ++i) {
      if (data[i] == '\0') {
        y = i;
        break;
      }
    }
    std::string path(data.begin() + x + 1, data.begin() + y);
    std::stringstream ss;
    for (size_t i = y + 1; i < y + 21 && i < data.size(); ++i) {
      ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    items.push_back({mode, path, ss.str()});
    pos = y + 21;
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
    for (size_t i = 0; i < item.sha.size(); i += 2) {
      uint8_t byte = std::stoul(item.sha.substr(i, 2), nullptr, 16);
      ret.push_back(byte);
    }
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
  std::string fmt(data.begin(), spc);
  auto nul = std::find(spc, data.end(), '\0');
  size_t size = std::stoul(std::string(spc + 1, nul));
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

std::string object_write(FitObject &obj, const FitRepository &repo) {
  std::vector<uint8_t> raw = obj.serialize();
  std::string hdr = obj.get_fmt() + " " + std::to_string(raw.size());
  std::vector<uint8_t> full;
  full.insert(full.end(), hdr.begin(), hdr.end());
  full.push_back('\0');
  full.insert(full.end(), raw.begin(), raw.end());
  std::string sha = sha1_hex(full);
  fs::path p = repo.fitdir / "objects" / sha.substr(0, 2) / sha.substr(2);
  if (!fs::exists(p.parent_path()))
    fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  std::vector<uint8_t> compressed = zlib_compress(full);
  f.write(reinterpret_cast<char *>(compressed.data()), compressed.size());
  return sha;
}

std::string object_hash(std::istream &fd, const std::string &fmt,
                        const FitRepository &repo) {
  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(fd)),
                           std::istreambuf_iterator<char>());
  std::shared_ptr<FitObject> obj;
  if (fmt == "blob") {
    auto b = std::make_shared<FitBlob>();
    b->blobdata = raw;
    obj = b;
  } else if (fmt == "commit") {
    auto c = std::make_shared<FitCommit>();
    c->deserialize(raw);
    obj = c;
  } else if (fmt == "tree") {
    auto t = std::make_shared<FitTree>();
    t->deserialize(raw);
    obj = t;
  }
  return object_write(*obj, repo);
}

std::string object_find(const FitRepository &repo, const std::string &name,
                        const std::string &fmt) {
  if (name == "HEAD")
    return ref_resolve(repo, "HEAD");
  if (name.size() >= 4 && name.size() <= 40) {
    fs::path p = repo.fitdir / "objects" / name.substr(0, 2);
    if (fs::exists(p)) {
      std::string rem = name.substr(2);
      for (auto &entry : fs::directory_iterator(p)) {
        std::string fname = entry.path().filename().string();
        if (fname.rfind(rem, 0) == 0) {
          std::string sha = name.substr(0, 2) + fname;
          if (fmt.empty() || object_read(repo, sha)->get_fmt() == fmt)
            return sha;
        }
      }
    }
  }
  std::string tag_sha = ref_resolve(repo, "refs/tags/" + name);
  if (!tag_sha.empty())
    return tag_sha;
  std::string branch_sha = ref_resolve(repo, "refs/heads/" + name);
  if (!branch_sha.empty())
    return branch_sha;
  return "";
}
