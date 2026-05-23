#pragma once
#include "Repository.hpp"
#include <string>
#include <vector>
#include <cstdint>

struct FitIndexEntry {
    std::pair<uint32_t, uint32_t> ctime;
    std::pair<uint32_t, uint32_t> mtime;
    uint32_t dev;
    uint32_t ino;
    uint16_t mode_type;
    uint16_t mode_perms;
    uint32_t uid;
    uint32_t gid;
    uint32_t fsize;
    std::string sha;
    bool flag_assume_valid = false;
    bool flag_stage = false;
    std::string name;
};

struct FitIndex {
    uint32_t version = 2;
    std::vector<FitIndexEntry> entries;
};

FitIndex index_read(const FitRepository& repo);
void index_write(const FitRepository& repo, const FitIndex& index);