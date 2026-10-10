#include "../src/Commands.hpp"
#include "framework.hpp"

using Lines = std::vector<std::string>;

// Dropping '+' lines must give back old; dropping '-' lines must give back new.
static void require_reconstructs(const std::vector<DiffLine> &d,
                                 const Lines &old_lines,
                                 const Lines &new_lines) {
  Lines old_back, new_back;
  for (auto &l : d) {
    REQUIRE(l.tag == ' ' || l.tag == '-' || l.tag == '+');
    if (l.tag != '+')
      old_back.push_back(l.text);
    if (l.tag != '-')
      new_back.push_back(l.text);
  }
  REQUIRE(old_back == old_lines);
  REQUIRE(new_back == new_lines);
}

static std::string tags(const std::vector<DiffLine> &d) {
  std::string s;
  for (auto &l : d)
    s += l.tag;
  return s;
}

TEST(diff_no_changes) {
  Lines a = {"one", "two", "three"};
  auto d = diff_lines(a, a);
  REQUIRE_EQ(tags(d), std::string("   "));
  require_reconstructs(d, a, a);
}

TEST(diff_pure_append) {
  Lines a = {"one", "two"}, b = {"one", "two", "three", "four"};
  auto d = diff_lines(a, b);
  REQUIRE_EQ(tags(d), std::string("  ++"));
  require_reconstructs(d, a, b);
}

TEST(diff_pure_deletion) {
  Lines a = {"one", "two", "three"}, b = {"one", "three"};
  auto d = diff_lines(a, b);
  REQUIRE_EQ(tags(d), std::string(" - "));
  require_reconstructs(d, a, b);
}

TEST(diff_line_changed_in_middle) {
  // A minimal diff is " -+ " (replace b with X). The naive walk has no
  // lookahead, so it deletes everything after the mismatch and re-adds it:
  // correct (it reconstructs both sides) but not minimal.
  Lines a = {"a", "b", "c"}, b = {"a", "X", "c"};
  auto d = diff_lines(a, b);
  require_reconstructs(d, a, b);
  REQUIRE_EQ(tags(d), std::string(" --++"));
}

TEST(diff_marks_missing_final_newline) {
  auto with_nl = split_diff_lines("a\nb\n");
  auto without = split_diff_lines("a\nb");
  REQUIRE(with_nl == Lines({"a", "b"}));
  REQUIRE(without == Lines({"a", "b\n\\ No newline at end of file"}));
  REQUIRE_EQ(tags(diff_lines(with_nl, without)), std::string(" -+"));
  REQUIRE(split_diff_lines("").empty());
}
