#include "../src/Config.hpp"
#include "framework.hpp"
#include "support.hpp"

TEST(config_reads_git_style_indented_keys) {
  // git indents every key; inih alone would read the second indented line
  // as a continuation of the first key's value.
  TempDir tmp;
  write_text(tmp.path / "config",
             "[user]\n\tname = Ada\n\temail = ada@example.com\n"
             "[core]\n    bare = false\n");
  ConfigMap conf = parse_ini_file(tmp.path / "config");
  REQUIRE_EQ(conf["user"]["name"], std::string("Ada"));
  REQUIRE_EQ(conf["user"]["email"], std::string("ada@example.com"));
  REQUIRE_EQ(conf["core"]["bare"], std::string("false"));
}

TEST(config_set_roundtrips_through_read) {
  TempDir tmp;
  fs::path p = tmp.path / "fit" / "config";
  fitconfig_set(p, "user", "name", "Ada");
  fitconfig_set(p, "user", "email", "ada@example.com");
  ConfigMap conf = parse_ini_file(p);
  REQUIRE_EQ(fitconfig_user_get(conf), std::string("Ada <ada@example.com>"));
}
