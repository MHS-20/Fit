#pragma once
#include "Repository.hpp"
#include <string>
#include <vector>
#include <map>

struct FitIgnore {
    std::vector<std::vector<std::pair<std::string, bool>>> absolute;
    std::map<std::string, std::vector<std::pair<std::string, bool>>> scoped;
};

FitIgnore fitignore_read(const FitRepository& repo);
bool check_ignore(const FitIgnore& rules, const std::string& path);