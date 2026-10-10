#include "Index.hpp"
#include "Utils.hpp"
#include <algorithm>
#include <fstream>
#include <openssl/sha.h>
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

static void write_be32(std::vector<uint8_t> &out, uint32_t val) {
  out.insert(out.end(),
             {static_cast<uint8_t>(val >> 24), static_cast<uint8_t>(val >> 16),
              static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val)});
}

static void write_be16(std::vector<uint8_t> &out, uint16_t val) {
  out.insert(out.end(),
             {static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val)});
}

static constexpr size_t kHeaderSize = 12;
static constexpr size_t kEntryFixedSize = 62;
static constexpr size_t kChecksumSize = SHA_DIGEST_LENGTH;
static constexpr uint16_t kNameLengthMask = 0x0FFF;

// Each entry is NUL-padded so its own length (not its offset in the file,
// which starts after a 12-byte header) is a multiple of 8, with at least
// one NUL.
static size_t padded_entry_size(size_t name_len) {
  return (kEntryFixedSize + name_len + 8) & ~size_t(7);
}

FitIndex index_read(const FitRepository &repo) {
  fs::path p = repo_file(repo, "index");
  if (!fs::exists(p))
    return FitIndex();

  std::ifstream f(p, std::ios::binary);
  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
  if (raw.size() < kHeaderSize + kChecksumSize)
    throw std::runtime_error("Corrupt index: file too short");

  size_t body_size = raw.size() - kChecksumSize;
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(raw.data(), body_size, hash);
  if (!std::equal(hash, hash + SHA_DIGEST_LENGTH, raw.begin() + body_size))
    throw std::runtime_error("Corrupt index: checksum mismatch");

  size_t idx = 0;
  if (raw[0] != 'D' || raw[1] != 'I' || raw[2] != 'R' || raw[3] != 'C')
    throw std::runtime_error("Bad signature");
  idx += 4;

  FitIndex index;
  index.version = read_be32(raw, idx);
  if (index.version != 2)
    throw std::runtime_error("Unsupported index version " +
                             std::to_string(index.version));
  uint32_t count = read_be32(raw, idx);

  for (uint32_t i = 0; i < count; ++i) {
    size_t entry_start = idx;
    if (body_size - entry_start < kEntryFixedSize)
      throw std::runtime_error("Corrupt index: truncated entry");
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

    // Names of 0xFFF bytes or more store 0xFFF and rely on the NUL instead.
    auto name_begin = raw.begin() + idx;
    auto name_end = std::find(name_begin, raw.begin() + body_size, '\0');
    if (name_end == raw.begin() + body_size)
      throw std::runtime_error("Corrupt index: unterminated name");
    e.name = std::string(name_begin, name_end);
    idx = entry_start + padded_entry_size(e.name.size());
    if (idx > body_size)
      throw std::runtime_error("Corrupt index: truncated entry");

    index.entries.push_back(std::move(e));
  }
  return index;
}

void index_write(const FitRepository &repo, const FitIndex &index) {
  // Git requires entries sorted by name (byte order).
  std::vector<const FitIndexEntry *> sorted;
  for (const auto &e : index.entries)
    sorted.push_back(&e);
  std::sort(sorted.begin(), sorted.end(),
            [](auto *a, auto *b) { return a->name < b->name; });

  std::vector<uint8_t> out = {'D', 'I', 'R', 'C'};
  write_be32(out, index.version);
  write_be32(out, static_cast<uint32_t>(sorted.size()));

  for (const auto *e : sorted) {
    size_t entry_start = out.size();
    write_be32(out, e->ctime.sec);
    write_be32(out, e->ctime.nsec);
    write_be32(out, e->mtime.sec);
    write_be32(out, e->mtime.nsec);
    write_be32(out, e->dev);
    write_be32(out, e->ino);
    write_be16(out, 0); // padding
    write_be16(out,
               static_cast<uint16_t>((e->mode_type << 12) | e->mode_perms));
    write_be32(out, e->uid);
    write_be32(out, e->gid);
    write_be32(out, e->fsize);

    std::vector<uint8_t> sha = hex_to_bytes(e->sha);
    out.insert(out.end(), sha.begin(), sha.end());

    uint16_t flags = static_cast<uint16_t>(
        (e->flag_assume_valid ? 0x8000u : 0u) | (e->flag_stage ? 0x4000u : 0u) |
        std::min<size_t>(e->name.size(), kNameLengthMask));
    write_be16(out, flags);
    out.insert(out.end(), e->name.begin(), e->name.end());
    out.resize(entry_start + padded_entry_size(e->name.size()), '\0');
  }

  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(out.data(), out.size(), hash);
  out.insert(out.end(), hash, hash + SHA_DIGEST_LENGTH);
  write_file_atomic(repo_file(repo, "index", true), out);
}
