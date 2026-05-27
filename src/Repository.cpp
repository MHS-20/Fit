#include "Repository.hpp"
#include "Config.hpp"
#include <fstream>
#include <ini.h>
#include <stdexcept>

FitRepository::FitRepository(const fs::path &path, bool force) {
  worktree = fs::absolute(path);
  fitdir = worktree / ".fit";

  if (!force && !fs::is_directory(fitdir)) {
    throw std::runtime_error("Not a Fit repository " + path.string());
  }

  fs::path cf = fitdir / "config";
  if (fs::exists(cf)) {
    parse_ini_file(cf, conf);
    if (conf.empty() && !force)
      throw std::runtime_error("Failed to parse configuration file");
  }

  if (!force) {
    std::string version = conf["core"]["repositoryformatversion"];
    if (version != "0") {
      throw std::runtime_error(
          "Unsupported repositoryformatversion (Extracted: '" + version + "')");
    }
  }
}

fs::path repo_path(const FitRepository &repo, const fs::path &path) {
  return repo.fitdir / path;
}

fs::path repo_dir(const FitRepository &repo, const fs::path &path, bool mkdir) {
  fs::path full_path = repo.fitdir / path;
  if (fs::exists(full_path)) {
    if (fs::is_directory(full_path))
      return full_path;
    throw std::runtime_error("Not a directory " + full_path.string());
  }
  if (mkdir) {
    fs::create_directories(full_path);
    return full_path;
  }
  return "";
}

fs::path repo_file(const FitRepository &repo, const fs::path &path,
                   bool mkdir) {
  if (!repo_dir(repo, path.parent_path(), mkdir).empty()) {
    return repo_path(repo, path);
  }
  return "";
}

FitRepository repo_create(const fs::path &path) {
  FitRepository repo(path, true);
  if (fs::exists(repo.worktree)) {
    if (!fs::is_directory(repo.worktree))
      throw std::runtime_error(path.string() + " is not a directory!");
  } else {
    fs::create_directories(repo.worktree);
  }

  repo_dir(repo, "branches", true);
  repo_dir(repo, "objects", true);
  repo_dir(repo, "refs/tags", true);
  repo_dir(repo, "refs/heads", true);

  std::ofstream desc(repo_path(repo, "description"));
  desc << "Unnamed repository; edit this file 'description' to name the "
          "repository.\n";

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

FitRepository repo_find(const fs::path &path, bool required) {
  fs::path current = fs::canonical(path);
  while (true) {
    if (fs::is_directory(current / ".fit")) {
      return FitRepository(current);
    }
    if (current.root_path() == current) {
      if (required)
        throw std::runtime_error("Not a Fit repository");
      return FitRepository();
    }
    current = current.parent_path();
  }
}
