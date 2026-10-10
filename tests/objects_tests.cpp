#include "../src/Objects.hpp"
#include "../src/Repository.hpp"
#include "framework.hpp"
#include "support.hpp"
#include <fstream>
#include <map>

static std::vector<uint8_t> to_bytes(const std::string &s) {
  return std::vector<uint8_t>(s.begin(), s.end());
}

TEST(objects_write_then_read_roundtrip) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitBlob blob;
  blob.blobdata = to_bytes("hello fit\n");
  std::string sha = object_write(blob, repo);
  REQUIRE_EQ(sha.size(), size_t(40));

  auto obj = object_read(repo, sha);
  REQUIRE(obj != nullptr);
  REQUIRE_EQ(obj->get_fmt(), std::string("blob"));
  auto back = std::dynamic_pointer_cast<FitBlob>(obj);
  REQUIRE(back != nullptr);
  REQUIRE_EQ(back->blobdata, blob.blobdata);
}

TEST(objects_empty_blob_hash_matches_known_value) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitBlob blob; // blobdata left empty
  std::string sha = object_write(blob, repo);
  REQUIRE_EQ(sha, std::string("e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"));
}

TEST(objects_missing_object_returns_null) {
  // Gated behind the positive roundtrip: an object_read stub that always
  // returns nullptr would pass THIS check for free, so it only counts once
  // a real, distinguishable object has round-tripped successfully above.
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  FitBlob blob;
  blob.blobdata = to_bytes("gate-check\n");
  std::string sha = object_write(blob, repo);
  auto ok = object_read(repo, sha);
  REQUIRE_GATE(ok != nullptr &&
                   std::dynamic_pointer_cast<FitBlob>(ok)->blobdata ==
                       blob.blobdata,
               "needs a working object_read/object_write pair first");
  auto missing = object_read(repo, "000000000000000000000000000000000000dead");
  REQUIRE(missing == nullptr);
}

TEST(objects_matches_real_git_hash_object) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::string content = "content Fit and git must agree on\n";
  std::ofstream(tmp.path / "file.txt") << content;

  FitBlob blob;
  blob.blobdata = to_bytes(content);
  std::string sha = object_write(blob, repo);

  std::string git_sha = rstrip_nl(git(tmp.path, "hash-object file.txt"));
  REQUIRE_EQ(sha, git_sha);
}

TEST(objects_git_cat_file_reads_it) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::string content = "read me back through real git\n";
  FitBlob blob;
  blob.blobdata = to_bytes(content);
  std::string sha = object_write(blob, repo);

  expose_as_dotgit(tmp.path);
  std::string out = git(tmp.path, "cat-file -p " + sha);
  REQUIRE_EQ(out, content);
}

// Writes `raw` zlib-compressed at the loose-object path for `sha`.
static void plant_object(FitRepository &repo, const std::string &sha,
                         const std::string &raw, bool compress = true) {
  std::vector<uint8_t> bytes(raw.begin(), raw.end());
  std::vector<uint8_t> data = compress ? zlib_compress(bytes) : bytes;
  fs::path p = repo.fitdir / "objects" / sha.substr(0, 2) / sha.substr(2);
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary)
      .write(reinterpret_cast<const char *>(data.data()), data.size());
}

TEST(objects_rejects_malformed_objects) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::string sha(40, 'a');
  plant_object(repo, sha, std::string("blob11", 6)); // no space, no NUL
  REQUIRE_THROWS(object_read(repo, sha));
  plant_object(repo, sha, std::string("blob x\0abc", 10)); // non-numeric size
  REQUIRE_THROWS(object_read(repo, sha));
  plant_object(repo, sha, "not zlib at all", false);
  REQUIRE_THROWS(object_read(repo, sha));
}

TEST(objects_hash_without_write_stores_nothing) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::istringstream in("content\n");
  std::string sha = object_hash(in, "blob", repo, false);
  REQUIRE(object_read(repo, sha) == nullptr);
  std::istringstream again("content\n");
  REQUIRE_EQ(object_hash(again, "blob", repo, true), sha);
  REQUIRE(object_read(repo, sha) != nullptr);
  std::istringstream bad("x");
  REQUIRE_THROWS(object_hash(bad, "nonsense", repo, false));
}

TEST(objects_find_rejects_ambiguous_short_sha) {
  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  // Blobs until two share a 4-hex-digit prefix (birthday bound: ~300).
  std::map<std::string, std::string> by_prefix;
  std::string a, b;
  for (int i = 0; a.empty() && i < 20000; ++i) {
    FitBlob blob;
    blob.blobdata = to_bytes(std::to_string(i));
    std::string sha = object_write(blob, repo);
    auto [it, inserted] = by_prefix.emplace(sha.substr(0, 4), sha);
    if (!inserted) {
      a = it->second;
      b = sha;
    }
  }
  REQUIRE(!a.empty());
  REQUIRE_THROWS(object_find(repo, a.substr(0, 4)));
  // One more distinguishing digit (where they differ) resolves it.
  size_t diff = 4;
  while (a[diff] == b[diff])
    ++diff;
  REQUIRE_EQ(object_find(repo, a.substr(0, diff + 1)), a);
}
