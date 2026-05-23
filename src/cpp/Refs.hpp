#pragma once
#include "Repository.hpp"
#include <map>
#include <string>
#include <variant>

struct RefNode {
  std::variant<std::string, std::map<std::string, RefNode>> value;
};

using RefMap = std::map<std::string, RefNode>;

std::string ref_resolve(const FitRepository &repo, const std::string &ref);
RefMap ref_list(const FitRepository &repo, fs::path path = "");
void ref_create(const FitRepository &repo, const std::string &ref_name,
                const std::string &sha);
