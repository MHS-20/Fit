// Fit test support.
//
// A temp-directory fixture and a thin wrapper for shelling out to real
// `git`, used by every "matches real git" verification test.
#pragma once
#include "framework.hpp"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

struct TempDir {
  fs::path path;
  TempDir() {
    auto base = fs::temp_directory_path();
    std::random_device rd;
    for (int attempt = 0; attempt < 100; ++attempt) {
      fs::path candidate = base / ("fit-" + std::to_string(rd()));
      if (fs::create_directory(candidate)) {
        path = candidate;
        return;
      }
    }
    throw TestFailure{"could not create a temp directory"};
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;
};

// Runs `cmd` with the working directory set to `dir` (via a `cd` prefix —
// simplest portable option for a scaffold) and returns {exit_code, stdout}.
// A 10s timeout is applied via `timeout` if present on the system.
inline std::pair<int, std::string> run_in_dir(const fs::path &dir,
                                              const std::string &cmd) {
  std::string full = "cd '" + dir.string() + "' && timeout 10 " + cmd + " 2>&1";
  std::array<char, 4096> buffer;
  std::string result;
  FILE *pipe = popen(full.c_str(), "r");
  if (!pipe)
    throw TestFailure{"popen failed for: " + cmd};
  size_t n;
  while ((n = fread(buffer.data(), 1, buffer.size(), pipe)) > 0)
    result.append(buffer.data(), n);
  int status = pclose(pipe);
  int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return {code, result};
}

// Convenience: run a git command in `dir`, require it succeeds, return stdout.
inline std::string git(const fs::path &dir, const std::string &args) {
  auto [code, out] = run_in_dir(dir, "git " + args);
  if (code != 0)
    throw TestFailure{"git " + args + " failed (" + std::to_string(code) +
                      "): " + out};
  return out;
}

// Trims a single trailing newline, if present — most `git` subcommands emit
// exactly one.
inline std::string rstrip_nl(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
    s.pop_back();
  return s;
}

// Symlinks <repo>/.fit to <repo>/.git so real `git` tooling can read a
// Fit repository directly — this is the load-bearing trick behind every
// "matches real git" test in this project.
inline void expose_as_dotgit(const fs::path &worktree) {
  std::error_code ec;
  fs::create_directory_symlink(worktree / ".fit", worktree / ".git", ec);
  if (ec)
    throw TestFailure{"could not symlink .fit -> .git: " + ec.message()};
}

// Changes the working directory for one scope, for code that locates the
// repository from the current directory (the cmd_* entry points).
struct ScopedChdir {
  fs::path previous = fs::current_path();
  explicit ScopedChdir(const fs::path &dir) { fs::current_path(dir); }
  ~ScopedChdir() { fs::current_path(previous); }
  ScopedChdir(const ScopedChdir &) = delete;
  ScopedChdir &operator=(const ScopedChdir &) = delete;
};

// Sets an environment variable for one scope, restoring the old value.
struct ScopedEnv {
  std::string name;
  std::optional<std::string> previous;
  ScopedEnv(std::string n, const std::string &value) : name(std::move(n)) {
    if (const char *v = std::getenv(name.c_str()))
      previous = v;
    setenv(name.c_str(), value.c_str(), 1);
  }
  ~ScopedEnv() {
    if (previous)
      setenv(name.c_str(), previous->c_str(), 1);
    else
      unsetenv(name.c_str());
  }
  ScopedEnv(const ScopedEnv &) = delete;
  ScopedEnv &operator=(const ScopedEnv &) = delete;
};

inline void write_text(const fs::path &p, const std::string &content) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << content;
}

inline std::string read_text(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
