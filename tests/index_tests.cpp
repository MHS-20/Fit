#include "../src/Index.hpp"
#include "../src/Repository.hpp"
#include "framework.hpp"
#include "support.hpp"

static FitIndexEntry make_entry(const std::string &name,
                                const std::string &sha) {
  FitIndexEntry e;
  e.name = name;
  e.sha = sha;
  e.mode_type = 0b1000;
  e.mode_perms = 0644;
  e.fsize = 4;
  e.ctime = {1700000000, 0};
  e.mtime = {1700000000, 0};
  return e;
}

TEST(index_missing_index_returns_empty) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx = index_read(repo);
  REQUIRE_EQ(idx.entries.size(), size_t(0));
  REQUIRE_EQ(idx.version, uint32_t(2));
}

TEST(index_roundtrip_one_entry) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  idx.entries.push_back(
      make_entry("a.txt", "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"));
  index_write(repo, idx);

  FitIndex back = index_read(repo);
  REQUIRE_EQ(back.entries.size(), size_t(1));
  REQUIRE_EQ(back.entries[0].name, std::string("a.txt"));
  REQUIRE_EQ(back.entries[0].sha,
             std::string("e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"));
  REQUIRE_EQ(back.entries[0].fsize, uint32_t(4));
}

TEST(index_roundtrip_many_entries) {
  // Deliberately exercises name lengths that land ON an 8-byte padding
  // boundary and ones that don't, since that's exactly where an
  // off-by-one in the padding math desyncs every entry after it.
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  idx.entries.push_back(
      make_entry("a", "111111111111111111111111111111111111beef"));
  idx.entries.push_back(make_entry("dir/nested/file.txt",
                                   "222222222222222222222222222222222222beef"));
  idx.entries.push_back(
      make_entry("exactly8", "333333333333333333333333333333333333beef"));
  idx.entries.push_back(
      make_entry("z.txt", "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"));
  index_write(repo, idx);

  FitIndex back = index_read(repo);
  REQUIRE_EQ(back.entries.size(), size_t(4));
  for (size_t i = 0; i < idx.entries.size(); ++i) {
    REQUIRE_EQ(back.entries[i].name, idx.entries[i].name);
    REQUIRE_EQ(back.entries[i].sha, idx.entries[i].sha);
  }
}

TEST(index_matches_real_git_update_index) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::string sha = "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391";
  FitIndex idx;
  idx.entries.push_back(make_entry("file.txt", sha));
  // fsize/uid/gid/dev/ino/timestamps all zero so byte comparison against a
  // freshly update-index'd file is meaningful without pinning stat() data.
  idx.entries[0].fsize = 0;
  idx.entries[0].ctime = {0, 0};
  idx.entries[0].mtime = {0, 0};
  index_write(repo, idx);

  // .git will be a symlink to .fit (same underlying file!), so save a copy
  // of what index_write produced BEFORE letting real git's update-index
  // overwrite .fit/index in place.
  fs::copy_file(repo.fitdir / "index", tmp.path / "index.mine");

  expose_as_dotgit(tmp.path);
  git(tmp.path, "update-index --add --cacheinfo 100644," + sha + ",file.txt");

  std::ifstream mine(tmp.path / "index.mine", std::ios::binary);
  std::string mine_bytes((std::istreambuf_iterator<char>(mine)),
                         std::istreambuf_iterator<char>());
  std::ifstream theirs(tmp.path / ".fit" / "index", std::ios::binary);
  std::string their_bytes((std::istreambuf_iterator<char>(theirs)),
                          std::istreambuf_iterator<char>());
  // Byte-identical, trailing SHA-1 checksum included.
  REQUIRE_EQ(mine_bytes, their_bytes);
}

TEST(index_rejects_corrupt_checksum) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  idx.entries.push_back(make_entry("a.txt", std::string(40, 'a')));
  index_write(repo, idx);
  REQUIRE_EQ(index_read(repo).entries.size(), size_t(1));

  std::string bytes = read_text(repo.fitdir / "index");
  bytes[20] ^= 0x01; // inside the first entry
  write_text(repo.fitdir / "index", bytes);
  REQUIRE_THROWS(index_read(repo));
}

TEST(index_matches_real_git_many_entries) {
  // Unsorted on purpose, with name lengths that hit every padding remainder.
  std::vector<std::string> names = {
      "zz",    "a",      "dir/nested.txt", "bbb",      "cccc",
      "ddddd", "eeeeee", "fffffff",        "gggggggg", "dir.txt"};
  std::string sha = "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391";
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  for (auto &n : names) {
    idx.entries.push_back(make_entry(n, sha));
    idx.entries.back().fsize = 0;
    idx.entries.back().ctime = {0, 0};
    idx.entries.back().mtime = {0, 0};
  }
  index_write(repo, idx);
  std::string mine = read_text(repo.fitdir / "index");

  fs::remove(repo.fitdir / "index");
  expose_as_dotgit(tmp.path);
  for (auto &n : names)
    git(tmp.path, "update-index --add --cacheinfo 100644," + sha + "," + n);
  REQUIRE_EQ(mine, read_text(repo.fitdir / "index"));
}

TEST(index_long_name_roundtrip) {
  // Names of 0xFFF bytes or more don't fit the 12-bit length field.
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitIndex idx;
  std::string long_name = std::string(5000, 'x') + ".txt";
  idx.entries.push_back(make_entry(long_name, std::string(40, 'a')));
  idx.entries.push_back(make_entry("short", std::string(40, 'b')));
  index_write(repo, idx);
  FitIndex back = index_read(repo);
  REQUIRE_EQ(back.entries.size(), size_t(2));
  // Written sorted by name.
  REQUIRE_EQ(back.entries[0].name, std::string("short"));
  REQUIRE_EQ(back.entries[1].name, long_name);
}
