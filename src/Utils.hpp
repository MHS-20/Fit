#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Convert a byte buffer to a lowercase hex string
inline std::string bytes_to_hex(const uint8_t *data, size_t len) {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < len; ++i)
    ss << std::setw(2) << static_cast<int>(data[i]);
  return ss.str();
}

inline std::string bytes_to_hex(const std::vector<uint8_t> &data) {
  return bytes_to_hex(data.data(), data.size());
}

// Convert a hex string to raw bytes
inline std::vector<uint8_t> hex_to_bytes(const std::string &hex) {
  if (hex.size() % 2 != 0)
    throw std::invalid_argument("hex_to_bytes: odd-length hex string");
  std::vector<uint8_t> out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2)
    out.push_back(
        static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return out;
}

// Writes `data` to a sibling ".lock" file, then renames it over `path`, so a
// reader (or a crash mid-write) never sees a partially written file.
inline void write_file_atomic(const std::filesystem::path &path,
                              std::string_view data) {
  std::filesystem::path tmp = path;
  tmp += ".lock";
  {
    std::ofstream f(tmp, std::ios::binary);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!f)
      throw std::runtime_error("Cannot write " + tmp.string());
  }
  std::filesystem::rename(tmp, path);
}

inline void write_file_atomic(const std::filesystem::path &path,
                              const std::vector<uint8_t> &data) {
  write_file_atomic(
      path, std::string_view(reinterpret_cast<const char *>(data.data()),
                             data.size()));
}
