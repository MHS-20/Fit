#include "../src/Objects.hpp"
#include "../src/Repository.hpp"
#include "framework.hpp"
#include "support.hpp"

static const std::string kTree = "4b825dc642cb6eb9a060e54bf8d69288fbee4904";
static const std::string kParentA = "1111111111111111111111111111111111111111";
static const std::string kParentB = "2222222222222222222222222222222222222222";
static const std::string kSig = "Fit Tester <fit@example.com> 1700000000 +0000";

static std::shared_ptr<FitCommit> roundtrip(FitRepository &repo, FitCommit &c) {
  std::string sha = object_write(c, repo);
  auto obj = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));
  REQUIRE(obj != nullptr);
  return obj;
}

TEST(commit_object_roundtrip) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitCommit c;
  c.kvlm["tree"] = {kTree};
  c.kvlm["parent"] = {kParentA};
  c.kvlm["author"] = {kSig};
  c.kvlm["committer"] = {kSig};
  c.kvlm[""] = {"first commit\n"};
  auto back = roundtrip(repo, c);
  REQUIRE(back->kvlm == c.kvlm);
}

TEST(commit_object_multiline_message) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitCommit c;
  c.kvlm["tree"] = {kTree};
  c.kvlm["author"] = {kSig};
  c.kvlm["committer"] = {kSig};
  // A multi-line header value exercises the "\n " continuation escape; a
  // multi-line message must come back verbatim.
  c.kvlm["gpgsig"] = {"-----BEGIN-----\nabc\n-----END-----"};
  c.kvlm[""] = {"subject\n\nbody line 1\nbody line 2\n"};
  auto back = roundtrip(repo, c);
  REQUIRE(back->kvlm == c.kvlm);
}

TEST(commit_object_multiple_parents) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitCommit c;
  c.kvlm["tree"] = {kTree};
  c.kvlm["parent"] = {kParentA, kParentB};
  c.kvlm["author"] = {kSig};
  c.kvlm["committer"] = {kSig};
  c.kvlm[""] = {"merge\n"};
  auto back = roundtrip(repo, c);
  REQUIRE_EQ(back->kvlm["parent"].size(), size_t(2));
  REQUIRE_EQ(back->kvlm["parent"][0], kParentA);
  REQUIRE_EQ(back->kvlm["parent"][1], kParentB);
}

TEST(commit_object_matches_real_git_commit_tree) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  expose_as_dotgit(tmp.path);

  // Real git needs the tree and parent to exist: empty tree, then a root
  // commit to use as the parent.
  std::string env = "env GIT_AUTHOR_NAME='Fit Tester' "
                    "GIT_AUTHOR_EMAIL=fit@example.com "
                    "GIT_AUTHOR_DATE='1700000000 +0000' "
                    "GIT_COMMITTER_NAME='Fit Tester' "
                    "GIT_COMMITTER_EMAIL=fit@example.com "
                    "GIT_COMMITTER_DATE='1700000000 +0000' git ";
  auto run = [&](const std::string &args) {
    auto [code, out] = run_in_dir(tmp.path, env + args);
    if (code != 0)
      throw TestFailure{"git " + args + " failed: " + out};
    return rstrip_nl(out);
  };
  std::string tree = run("mktree </dev/null");
  std::string parent = run("commit-tree " + tree + " -m root");
  std::string git_sha =
      run("commit-tree " + tree + " -p " + parent + " -m second");

  FitCommit c;
  c.kvlm["tree"] = {tree};
  c.kvlm["parent"] = {parent};
  c.kvlm["author"] = {kSig};
  c.kvlm["committer"] = {kSig};
  c.kvlm[""] = {"second\n"};
  REQUIRE_EQ(object_write(c, repo), git_sha);
}

TEST(commit_object_rejects_malformed_commits) {
  auto parse = [](const std::string &s) {
    FitCommit c;
    c.deserialize(std::vector<uint8_t>(s.begin(), s.end()));
    return c;
  };
  REQUIRE(parse("tree abc\n\nmsg\n").kvlm["tree"][0] == "abc");
  REQUIRE_THROWS(parse("tree abc"));                  // no newline at all
  REQUIRE_THROWS(parse("tree abc\nno-blank-line\n")); // header line w/o value
}

TEST(commit_object_tag_matches_real_git_mktag) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  expose_as_dotgit(tmp.path);
  std::string tree = rstrip_nl(git(tmp.path, "mktree </dev/null"));
  std::string commit =
      rstrip_nl(git(tmp.path, "-c user.name=t -c user.email=t@t commit-tree " +
                                  tree + " -m x"));

  FitTag tag;
  tag.kvlm["object"] = {commit};
  tag.kvlm["type"] = {"commit"};
  tag.kvlm["tag"] = {"v1"};
  tag.kvlm["tagger"] = {kSig};
  tag.kvlm[""] = {"release\n"};
  std::string sha = object_write(tag, repo);

  std::string text = "object " + commit + "\ntype commit\ntag v1\ntagger " +
                     kSig + "\n\nrelease\n";
  std::ofstream(tmp.path / "tag.txt") << text;
  REQUIRE_EQ(rstrip_nl(git(tmp.path, "mktag < tag.txt")), sha);
}
