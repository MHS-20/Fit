#pragma once
#include <map>
#include <string>

std::map<std::string, std::map<std::string, std::string>> fitconfig_read();
std::string fitconfig_user_get(const std::map<std::string, std::map<std::string, std::string>>& config);