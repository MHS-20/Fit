#include "Repository.hpp"
#include <ini.h>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <iostream>

// Robust utility to strip all leading/trailing whitespace, tabs, and carriage returns
std::string trim_ini_whitespace(const std::string& str) {
    if (str.empty()) return "";
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

FitRepository::FitRepository(const fs::path& path, bool force) {
    worktree = fs::absolute(path);
    fitdir = worktree / ".fit";
    
    if (!force && !fs::is_directory(fitdir)) {
        throw std::runtime_error("Not a Fit repository " + path.string());
    }
    
    fs::path cf = fitdir / "config";
    if (fs::exists(cf)) {
        auto handler = [](void* user, const char* section, const char* name, const char* value) -> int {
            auto* configuration = static_cast<std::map<std::string, std::map<std::string, std::string>>*>(user);
            
            std::string clean_section = trim_ini_whitespace(section ? section : "");
            std::string clean_name = trim_ini_whitespace(name ? name : "");
            std::string clean_value = trim_ini_whitespace(value ? value : "");
            
            if (!clean_section.empty() && !clean_name.empty()) {
                (*configuration)[clean_section][clean_name] = clean_value;
            }
            return 1;
        };
        
        if (ini_parse(cf.string().c_str(), handler, &conf) < 0) {
            if (!force) throw std::runtime_error("Failed to parse configuration file");
        }
    } else if (!force) {
        throw std::runtime_error("Configuration file missing");
    }
    
    if (!force) {
        std::string version = conf["core"]["repositoryformatversion"];
        if (version != "0") {
            throw std::runtime_error("Unsupported repositoryformatversion (Extracted: '" + version + "')");
        }
    }
}

fs::path repo_path(const FitRepository& repo, const fs::path& path) {
    return repo.fitdir / path;
}

fs::path repo_dir(const FitRepository& repo, const fs::path& path, bool mkdir) {
    fs::path full_path = repo.fitdir / path;
    if (fs::exists(full_path)) {
        if (fs::is_directory(full_path)) return full_path;
        throw std::runtime_error("Not a directory " + full_path.string());
    }
    if (mkdir) {
        fs::create_directories(full_path);
        return full_path;
    }
    return "";
}

fs::path repo_file(const FitRepository& repo, const fs::path& path, bool mkdir) {
    if (!repo_dir(repo, path.parent_path(), mkdir).empty()) {
        return repo_path(repo, path);
    }
    return "";
}

FitRepository repo_create(const fs::path& path) {
    FitRepository repo(path, true);
    if (fs::exists(repo.worktree)) {
        if (!fs::is_directory(repo.worktree)) throw std::runtime_error(path.string() + " is not a directory!");
    } else {
        fs::create_directories(repo.worktree);
    }
    
    repo_dir(repo, "branches", true);
    repo_dir(repo, "objects", true);
    repo_dir(repo, "refs/tags", true);
    repo_dir(repo, "refs/heads", true);

    std::ofstream desc(repo_path(repo, "description"));
    desc << "Unnamed repository; edit this file 'description' to name the repository.\n";
    
    std::ofstream head(repo_path(repo, "HEAD"));
    head << "ref: refs/heads/master\n";

    // Standard, completely legal clean INI format generation
    std::ofstream config(repo_path(repo, "config"));
    config << "[core]\n"
           << "repositoryformatversion = 0\n"
           << "filemode = false\n"
           << "bare = false\n";
    
    return repo;
}

FitRepository repo_find(const fs::path& path, bool required) {
    fs::path current = fs::canonical(path);
    while (true) {
        if (fs::is_directory(current / ".fit")) {
            return FitRepository(current);
        }
        if (current.root_path() == current) {
            if (required) throw std::runtime_error("Not a Fit repository");
            return FitRepository();
        }
        current = current.parent_path();
    }
}