#include "../src/Commands.hpp"
#include "../src/Index.hpp"
#include "../src/Objects.hpp"
#include "../src/Remote.hpp"
#include "framework.hpp"
#include "support.hpp"

static const std::string kId = "Fit Tester <fit@example.com> 1700000000 +0000";

// Decoder for the pack entry header, independent of the encoder under test.
static void decode_header(const std::vector<uint8_t> &h, int &type,
                          size_t &size) {
  size_t i = 0;
  uint8_t byte = h.at(i++);
  type = (byte >> 4) & 7;
  size = byte & 15;
  int shift = 4;
  while (byte & 0x80) {
    byte = h.at(i++);
    size |= static_cast<size_t>(byte & 0x7F) << shift;
    shift += 7;
  }
  REQUIRE_EQ(i, h.size()); // no stray bytes after the last one
}

struct History {
  std::string c1, c2, blob_a, blob_b, blob_a2;
};

// c1: a.txt, dir/b.txt.  c2: a.txt changed, dir/b.txt unchanged.
static History make_history(FitRepository &repo) {
  auto blob = [&](const std::string &s) {
    FitBlob b;
    b.blobdata = std::vector<uint8_t>(s.begin(), s.end());
    return object_write(b, repo);
  };
  auto entry = [](const std::string &name, const std::string &sha) {
    FitIndexEntry e;
    e.name = name;
    e.sha = sha;
    e.mode_type = 0b1000;
    e.mode_perms = 0644;
    return e;
  };
  History h;
  h.blob_a = blob("a\n");
  h.blob_b = blob("b\n");
  h.blob_a2 = blob("a, changed\n");
  FitIndex idx;
  idx.entries = {entry("a.txt", h.blob_a), entry("dir/b.txt", h.blob_b)};
  index_write(repo, idx);
  h.c1 = cmd_commit(repo, "one", kId);
  idx.entries[0].sha = h.blob_a2;
  index_write(repo, idx);
  h.c2 = cmd_commit(repo, "two", kId);
  return h;
}

TEST(pack_header_encoding_small_and_large_sizes) {
  // 0..15 fit the first byte; 16 needs a second; the rest cross 7-bit edges.
  for (size_t size :
       {size_t(0), size_t(1), size_t(15), size_t(16), size_t(127), size_t(2047),
        size_t(2048), size_t(1) << 20, (size_t(1) << 32) + 5}) {
    for (auto t : {PackObjectType::commit, PackObjectType::tree,
                   PackObjectType::blob, PackObjectType::tag}) {
      auto h = encode_pack_object_header(t, size);
      int type;
      size_t back;
      decode_header(h, type, back);
      REQUIRE_EQ(type, static_cast<int>(t));
      REQUIRE_EQ(back, size);
    }
  }
  REQUIRE_EQ(encode_pack_object_header(PackObjectType::blob, 15).size(),
             size_t(1));
  REQUIRE_EQ(encode_pack_object_header(PackObjectType::blob, 16).size(),
             size_t(2));
}

TEST(pack_collect_objects_reachability) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  History h = make_history(repo);

  std::set<std::string> from_c1;
  collect_objects(repo, h.c1, from_c1);
  // c1, its root tree, the dir tree, a.txt, b.txt.
  REQUIRE_EQ(from_c1.size(), size_t(5));
  REQUIRE(from_c1.count(h.c1) && from_c1.count(h.blob_a) &&
          from_c1.count(h.blob_b));
  REQUIRE(!from_c1.count(h.c2) && !from_c1.count(h.blob_a2));

  std::set<std::string> from_c2;
  collect_objects(repo, h.c2, from_c2);
  // Everything in c1, plus c2, its new root tree, and the new a.txt — the
  // unchanged dir tree and b.txt are shared, not duplicated.
  REQUIRE_EQ(from_c2.size(), size_t(8));

  auto missing = find_missing_objects(repo, h.c2, h.c1);
  std::set<std::string> m(missing.begin(), missing.end());
  REQUIRE_EQ(m.size(), size_t(3));
  REQUIRE(m.count(h.c2) && m.count(h.blob_a2));
  REQUIRE(!m.count(h.blob_b) && !m.count(h.c1));
  // Remote has nothing yet: everything is missing.
  REQUIRE_EQ(find_missing_objects(repo, h.c2, "").size(), size_t(8));
}

TEST(pack_matches_real_git_index_pack) {
  TempDir tmp, bare;
  FitRepository repo = repo_create(tmp.path);
  History h = make_history(repo);
  auto objects = find_missing_objects(repo, h.c2, "");
  auto pack = create_pack(repo, objects);

  std::ofstream(tmp.path / "out.pack", std::ios::binary)
      .write(reinterpret_cast<const char *>(pack.data()), pack.size());
  git(bare.path, "init -q --bare .");
  // index-pack verifies the trailing checksum, every entry's header and zlib
  // stream, and hashes each object; it prints the pack's checksum on success.
  std::string out = git(bare.path, "index-pack --stdin < '" +
                                       (tmp.path / "out.pack").string() + "'");
  REQUIRE(out.find("pack") != std::string::npos);

  // Every object is now in the bare repo and identical to ours.
  for (auto &sha : objects)
    REQUIRE_EQ(rstrip_nl(git(bare.path, "cat-file -t " + sha)).empty(), false);
  REQUIRE_EQ(git(bare.path, "cat-file -p " + h.blob_a2),
             std::string("a, changed\n"));
  // And the history is whole: a full connectivity check from c2 passes.
  git(bare.path, "update-ref refs/heads/master " + h.c2);
  git(bare.path, "fsck --strict --no-dangling");
  REQUIRE_EQ(rstrip_nl(git(bare.path, "rev-list --count master")),
             std::string("2"));
}

TEST(pack_collect_throws_on_missing_object) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitTree t;
  t.items.push_back({"100644", "gone.txt", std::string(40, 'd')});
  std::string tree = object_write(t, repo);
  std::set<std::string> objs;
  REQUIRE_THROWS(collect_objects(repo, tree, objs));
}

TEST(pack_collect_skips_submodule_commits) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitTree t;
  t.items.push_back({"160000", "sub", std::string(40, 'e')}); // other repo
  std::string tree = object_write(t, repo);
  std::set<std::string> objs;
  collect_objects(repo, tree, objs);
  REQUIRE(objs == std::set<std::string>{tree});
}
