#include "../src/Commands.hpp"
#include "../src/Index.hpp"
#include "../src/Objects.hpp"
#include "framework.hpp"
#include "support.hpp"

static const std::string kId = "Fit Tester <fit@example.com> 1700000000 +0000";

// Commits `n` times through cmd_commit, each adding file<i>.txt containing
// "<i>\n" with message "c<i>". Returns the SHAs oldest first.
static std::vector<std::string> make_history(FitRepository &repo, int n) {
  FitIndex idx;
  std::vector<std::string> shas;
  for (int i = 0; i < n; ++i) {
    std::string content = std::to_string(i) + "\n";
    FitBlob b;
    b.blobdata = std::vector<uint8_t>(content.begin(), content.end());
    FitIndexEntry e;
    e.name = "file" + std::to_string(i) + ".txt";
    e.sha = object_write(b, repo);
    e.mode_type = 0b1000;
    e.mode_perms = 0644;
    idx.entries.push_back(e);
    index_write(repo, idx);
    shas.push_back(cmd_commit(repo, "c" + std::to_string(i), kId));
  }
  return shas;
}

static std::vector<std::string> reversed(std::vector<std::string> v) {
  return {v.rbegin(), v.rend()};
}

TEST(log_linear_history) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  auto shas = make_history(repo, 5);
  REQUIRE(log_walk(repo, "HEAD") == reversed(shas));
  // Same walk from a branch name, and from a mid-history abbreviated SHA.
  REQUIRE(log_walk(repo, "master") == reversed(shas));
  auto from_mid = log_walk(repo, shas[2].substr(0, 7));
  REQUIRE(from_mid == std::vector<std::string>({shas[2], shas[1], shas[0]}));
}

TEST(log_single_commit) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  auto shas = make_history(repo, 1);
  REQUIRE(log_walk(repo, "HEAD") == shas);
}

TEST(log_matches_real_git_log) {
  TempDir mine, theirs;
  FitRepository repo = repo_create(mine.path);
  auto shas = make_history(repo, 5);

  // The same five commits via real `git commit`, identity and dates pinned
  // to match kId, so equal content must produce equal hashes.
  std::string env = "env GIT_AUTHOR_NAME='Fit Tester' "
                    "GIT_AUTHOR_EMAIL=fit@example.com "
                    "GIT_AUTHOR_DATE='1700000000 +0000' "
                    "GIT_COMMITTER_NAME='Fit Tester' "
                    "GIT_COMMITTER_EMAIL=fit@example.com "
                    "GIT_COMMITTER_DATE='1700000000 +0000' git ";
  git(theirs.path, "init -q .");
  for (int i = 0; i < 5; ++i) {
    std::string name = "file" + std::to_string(i) + ".txt";
    std::ofstream(theirs.path / name) << i << "\n";
    git(theirs.path, "add " + name);
    auto [code, out] = run_in_dir(
        theirs.path, env + "commit -q --no-gpg-sign -m c" + std::to_string(i));
    if (code != 0)
      throw TestFailure{"git commit failed: " + out};
  }
  std::istringstream lines(git(theirs.path, "log --format=%H"));
  std::vector<std::string> git_shas;
  for (std::string line; std::getline(lines, line);)
    git_shas.push_back(line);

  REQUIRE(log_walk(repo, "HEAD") == git_shas);
}
