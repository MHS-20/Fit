#include "Index.hpp"
#include "Utils.hpp"
#include <fstream>
#include <stdexcept>

static uint32_t read_be32(const std::vector<uint8_t> &data, size_t &idx) {
  uint32_t val = (static_cast<uint32_t>(data[idx]) << 24) |
                 (static_cast<uint32_t>(data[idx + 1]) << 16) |
                 (static_cast<uint32_t>(data[idx + 2]) << 8) |
                 static_cast<uint32_t>(data[idx + 3]);
  idx += 4;
  return val;
}

static uint16_t read_be16(const std::vector<uint8_t> &data, size_t &idx) {
  uint16_t val = static_cast<uint16_t>((data[idx] << 8) | data[idx + 1]);
  idx += 2;
  return val;
}

static void write_be32(std::ofstream &f, uint32_t val) {
  uint8_t bytes[4] = {
      static_cast<uint8_t>(val >> 24), static_cast<uint8_t>(val >> 16),
      static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val)};
  f.write(reinterpret_cast<char *>(bytes), 4);
}

static void write_be16(std::ofstream &f, uint16_t val) {
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

  FitIndex index;
  index.version = read_be32(raw, idx);
  uint32_t count = read_be32(raw, idx);

  for (uint32_t i = 0; i < count; ++i) {
    FitIndexEntry e;

    e.ctime.sec = read_be32(raw, idx);
    e.ctime.nsec = read_be32(raw, idx);
    e.mtime.sec = read_be32(raw, idx);
    e.mtime.nsec = read_be32(raw, idx);

    e.dev = read_be32(raw, idx);
    e.ino = read_be32(raw, idx);
    idx += 2; // padding
    uint16_t mode = read_be16(raw, idx);
    e.mode_type = mode >> 12;
    e.mode_perms = mode & 0x1FF;
    e.uid = read_be32(raw, idx);
    e.gid = read_be32(raw, idx);
    e.fsize = read_be32(raw, idx);

    e.sha = bytes_to_hex(&raw[idx], 20);
    idx += 20;

    uint16_t flags = read_be16(raw, idx);
    e.flag_assume_valid = flags & 0x8000;
    e.flag_stage = flags & 0x4000;
    uint16_t name_len = flags & 0x0FFF;

    e.name = std::string(raw.begin() + idx, raw.begin() + idx + name_len);
    idx += name_len + 1; // +1 for NUL
    while (idx % 8 != 0)
      idx++;

    index.entries.push_back(std::move(e));
  }
  return index;
}

void index_write(const FitRepository &repo, const FitIndex &index) {
  std::ofstream f(repo_file(repo, "index", true), std::ios::binary);
  f.write("DIRC", 4);
  write_be32(f, index.version);
  write_be32(f, static_cast<uint32_t>(index.entries.size()));

  size_t file_pos = 12; // 4 (DIRC) + 4 (version) + 4 (count)
  for (const auto &e : index.entries) {
    write_be32(f, e.ctime.sec);
    write_be32(f, e.ctime.nsec);
    write_be32(f, e.mtime.sec);
    write_be32(f, e.mtime.nsec);
    write_be32(f, e.dev);
    write_be32(f, e.ino);
    write_be16(f, 0); // padding
    write_be16(f, static_cast<uint16_t>((e.mode_type << 12) | e.mode_perms));
    write_be32(f, e.uid);
    write_be32(f, e.gid);
    write_be32(f, e.fsize);

    for (auto byte : hex_to_bytes(e.sha))
      f.put(static_cast<char>(byte));

    uint16_t flags = static_cast<uint16_t>(
        (e.flag_assume_valid ? 0x8000u : 0u) | (e.flag_stage ? 0x4000u : 0u) |
        (e.name.size() & 0x0FFFu));
    write_be16(f, flags);
    f.write(e.name.c_str(), e.name.size());
    f.put('\0');

    file_pos += 62 + e.name.size() + 1;
    while (file_pos % 8 != 0) {
      f.put('\0');
      ++file_pos;
    }
  }
}
