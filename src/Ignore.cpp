#include "Ignore.hpp"
#include "Index.hpp"
#include "Objects.hpp"
#include <cstdlib>
#include <fnmatch.h>
#include <fstream>
#include <optional>
#include <sstream>

std::vector<std::pair<std::string, bool>>
fitignore_parse(const std::vector<std::string> &lines) {
  std::vector<std::pair<std::string, bool>> ret;
  for (auto &line : lines) {
    std::string raw = line;
    raw.erase(0, raw.find_first_not_of(" \t\r\n"));
    raw.erase(raw.find_last_not_of(" \t\r\n") + 1);
    if (raw.empty() || raw[0] == '#')
      continue;
    if (raw[0] == '!')
      ret.push_back({raw.substr(1), false});
    else if (raw[0] == '\\')
      ret.push_back({raw.substr(1), true});
    else
      ret.push_back({raw, true});
  }
  return ret;
}

FitIgnore fitignore_read(const FitRepository &repo) {
  FitIgnore ret;
  fs::path excl = repo.fitdir / "info/exclude";
  if (fs::exists(excl)) {
    std::ifstream f(excl);
    std::string l;
    std::vector<std::string> ls;
    while (std::getline(f, l))
      ls.push_back(l);
    ret.absolute.push_back(fitignore_parse(ls));
  }
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  fs::path home = std::getenv("HOME") ? std::getenv("HOME") : "";
  fs::path global_file =
      xdg ? fs::path(xdg) / "fit/ignore" : home / ".config/fit/ignore";
  if (fs::exists(global_file)) {
    std::ifstream f(global_file);
    std::string l;
    std::vector<std::string> ls;
    while (std::getline(f, l))
      ls.push_back(l);
    ret.absolute.push_back(fitignore_parse(ls));
  }
  FitIndex idx = index_read(repo);
  for (auto &entry : idx.entries) {
    if (entry.name == ".fitignore" ||
        (entry.name.size() > 11 &&
         entry.name.substr(entry.name.size() - 11) == "/.fitignore")) {
      std::string dir = entry.name.size() == 11
                            ? ""
                            : entry.name.substr(0, entry.name.size() - 11);
      auto obj = object_read(repo, entry.sha);
      if (obj) {
        auto blob = std::dynamic_pointer_cast<FitBlob>(obj);
        std::string contents(blob->blobdata.begin(), blob->blobdata.end());
        std::stringstream ss(contents);
        std::string l;
        std::vector<std::string> ls;
        while (std::getline(ss, l))
          ls.push_back(l);
        ret.scoped[dir] = fitignore_parse(ls);
      }
    }
  }
  return ret;
}

bool match_pattern(const std::string &path, const std::string &pattern) {
  return fnmatch(pattern.c_str(), path.c_str(), 0) == 0;
}

std::optional<bool>
check_ignore_rules(const std::vector<std::pair<std::string, bool>> &rules,
                   const std::string &path) {
  std::optional<bool> res;
  for (auto &rule : rules) {
    if (match_pattern(path, rule.first))
      res = rule.second;
  }
  return res;
}

bool check_ignore(const FitIgnore &rules, const std::string &path) {
  fs::path p(path);
  if (p.is_absolute())
    throw std::runtime_error("Requires relative path");
  std::string parent = p.parent_path().string();
  while (true) {
    if (rules.scoped.count(parent)) {
      auto res = check_ignore_rules(rules.scoped.at(parent), path);
      if (res.has_value())
        return res.value();
    }
    if (parent.empty())
      break;
    parent = fs::path(parent).parent_path().string();
  }
  for (auto &ruleset : rules.absolute) {
    auto res = check_ignore_rules(ruleset, path);
    if (res.has_value())
      return res.value();
  }
  return false;
}
