#include "../src/Commands.hpp"
#include "../src/Index.hpp"
#include "../src/Objects.hpp"
#include "framework.hpp"
#include "support.hpp"
#include <sys/stat.h>

static const std::string kId = "Fit Tester <fit@example.com> 1700000000 +0000";
using Paths = std::vector<std::string>;

// Writes `content` to the worktree file `name` and stages it the way
// `fit add` will: blob written, index entry carrying the file's real mtime.
static void add_file(FitRepository &repo, const std::string &name,
                     const std::string &content) {
  fs::path p = repo.worktree / name;
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << content;

  FitBlob b;
  b.blobdata = std::vector<uint8_t>(content.begin(), content.end());
  struct stat st;
  stat(p.c_str(), &st);
  FitIndexEntry e;
  e.name = name;
  e.sha = object_write(b, repo);
  e.mode_type = 0b1000;
  e.mode_perms = 0644;
  e.mtime.sec = static_cast<uint32_t>(st.st_mtime);
  e.fsize = static_cast<uint32_t>(content.size());

  FitIndex idx = index_read(repo);
  std::erase_if(idx.entries, [&](auto &x) { return x.name == name; });
  idx.entries.push_back(e);
  index_write(repo, idx);
}

static bool is_clean(const StatusReport &r) {
  return r.staged_new.empty() && r.staged_modified.empty() &&
         r.staged_deleted.empty() && r.unstaged_modified.empty() &&
         r.unstaged_deleted.empty();
}

// Baseline every status test is gated on: after committing what's staged,
// status reports nothing. Only meaningful once something non-clean is
// reported for a dirty repo, so that's checked here too — an always-empty
// stub passes the clean half for free but not the dirty half.
static bool clean_baseline_works() {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  add_file(repo, "a.txt", "a\n");
  if (compute_status(repo).staged_new != Paths{"a.txt"})
    return false;
  cmd_commit(repo, "base", kId);
  return is_clean(compute_status(repo));
}

TEST(status_flatten_tree) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  add_file(repo, "a.txt", "a\n");
  add_file(repo, "dir/sub/b.txt", "b\n");
  std::string sha = cmd_commit(repo, "c", kId);
  auto c = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));
  auto flat = flatten_tree(repo, c->kvlm["tree"][0]);
  REQUIRE_EQ(flat.size(), size_t(2));
  REQUIRE(flat.count("a.txt") == 1);
  REQUIRE(flat.count("dir/sub/b.txt") == 1);
  FitIndex idx = index_read(repo);
  for (auto &e : idx.entries)
    REQUIRE_EQ(flat[e.name], e.sha);
}

TEST(status_new_file_staged) {
  REQUIRE_GATE(clean_baseline_works(),
               "a clean repo must report nothing first");
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  add_file(repo, "a.txt", "a\n");
  cmd_commit(repo, "base", kId);
  add_file(repo, "new.txt", "n\n");
  StatusReport r = compute_status(repo);
  REQUIRE(r.staged_new == Paths{"new.txt"});
  REQUIRE(r.staged_modified.empty() && r.staged_deleted.empty());
  REQUIRE(r.unstaged_modified.empty() && r.unstaged_deleted.empty());
}

TEST(status_modified_after_commit) {
  REQUIRE_GATE(clean_baseline_works(),
               "a clean repo must report nothing first");
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  add_file(repo, "a.txt", "a\n");
  cmd_commit(repo, "base", kId);

  // Edit on disk, unstaged. Push the mtime forward explicitly: the edit can
  // land within the same second as the commit, which mtime alone can't see.
  fs::path p = repo.worktree / "a.txt";
  std::ofstream(p, std::ios::binary) << "edited\n";
  fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(5));
  StatusReport r = compute_status(repo);
  REQUIRE(r.unstaged_modified == Paths{"a.txt"});
  REQUIRE(r.staged_modified.empty());

  // Stage it: now index differs from HEAD, worktree matches the index.
  add_file(repo, "a.txt", "edited again\n");
  r = compute_status(repo);
  REQUIRE(r.staged_modified == Paths{"a.txt"});
  REQUIRE(r.unstaged_modified.empty());
}

TEST(status_deleted_from_worktree) {
  REQUIRE_GATE(clean_baseline_works(),
               "a clean repo must report nothing first");
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  add_file(repo, "a.txt", "a\n");
  add_file(repo, "b.txt", "b\n");
  cmd_commit(repo, "base", kId);

  fs::remove(repo.worktree / "a.txt");
  StatusReport r = compute_status(repo);
  REQUIRE(r.unstaged_deleted == Paths{"a.txt"});
  REQUIRE(r.staged_deleted.empty());

  // Unstage it too (what `fit rm` will do): now it's a staged deletion.
  FitIndex idx = index_read(repo);
  std::erase_if(idx.entries, [](auto &x) { return x.name == "a.txt"; });
  index_write(repo, idx);
  r = compute_status(repo);
  REQUIRE(r.staged_deleted == Paths{"a.txt"});
  REQUIRE(r.unstaged_deleted.empty());
}

TEST(status_touched_but_unchanged_is_clean) {
  REQUIRE_GATE(clean_baseline_works(),
               "a clean repo must report nothing first");
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  add_file(repo, "a.txt", "a\n");
  cmd_commit(repo, "base", kId);
  fs::path p = repo.worktree / "a.txt";
  fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(5));
  REQUIRE(is_clean(compute_status(repo)));
}

TEST(status_same_second_edit_is_detected) {
  REQUIRE_GATE(clean_baseline_works(),
               "a clean repo must report nothing first");
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  add_file(repo, "a.txt", "a\n");
  cmd_commit(repo, "base", kId);
  // Rewrite immediately and pin the mtime's seconds to the staged value:
  // only the size and nanoseconds give the edit away.
  fs::path p = repo.worktree / "a.txt";
  auto staged_mtime = fs::last_write_time(p);
  std::ofstream(p, std::ios::binary) << "a longer line\n";
  fs::last_write_time(p, staged_mtime);
  REQUIRE(compute_status(repo).unstaged_modified == Paths{"a.txt"});
}
