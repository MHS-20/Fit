// End-to-end checks through the cmd_* entry points, which locate the
// repository from the current directory and the identity from $HOME.
#include "../src/Commands.hpp"
#include "../src/Config.hpp"
#include "../src/Objects.hpp"
#include "../src/Refs.hpp"
#include "framework.hpp"
#include "support.hpp"

// A repository in `tmp.path/repo` with a configured identity, entered for
// the lifetime of the fixture.
struct CliRepo {
  TempDir tmp;
  fs::path root = tmp.path / "repo";
  ScopedEnv home{"HOME", (tmp.path / "home").string()};
  ScopedEnv xdg{"XDG_CONFIG_HOME", (tmp.path / "home" / ".config").string()};
  std::optional<ScopedChdir> cd;
  CliRepo() {
    cmd_init(root.string());
    fitconfig_set(tmp.path / "home" / ".config" / "fit" / "config", "user",
                  "name", "Ada");
    fitconfig_set(tmp.path / "home" / ".config" / "fit" / "config", "user",
                  "email", "ada@example.com");
    cd.emplace(root);
  }
};

TEST(commands_commit_is_valid_for_real_git) {
  CliRepo r;
  write_text(r.root / "a.txt", "hello\n");
  write_text(r.root / "bin" / "run.sh", "#!/bin/sh\n");
  fs::permissions(r.root / "bin" / "run.sh", fs::perms::owner_all,
                  fs::perm_options::add);
  cmd_add({"a.txt", "bin/run.sh"});
  cmd_commit("first");

  expose_as_dotgit(r.root);
  // Author/committer carry a timestamp, so fsck --strict accepts the commit.
  auto [code, out] = run_in_dir(r.root, "git fsck --strict --no-dangling");
  if (code != 0)
    throw TestFailure{"git fsck failed: " + out};
  REQUIRE_EQ(git(r.root, "log -1 --format=%an%x20%ae%x20%s"),
             std::string("Ada ada@example.com first\n"));
  REQUIRE(git(r.root, "ls-tree -r HEAD").find("100755 blob") !=
          std::string::npos);
  // The index carries real stat data in git's format: git sees a clean tree.
  REQUIRE_EQ(git(r.root, "status --porcelain --untracked-files=no"),
             std::string());
}

TEST(commands_add_and_rm_reject_bad_paths) {
  CliRepo r;
  write_text(r.root / "a.txt", "a\n");
  cmd_add({"a.txt"});
  REQUIRE_THROWS(cmd_add({"missing.txt"}));
  REQUIRE_THROWS(cmd_rm({"untracked.txt"}));
  REQUIRE_THROWS(cmd_rm({"../outside.txt"}));
  cmd_rm({"a.txt"});
  REQUIRE(!fs::exists(r.root / "a.txt"));
  REQUIRE_THROWS(cmd_rm({"a.txt"})); // no longer tracked
}

TEST(commands_annotated_tag_is_valid_for_real_git) {
  CliRepo r;
  write_text(r.root / "a.txt", "a\n");
  cmd_add({"a.txt"});
  cmd_commit("first");
  REQUIRE_THROWS(cmd_tag(true, {"v1"}, "")); // annotated tags need -m
  cmd_tag(true, {"v1"}, "release");

  FitRepository repo = repo_find();
  std::string head = ref_resolve(repo, "HEAD");
  REQUIRE_EQ(object_find(repo, "v1", "commit"), head); // peels the tag
  expose_as_dotgit(r.root);
  REQUIRE_EQ(git(r.root, "cat-file -t v1"), std::string("tag\n"));
  REQUIRE_EQ(rstrip_nl(git(r.root, "rev-parse v1^{commit}")), head);
  git(r.root, "fsck --strict --no-dangling");
}
