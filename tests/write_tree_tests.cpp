#include "../src/Commands.hpp"
#include "../src/Objects.hpp"
#include "../src/Repository.hpp"
#include "framework.hpp"
#include "support.hpp"

static std::string write_blob(FitRepository &repo, const std::string &content) {
  FitBlob b;
  b.blobdata = std::vector<uint8_t>(content.begin(), content.end());
  return object_write(b, repo);
}

TEST(write_tree_flat_files) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::vector<FitIndexEntry> entries;
  FitIndexEntry a, b;
  a.name = "a.txt";
  a.sha = write_blob(repo, "aaa\n");
  b.name = "b.txt";
  b.sha = write_blob(repo, "bbb\n");
  entries = {a, b};

  std::string root_sha = write_tree_from_entries(repo, entries, "");
  auto tree = std::dynamic_pointer_cast<FitTree>(object_read(repo, root_sha));
  REQUIRE(tree != nullptr);
  REQUIRE_EQ(tree->items.size(), size_t(2));
}

TEST(write_tree_nested_directories) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::vector<FitIndexEntry> entries;
  FitIndexEntry top, nested, deep;
  top.name = "top.txt";
  top.sha = write_blob(repo, "top\n");
  nested.name = "dir/nested.txt";
  nested.sha = write_blob(repo, "nested\n");
  deep.name = "dir/sub/deep.txt";
  deep.sha = write_blob(repo, "deep\n");
  entries = {top, nested, deep};

  std::string root_sha = write_tree_from_entries(repo, entries, "");
  auto root = std::dynamic_pointer_cast<FitTree>(object_read(repo, root_sha));
  REQUIRE(root != nullptr);
  REQUIRE_EQ(root->items.size(), size_t(2)); // top.txt, dir

  std::string dir_sha;
  for (auto &item : root->items)
    if (item.path == "dir") {
      REQUIRE_EQ(item.mode, std::string("040000"));
      dir_sha = item.sha;
    }
  REQUIRE(!dir_sha.empty());
  auto dir = std::dynamic_pointer_cast<FitTree>(object_read(repo, dir_sha));
  REQUIRE_EQ(dir->items.size(), size_t(2)); // nested.txt, sub
}

TEST(write_tree_matches_real_git_write_tree) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::vector<FitIndexEntry> entries;
  FitIndexEntry a, b;
  a.name = "a.txt";
  a.sha = write_blob(repo, "hello\n");
  b.name = "dir/b.txt";
  b.sha = write_blob(repo, "world\n");
  entries = {a, b};

  std::string root_sha = write_tree_from_entries(repo, entries, "");

  // Mirror the same file layout for real git to write-tree independently.
  fs::create_directories(tmp.path / "dir");
  std::ofstream(tmp.path / "a.txt") << "hello\n";
  std::ofstream(tmp.path / "dir" / "b.txt") << "world\n";
  git(tmp.path, "init -q .");
  git(tmp.path, "add a.txt dir/b.txt");
  std::string git_sha = rstrip_nl(git(tmp.path, "write-tree"));

  REQUIRE_EQ(root_sha, git_sha);
}

TEST(write_tree_records_exec_and_symlink_modes_like_git) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndexEntry exe, link;
  exe.name = "run.sh";
  exe.sha = write_blob(repo, "#!/bin/sh\n");
  exe.mode_type = 0b1000;
  exe.mode_perms = 0755;
  link.name = "link";
  link.sha = write_blob(repo, "run.sh"); // a symlink's blob is its target
  link.mode_type = 0b1010;
  std::string root_sha = write_tree_from_entries(repo, {exe, link}, "");

  write_text(tmp.path / "run.sh", "#!/bin/sh\n");
  fs::permissions(tmp.path / "run.sh", fs::perms::owner_all,
                  fs::perm_options::add);
  fs::create_symlink("run.sh", tmp.path / "link");
  git(tmp.path, "init -q .");
  git(tmp.path, "add run.sh link");
  REQUIRE_EQ(root_sha, rstrip_nl(git(tmp.path, "write-tree")));
}
