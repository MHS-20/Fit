#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;
using ConfigMap = std::map<std::string, std::map<std::string, std::string>>;

std::map<std::string, std::map<std::string, std::string>> fitconfig_read();
std::string fitconfig_user_get(
    const std::map<std::string, std::map<std::string, std::string>> &config);

std::string trim_ini_string(const std::string &str,
                            bool normalize_keys = false);
void parse_ini_file(const fs::path &path, ConfigMap &out);
ConfigMap parse_ini_file(const fs::path &path);
