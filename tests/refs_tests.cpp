#include "../src/Refs.hpp"
#include "../src/Repository.hpp"
#include "framework.hpp"
#include "support.hpp"

static const std::string kSha = "1111111111111111111111111111111111111111";

static bool direct_ref_works() {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  ref_create(repo, "heads/probe", kSha);
  return ref_resolve(repo, "refs/heads/probe") == kSha;
}

TEST(refs_direct_ref) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  ref_create(repo, "heads/feature", kSha);
  REQUIRE(fs::is_regular_file(repo.fitdir / "refs" / "heads" / "feature"));
  REQUIRE_EQ(ref_resolve(repo, "refs/heads/feature"), kSha);
}

TEST(refs_symbolic_head_chain) {
  // repo_create already wrote HEAD as "ref: refs/heads/master"; once master
  // exists, resolving HEAD must follow the indirection to the SHA.
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  ref_create(repo, "heads/master", kSha);
  REQUIRE_EQ(ref_resolve(repo, "HEAD"), kSha);
}

TEST(refs_missing_ref) {
  // "" for a missing ref is exactly what a stub returning "" always does, so
  // this only counts once a real ref resolves.
  REQUIRE_GATE(direct_ref_works(), "refs_direct_ref must pass first");
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  REQUIRE_EQ(ref_resolve(repo, "refs/heads/nope"), std::string());
  // A fresh repo's HEAD points at a master that doesn't exist yet.
  REQUIRE_EQ(ref_resolve(repo, "HEAD"), std::string());
}

TEST(refs_matches_real_git_symbolic_ref) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  expose_as_dotgit(tmp.path);
  // rev-parse refuses a ref to a missing object, so point master at a real
  // commit made by git itself.
  std::string tree = rstrip_nl(git(tmp.path, "mktree </dev/null"));
  std::string commit =
      rstrip_nl(git(tmp.path, "-c user.name=t -c user.email=t@t commit-tree " +
                                  tree + " -m x"));
  ref_create(repo, "heads/master", commit);

  REQUIRE_EQ(rstrip_nl(git(tmp.path, "symbolic-ref HEAD")),
             std::string("refs/heads/master"));
  REQUIRE_EQ(rstrip_nl(git(tmp.path, "rev-parse HEAD")),
             ref_resolve(repo, "HEAD"));
  REQUIRE_EQ(ref_resolve(repo, "HEAD"), commit);
}

TEST(refs_symbolic_cycle_throws) {
  REQUIRE_GATE(direct_ref_works(), "refs_direct_ref must pass first");
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  write_text(repo.fitdir / "refs" / "heads" / "a", "ref: refs/heads/b\n");
  write_text(repo.fitdir / "refs" / "heads" / "b", "ref: refs/heads/a\n");
  REQUIRE_THROWS(ref_resolve(repo, "refs/heads/a"));
}

TEST(refs_head_target) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  REQUIRE_EQ(ref_head_target(repo), std::string("refs/heads/master"));
  write_text(repo.fitdir / "HEAD", kSha + "\n");
  REQUIRE_EQ(ref_head_target(repo), std::string());
}
