#include "../src/Objects.hpp"
#include "../src/Repository.hpp"
#include "framework.hpp"
#include "support.hpp"

static std::vector<uint8_t> to_bytes(const std::string &s) {
  return std::vector<uint8_t>(s.begin(), s.end());
}

TEST(tree_roundtrip) {
  FitTree tree;
  tree.items.push_back(
      {"100644", "b.txt", "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"});
  tree.items.push_back(
      {"100644", "a.txt", "000000000000000000000000000000000000dead"});
  tree.items.push_back(
      {"040000", "sub", "111111111111111111111111111111111111beef"});

  auto raw = tree.serialize();
  FitTree back;
  back.deserialize(raw);

  REQUIRE_EQ(back.items.size(), size_t(3));
  // deserialize must recover exactly what was serialized, path by path,
  // regardless of what order serialize sorted them into.
  for (auto &want : tree.items) {
    bool found = false;
    for (auto &got : back.items)
      if (got.path == want.path) {
        REQUIRE_EQ(got.mode, want.mode);
        REQUIRE_EQ(got.sha, want.sha);
        found = true;
      }
    REQUIRE(found);
  }
}

TEST(tree_sorts_directories_with_trailing_slash) {
  // Git's rule: compare "foo" (a directory) as if it were "foo/". Since
  // '.' (0x2E) sorts before '/' (0x2F), "foo.txt" sorts BEFORE the
  // directory "foo" once that rule is applied -- a naive plain-string
  // comparison would agree here too, so this alone wouldn't catch a
  // missing trailing-slash rule; it exists to pin the expected order down
  // precisely, cross-checked against real git in the next test.
  FitTree tree;
  tree.items.push_back(
      {"040000", "foo", "111111111111111111111111111111111111beef"});
  tree.items.push_back(
      {"100644", "foo.txt", "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"});
  auto raw = tree.serialize();
  FitTree back;
  back.deserialize(raw);
  REQUIRE_EQ(back.items.size(), size_t(2));
  REQUIRE_EQ(back.items[0].path, std::string("foo.txt"));
  REQUIRE_EQ(back.items[1].path, std::string("foo"));
}

TEST(tree_matches_real_git_mktree) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);

  FitBlob a, b;
  a.blobdata = to_bytes("aaa\n");
  b.blobdata = to_bytes("bbb\n");
  std::string sha_a = object_write(a, repo);
  std::string sha_b = object_write(b, repo);

  FitTree tree;
  tree.items.push_back({"100644", "a.txt", sha_a});
  tree.items.push_back({"100644", "b.txt", sha_b});
  std::string sha = object_write(tree, repo);

  expose_as_dotgit(tmp.path);
  std::ofstream spec(tmp.path / "spec.txt");
  spec << "100644 blob " << sha_a << "\ta.txt\n";
  spec << "100644 blob " << sha_b << "\tb.txt\n";
  spec.close();
  std::string git_sha = rstrip_nl(git(tmp.path, "mktree < spec.txt"));
  REQUIRE_EQ(sha, git_sha);
}

TEST(tree_rejects_truncated_entries) {
  auto parse = [](const std::string &s) {
    FitTree t;
    t.deserialize(std::vector<uint8_t>(s.begin(), s.end()));
    return t;
  };
  std::string sha20(20, '\x11');
  REQUIRE_EQ(parse(std::string("100644 a\0", 9) + sha20).items.size(),
             size_t(1));
  REQUIRE_THROWS(parse(std::string("100644 a\0", 9) + sha20.substr(0, 19)));
  REQUIRE_THROWS(parse("100644 a-without-nul"));
}
