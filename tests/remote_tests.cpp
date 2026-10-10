#include "../src/Commands.hpp"
#include "../src/Index.hpp"
#include "../src/Objects.hpp"
#include "../src/Remote.hpp"
#include "framework.hpp"
#include "support.hpp"
#include <cstdlib>

static std::vector<uint8_t> bytes(const std::string &s) {
  return {s.begin(), s.end()};
}

TEST(remote_pktline_roundtrip) {
  // The length counts its own 4 bytes: "abc\n" is "0008abc\n".
  REQUIRE(pkt_build_lines({"abc\n"}) == bytes("0008abc\n"));
  REQUIRE(pkt_build_lines({"abc\n", std::nullopt}) == bytes("0008abc\n0000"));
  REQUIRE(pkt_build_lines({std::string()}) == bytes("0004"));

  std::string nul_line = std::string("sha refs/heads/x\0caps\n", 22);
  std::vector<PktLine> lines = {"# service=git-receive-pack\n",
                                std::nullopt,
                                nul_line,
                                "x",
                                std::string(),
                                std::nullopt};
  REQUIRE(pkt_read_lines(pkt_build_lines(lines)) == lines);
}

TEST(remote_pktline_flush_is_not_empty_line) {
  // "0000" is a flush-pkt (section end); "0004" is a pkt-line whose payload
  // is genuinely empty. The codec must keep them distinct.
  auto flush = pkt_read_lines(bytes("0000"));
  auto empty = pkt_read_lines(bytes("0004"));
  REQUIRE_EQ(flush.size(), size_t(1));
  REQUIRE_EQ(empty.size(), size_t(1));
  REQUIRE(flush != empty);
  REQUIRE(!flush[0].has_value());
  REQUIRE(empty[0] == std::string());
}

TEST(remote_parse_ref_advertisement) {
  // Hand-written in GitHub's info/refs shape: service line, flush, a first
  // ref line carrying "\0<capabilities>", then master, then flush. master is
  // deliberately NOT first.
  std::ifstream f(fs::path(FIT_FIXTURES) / "github_info_refs_receive_pack.bin",
                  std::ios::binary);
  REQUIRE(static_cast<bool>(f));
  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
  auto lines = pkt_read_lines(raw);
  REQUIRE_EQ(parse_ref_advertisement(lines, "refs/heads/master"),
             std::string("a1b2c3d4e5f60718293a4b5c6d7e8f9012345678"));
  // The first line's ref name ends at the NUL, not at the newline.
  REQUIRE_EQ(parse_ref_advertisement(lines, "refs/heads/feature"),
             std::string("0123456789abcdef0123456789abcdef01234567"));
  REQUIRE_EQ(parse_ref_advertisement(lines, "refs/heads/main"), std::string());

  // Empty repo: no refs at all.
  REQUIRE_EQ(
      parse_ref_advertisement({"# service=git-receive-pack\n", std::nullopt},
                              "refs/heads/master"),
      std::string());
}

TEST(remote_pktline_rejects_malformed_input) {
  auto throws = [](const std::string &s) {
    try {
      pkt_read_lines(bytes(s));
    } catch (const std::runtime_error &) {
      return true;
    }
    return false;
  };
  REQUIRE(!throws("0008abc\n0000"));
  REQUIRE(throws("00"));      // truncated length
  REQUIRE(throws("zzzzabc")); // non-hex length
  REQUIRE(throws("0001"));    // protocol v2 delimiter
  REQUIRE(throws("0010abc")); // length runs past the data
}

TEST(remote_live_push_to_github) {
  const char *token = std::getenv("FIT_TEST_GITHUB_TOKEN");
  const char *slug = std::getenv("FIT_TEST_GITHUB_REPO");
  REQUIRE_GATE(
      token && *token && slug && *slug,
      "set FIT_TEST_GITHUB_TOKEN and FIT_TEST_GITHUB_REPO=<user>/<repo>");

  TempDir tmp;
  FitRepository repo = repo_create(tmp.path);
  std::string content =
      "pushed by fit " + std::to_string(std::time(nullptr)) + "\n";
  FitBlob b;
  b.blobdata = bytes(content);
  FitIndex idx;
  FitIndexEntry e;
  e.name = "fit.txt";
  e.sha = object_write(b, repo);
  e.mode_type = 0b1000;
  e.mode_perms = 0644;
  idx.entries.push_back(e);
  index_write(repo, idx);

  std::string url = std::string("https://github.com/") + slug + ".git";
  std::string remote =
      get_remote_ref_hash(url, "x-access-token", token, "refs/heads/master");
  REQUIRE_GATE(remote.empty(),
               "FIT_TEST_GITHUB_REPO must be an EMPTY scratch repo: fit has no "
               "fetch, so it can't build on top of existing remote commits");
  std::string sha =
      cmd_commit(repo, "fit live push",
                 "Fit Tester <fit@example.com> " +
                     std::to_string(std::time(nullptr)) + " +0000");

  push(repo, url, "x-access-token", token);

  std::string authed = std::string("https://x-access-token:") + token +
                       "@github.com/" + slug + ".git";
  std::string out =
      git(tmp.path, "ls-remote '" + authed + "' refs/heads/master");
  REQUIRE_EQ(out, sha + "\trefs/heads/master\n");
}
