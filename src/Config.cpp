#include "Config.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ini.h>

namespace fs = std::filesystem;

std::string trim_ini_string(const std::string &str, bool normalize_keys) {
  if (str.empty())
    return "";
  size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return "";
  size_t last = str.find_last_not_of(" \t\r\n");
  std::string out = str.substr(first, (last - first + 1));
  if (normalize_keys)
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
  return out;
}

static int fit_ini_handler(void *user, const char *section, const char *name,
                           const char *value) {
  auto *conf = static_cast<ConfigMap *>(user);
  std::string s = trim_ini_string(section ? section : "", /*normalize=*/true);
  std::string k = trim_ini_string(name ? name : "", /*normalize=*/true);
  std::string v = trim_ini_string(value ? value : "", /*normalize=*/false);
  if (!s.empty() && !k.empty())
    (*conf)[s][k] = v;
  return 1;
}

void parse_ini_file(const fs::path &path, ConfigMap &out) {
  ini_parse(path.string().c_str(), fit_ini_handler, &out);
}

ConfigMap parse_ini_file(const fs::path &path) {
  ConfigMap out;
  parse_ini_file(path, out);
  return out;
}

std::map<std::string, std::map<std::string, std::string>> fitconfig_read() {
  std::map<std::string, std::map<std::string, std::string>> conf;
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  fs::path home = std::getenv("HOME") ? std::getenv("HOME") : "";
  fs::path p1 =
      xdg ? fs::path(xdg) / "fit/config" : home / ".config/fit/config";
  fs::path p2 = home / ".fitconfig";

  for (const auto &p : {p1, p2}) {
    if (fs::exists(p))
      parse_ini_file(p, conf);
  }

  if (!conf.count("user") || !conf["user"].count("name") ||
      !conf["user"].count("email")) {
    fs::path git_p1 =
        xdg ? fs::path(xdg) / "git/config" : home / ".config/git/config";
    fs::path git_p2 = home / ".gitconfig";
    for (const auto &p : {git_p1, git_p2}) {
      if (fs::exists(p))
        parse_ini_file(p, conf);
    }
  }

  return conf;
}

std::string fitconfig_user_get(
    const std::map<std::string, std::map<std::string, std::string>> &config) {
  if (config.count("user") && config.at("user").count("name") &&
      config.at("user").count("email")) {
    return config.at("user").at("name") + " <" + config.at("user").at("email") +
           ">";
  }
  return "";
}

void fitconfig_set(const fs::path &config_path, const std::string &section,
                   const std::string &key, const std::string &value) {
  ConfigMap conf;
  if (fs::exists(config_path))
    parse_ini_file(config_path, conf);
  conf[section][key] = value;

  fs::create_directories(config_path.parent_path());
  std::ofstream out(config_path);
  if (!out)
    throw std::runtime_error("Cannot write config: " + config_path.string());

  std::map<std::string, std::map<std::string, std::string>> ordered(
      conf.begin(), conf.end());
  for (auto &[sec, kvs] : ordered) {
    out << "[" << sec << "]\n";
    for (auto &[k, v] : kvs)
      out << "\t" << k << " = " << v << "\n";
  }
}
