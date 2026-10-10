#pragma once
#include "Repository.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <set>

enum class PackObjectType {
    commit = 1,
    tree = 2,
    blob = 3,
    tag = 4
};

// Adds `sha` and every object reachable from it to `objs`.
void collect_objects(const FitRepository& repo, const std::string& sha, std::set<std::string>& objs);
// Objects reachable from `local` but not from `remote` ("" = nothing).
std::vector<std::string> find_missing_objects(const FitRepository& repo, const std::string& local, const std::string& remote);
std::vector<uint8_t> encode_pack_object_header(PackObjectType type, size_t size);
std::vector<uint8_t> create_pack(const FitRepository& repo, const std::vector<std::string>& objects);

std::vector<uint8_t> pkt_build_lines(const std::vector<std::string>& lines);
std::vector<std::string> pkt_read_lines(const std::vector<uint8_t>& data);
// Finds refs/heads/master (else main) in decoded info/refs pkt-lines.
std::string parse_ref_advertisement(const std::vector<std::string>& lines, std::string& out_branch);
std::string get_remote_master_hash(const std::string& git_url, const std::string& username, const std::string& password, std::string& out_branch);
void push(const FitRepository& repo, const std::string& git_url, const std::string& username, const std::string& password);