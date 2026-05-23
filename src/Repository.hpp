#pragma once
#include <string>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

struct FitRepository {
    fs::path worktree;
    fs::path fitdir;
    std::map<std::string, std::map<std::string, std::string>> conf;

    FitRepository() = default;
    FitRepository(const fs::path& path, bool force = false);
};

fs::path repo_path(const FitRepository& repo, const fs::path& path);
fs::path repo_file(const FitRepository& repo, const fs::path& path, bool mkdir = false);
fs::path repo_dir(const FitRepository& repo, const fs::path& path, bool mkdir = false);
FitRepository repo_create(const fs::path& path);
FitRepository repo_find(const fs::path& path = ".", bool required = true);