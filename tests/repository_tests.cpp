#include "../src/Repository.hpp"
#include "framework.hpp"
#include "support.hpp"

TEST(repository_creates_directory_layout) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  REQUIRE(fs::is_directory(repo.fitdir / "objects"));
  REQUIRE(fs::is_directory(repo.fitdir / "refs" / "heads"));
  REQUIRE(fs::is_directory(repo.fitdir / "refs" / "tags"));
}

TEST(repository_head_is_symbolic_ref) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::ifstream f(repo.fitdir / "HEAD");
  REQUIRE(static_cast<bool>(f));
  std::string line;
  std::getline(f, line);
  REQUIRE_EQ(line, std::string("ref: refs/heads/master"));
}

TEST(repository_repo_find_from_subdirectory) {
  TempDir tmp;
  repo_create(tmp.path);
  fs::path nested = tmp.path / "a" / "b" / "c";
  fs::create_directories(nested);
  FitRepository found = repo_find(nested);
  REQUIRE_EQ(fs::canonical(found.worktree), fs::canonical(tmp.path));
}

TEST(repository_repo_find_missing_is_not_required) {
  // A directory with no .fit anywhere above it. required=false must return
  // an empty FitRepository, not throw — a stub that unconditionally throws
  // fails this the same as everything else, so it isn't a vacuous check.
  TempDir tmp;
  FitRepository found = repo_find(tmp.path, /*required=*/false);
  REQUIRE(found.worktree.empty());
}
