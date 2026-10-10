#include "../src/Commands.hpp"
#include "../src/Index.hpp"
#include "../src/Objects.hpp"
#include "framework.hpp"
#include "support.hpp"

static const std::string kId = "Fit Tester <fit@example.com> 1700000000 +0000";
using Files = std::map<std::string, std::string>; // relative path -> bytes

// Stages every file (blob + index entry) and commits; returns the root tree.
static std::string commit_files(FitRepository &repo, const Files &files) {
  FitIndex idx;
  for (auto &[name, content] : files) {
    FitBlob b;
    b.blobdata = std::vector<uint8_t>(content.begin(), content.end());
    FitIndexEntry e;
    e.name = name;
    e.sha = object_write(b, repo);
    e.mode_type = 0b1000;
    e.mode_perms = 0644;
    idx.entries.push_back(e);
  }
  index_write(repo, idx);
  std::string sha = cmd_commit(repo, "c", kId);
  auto c = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));
  return c->kvlm["tree"][0];
}

// Every regular file under `root`, skipping .fit/.git.
static Files read_tree_on_disk(const fs::path &root) {
  Files out;
  for (auto it = fs::recursive_directory_iterator(root);
       it != fs::recursive_directory_iterator(); ++it) {
    std::string name = it->path().filename().string();
    if (it->is_directory() && (name == ".fit" || name == ".git")) {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file())
      continue;
    std::ifstream f(it->path(), std::ios::binary);
    out[fs::relative(it->path(), root).string()] = std::string(
        (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  }
  return out;
}

TEST(checkout_flat_files) {
  TempDir tmp, out;
  FitRepository repo = repo_create(tmp.path);
  // Includes bytes text-mode I/O could mangle: CRLF, a NUL, no final newline.
  Files files = {{"a.txt", "hello\n"},
                 {"crlf.txt", "x\r\ny\r\n"},
                 {"bin.dat", std::string("\0\x01\xff\r\n", 5)}};
  std::string tree = commit_files(repo, files);
  checkout_tree(repo, tree, out.path); // empty existing dir is allowed
  REQUIRE(read_tree_on_disk(out.path) == files);
}

TEST(checkout_nested_directories) {
  TempDir tmp, out;
  FitRepository repo = repo_create(tmp.path);
  Files files = {
      {"top.txt", "t\n"}, {"dir/a.txt", "a\n"}, {"dir/sub/deep/b.txt", "b\n"}};
  std::string tree = commit_files(repo, files);
  checkout_tree(repo, tree, out.path);
  REQUIRE(fs::is_directory(out.path / "dir" / "sub" / "deep"));
  REQUIRE(read_tree_on_disk(out.path) == files);
}

TEST(checkout_creates_missing_dest) {
  TempDir tmp, out;
  FitRepository repo = repo_create(tmp.path);
  Files files = {{"a.txt", "a\n"}, {"dir/b.txt", "b\n"}};
  std::string tree = commit_files(repo, files);
  fs::path dest = out.path / "not" / "yet" / "here";
  checkout_tree(repo, tree, dest);
  REQUIRE(read_tree_on_disk(dest) == files);
}

TEST(checkout_refuses_non_empty_dest) {
  // Gated: a stub that throws on everything passes "it throws" for free.
  {
    TempDir tmp, out;
    FitRepository repo = repo_create(tmp.path);
    Files files = {{"a.txt", "a\n"}};
    bool ok = false;
    try {
      checkout_tree(repo, commit_files(repo, files), out.path);
      ok = read_tree_on_disk(out.path) == files;
    } catch (...) {
    }
    REQUIRE_GATE(ok, "checkout into an empty dir must work first");
  }
  TempDir tmp, out;
  FitRepository repo = repo_create(tmp.path);
  std::string tree = commit_files(repo, {{"a.txt", "a\n"}});
  std::ofstream(out.path / "existing.txt") << "keep me\n";
  bool threw = false;
  try {
    checkout_tree(repo, tree, out.path);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  REQUIRE(threw);
  REQUIRE(!fs::exists(out.path / "a.txt"));
}

TEST(checkout_matches_real_git_checkout) {
  TempDir mine, theirs, out;
  Files files = {{"README.md", "# hi\n"},
                 {"src/main.cpp", "int main() {}\n"},
                 {"src/lib/util.hpp", "#pragma once\n"}};
  FitRepository repo = repo_create(mine.path);
  std::string tree = commit_files(repo, files);

  // Real git: commit the same files, wipe the worktree, check it back out.
  git(theirs.path, "init -q .");
  for (auto &[name, content] : files) {
    fs::create_directories((theirs.path / name).parent_path());
    std::ofstream(theirs.path / name, std::ios::binary) << content;
  }
  git(theirs.path, "add .");
  git(theirs.path,
      "-c user.name=t -c user.email=t@t commit -q --no-gpg-sign -m c");
  REQUIRE_EQ(rstrip_nl(git(theirs.path, "rev-parse HEAD^{tree}")), tree);
  for (auto &[name, content] : files)
    fs::remove(theirs.path / name);
  git(theirs.path, "checkout HEAD -- .");

  checkout_tree(repo, tree, out.path);
  REQUIRE(read_tree_on_disk(out.path) == read_tree_on_disk(theirs.path));
}

TEST(checkout_restores_exec_bit_and_symlinks) {
  TempDir tmp, out;
  FitRepository repo = repo_create(tmp.path);
  auto blob = [&](const std::string &s) {
    FitBlob b;
    b.blobdata = std::vector<uint8_t>(s.begin(), s.end());
    return object_write(b, repo);
  };
  FitIndex idx;
  FitIndexEntry exe, link;
  exe.name = "bin/run.sh";
  exe.sha = blob("#!/bin/sh\n");
  exe.mode_type = 0b1000;
  exe.mode_perms = 0755;
  link.name = "link";
  link.sha = blob("bin/run.sh");
  link.mode_type = 0b1010;
  idx.entries = {exe, link};
  index_write(repo, idx);
  std::string sha = cmd_commit(repo, "c", kId);
  auto c = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));

  fs::path dest = out.path / "co";
  checkout_tree(repo, c->kvlm["tree"][0], dest);
  REQUIRE((fs::status(dest / "bin/run.sh").permissions() &
           fs::perms::owner_exec) != fs::perms::none);
  REQUIRE(fs::is_symlink(dest / "link"));
  REQUIRE_EQ(fs::read_symlink(dest / "link").string(),
             std::string("bin/run.sh"));
}
