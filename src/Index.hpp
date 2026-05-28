#pragma once
#include "Repository.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct FitTimestamp {
  uint32_t sec = 0;
  uint32_t nsec = 0;
};

struct FitIndexEntry {
  FitTimestamp ctime;
  FitTimestamp mtime;
  uint32_t dev = 0;
  uint32_t ino = 0;
  uint16_t mode_type = 0;
  uint16_t mode_perms = 0;
  uint32_t uid = 0;
  uint32_t gid = 0;
  uint32_t fsize = 0;
  std::string sha;
  bool flag_assume_valid = false;
  bool flag_stage = false;
  std::string name;
};

struct FitIndex {
  uint32_t version = 2;
  std::vector<FitIndexEntry> entries;
};

FitIndex index_read(const FitRepository &repo);
void index_write(const FitRepository &repo, const FitIndex &index);
