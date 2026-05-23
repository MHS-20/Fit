#pragma once
#include "Repository.hpp"
#include <string>
#include <vector>
#include <cstdint>

enum class PackObjectType {
    commit = 1,
    tree = 2,
    blob = 3,
    tag = 4
};

std::vector<uint8_t> pkt_build_lines(const std::vector<std::string>& lines);
std::vector<std::string> pkt_read_lines(const std::vector<uint8_t>& data);
void push(const FitRepository& repo, const std::string& git_url, const std::string& username, const std::string& password);