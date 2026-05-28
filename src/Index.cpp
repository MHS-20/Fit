#include "Index.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

uint32_t read_be32(const std::vector<uint8_t> &data, size_t &idx) {
  uint32_t val = (data[idx] << 24) | (data[idx + 1] << 16) |
                 (data[idx + 2] << 8) | data[idx + 3];
  idx += 4;
  return val;
}

uint16_t read_be16(const std::vector<uint8_t> &data, size_t &idx) {
  uint16_t val = (data[idx] << 8) | data[idx + 1];
  idx += 2;
  return val;
}

void write_be32(std::ofstream &f, uint32_t val) {
  uint8_t bytes[4] = {
      static_cast<uint8_t>(val >> 24), static_cast<uint8_t>(val >> 16),
      static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val)};
  f.write(reinterpret_cast<char *>(bytes), 4);
}

void write_be16(std::ofstream &f, uint16_t val) {
  uint8_t bytes[2] = {static_cast<uint8_t>(val >> 8),
                      static_cast<uint8_t>(val)};
  f.write(reinterpret_cast<char *>(bytes), 2);
}

FitIndex index_read(const FitRepository &repo) {
  fs::path p = repo_file(repo, "index");
  if (!fs::exists(p))
    return FitIndex();
  std::ifstream f(p, std::ios::binary);
  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
  if (raw.size() < 12)
    return FitIndex();
  size_t idx = 0;
  if (raw[0] != 'D' || raw[1] != 'I' || raw[2] != 'R' || raw[3] != 'C')
    throw std::runtime_error("Bad signature");
  idx += 4;
  uint32_t version = read_be32(raw, idx);
  uint32_t count = read_be32(raw, idx);
  FitIndex index;
  index.version = version;
  for (uint32_t i = 0; i < count; ++i) {
    FitIndexEntry e;
    e.ctime.first = read_be32(raw, idx);
    e.ctime.second = read_be32(raw, idx);
    e.mtime.first = read_be32(raw, idx);
    e.mtime.second = read_be32(raw, idx);
    e.dev = read_be32(raw, idx);
    e.ino = read_be32(raw, idx);
    idx += 2;
    uint16_t mode = read_be16(raw, idx);
    e.mode_type = mode >> 12;
    e.mode_perms = mode & 0x1FF;
    e.uid = read_be32(raw, idx);
    e.gid = read_be32(raw, idx);
    e.fsize = read_be32(raw, idx);
    std::stringstream ss;
    for (size_t j = 0; j < 20; ++j)
      ss << std::hex << std::setw(2) << std::setfill('0') << (int)raw[idx++];
    e.sha = ss.str();
    uint16_t flags = read_be16(raw, idx);
    e.flag_assume_valid = flags & 0x8000;
    e.flag_stage = flags & 0x4000;
    uint16_t name_len = flags & 0x0FFF;
    std::string name(raw.begin() + idx, raw.begin() + idx + name_len);
    e.name = name;
    idx += name_len + 1;
    while (idx % 8 != 0)
      idx++;
    index.entries.push_back(e);
  }
  return index;
}

void index_write(const FitRepository &repo, const FitIndex &index) {
  std::ofstream f(repo_file(repo, "index", true), std::ios::binary);
  f.write("DIRC", 4);
  write_be32(f, index.version);
  write_be32(f, index.entries.size());
  size_t file_pos =
      12; // absolute file offset: 4 (DIRC) + 4 (version) + 4 (count)
  for (auto &e : index.entries) {
    write_be32(f, e.ctime.first);
    write_be32(f, e.ctime.second);
    write_be32(f, e.mtime.first);
    write_be32(f, e.mtime.second);
    write_be32(f, e.dev);
    write_be32(f, e.ino);
    write_be16(f, 0);
    uint16_t mode = (e.mode_type << 12) | e.mode_perms;
    write_be16(f, mode);
    write_be32(f, e.uid);
    write_be32(f, e.gid);
    write_be32(f, e.fsize);
    for (size_t i = 0; i < e.sha.size(); i += 2) {
      f.put(static_cast<char>(std::stoul(e.sha.substr(i, 2), nullptr, 16)));
    }
    uint16_t flags = (e.flag_assume_valid ? 0x8000 : 0) |
                     (e.flag_stage ? 0x4000 : 0) | (e.name.size() & 0x0FFF);
    write_be16(f, flags);
    f.write(e.name.c_str(), e.name.size());
    f.put('\0');
    file_pos += 62 + e.name.size() + 1;
    while (file_pos % 8 != 0) {
      f.put('\0');
      file_pos++;
    }
  }
}
