#include "../src/Commands.hpp"
#include "../src/Index.hpp"
#include "../src/Objects.hpp"
#include "../src/Refs.hpp"
#include "framework.hpp"
#include "support.hpp"

static const std::string kId = "Fit Tester <fit@example.com> 1700000000 +0000";

// Writes `content` as a blob and stages it under `name` (regular file 0644).
static void stage(FitRepository &repo, FitIndex &idx, const std::string &name,
                  const std::string &content) {
  FitBlob b;
  b.blobdata = std::vector<uint8_t>(content.begin(), content.end());
  FitIndexEntry e;
  e.name = name;
  e.sha = object_write(b, repo);
  e.mode_type = 0b1000;
  e.mode_perms = 0644;
  e.fsize = static_cast<uint32_t>(content.size());
  idx.entries.push_back(e);
  index_write(repo, idx);
}

static std::shared_ptr<FitCommit> read_commit(FitRepository &repo,
                                              const std::string &sha) {
  auto c = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));
  REQUIRE(c != nullptr);
  return c;
}

TEST(commit_first_commit_no_parent) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  stage(repo, idx, "a.txt", "hello\n");
  std::string sha = cmd_commit(repo, "first", kId);

  auto c = read_commit(repo, sha);
  REQUIRE(c->kvlm.find("parent") == c->kvlm.end());
  REQUIRE_EQ(c->kvlm["author"][0], kId);
  REQUIRE_EQ(c->kvlm[""][0], std::string("first\n"));
  // HEAD stays symbolic; the branch it names now exists and points here.
  REQUIRE_EQ(ref_resolve(repo, "refs/heads/master"), sha);
  REQUIRE_EQ(ref_resolve(repo, "HEAD"), sha);
}

TEST(commit_second_commit_has_parent) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  stage(repo, idx, "a.txt", "hello\n");
  std::string first = cmd_commit(repo, "first", kId);
  stage(repo, idx, "dir/b.txt", "world\n");
  std::string second = cmd_commit(repo, "second", kId);

  REQUIRE(first != second);
  auto c = read_commit(repo, second);
  REQUIRE_EQ(c->kvlm["parent"].size(), size_t(1));
  REQUIRE_EQ(c->kvlm["parent"][0], first);
  REQUIRE_EQ(ref_resolve(repo, "HEAD"), second);
}

TEST(commit_matches_real_git_end_to_end) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  stage(repo, idx, "a.txt", "hello\n");
  std::string first = cmd_commit(repo, "first", kId);
  stage(repo, idx, "dir/b.txt", "world\n");
  std::string second = cmd_commit(repo, "second", kId);

  expose_as_dotgit(tmp.path);
  REQUIRE_EQ(git(tmp.path, "log --format=%H"), second + "\n" + first + "\n");
  // fsck validates every object reachable from the refs (commit headers,
  // tree encoding and sort order, referenced blobs) and the index itself.
  auto [code, out] = run_in_dir(tmp.path, "git fsck --strict --no-dangling");
  if (code != 0)
    throw TestFailure{"git fsck failed: " + out};
}

TEST(commit_refuses_nothing_to_commit) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  // The successful commit in between keeps an always-throwing cmd_commit
  // from passing this test.
  REQUIRE_THROWS(cmd_commit(repo, "empty", kId)); // nothing staged yet
  stage(repo, idx, "a.txt", "hello\n");
  std::string first = cmd_commit(repo, "first", kId);
  REQUIRE_THROWS(cmd_commit(repo, "again", kId)); // same tree as HEAD
  REQUIRE_EQ(ref_resolve(repo, "HEAD"), first);
}

TEST(commit_on_detached_head_moves_head_only) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  stage(repo, idx, "a.txt", "hello\n");
  std::string first = cmd_commit(repo, "first", kId);
  write_text(repo.fitdir / "HEAD", first + "\n");
  stage(repo, idx, "b.txt", "b\n");
  std::string second = cmd_commit(repo, "second", kId);
  REQUIRE_EQ(rstrip_nl(read_text(repo.fitdir / "HEAD")), second);
  REQUIRE_EQ(ref_resolve(repo, "refs/heads/master"), first);
}
