#pragma once
#include "Repository.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <set>

enum class PackObjectType {
    commit = 1,
    tree = 2,
    blob = 3,
    tag = 4
};

// Adds `sha` and every object reachable from it to `objs`, skipping objects
// in `known` and everything they reach. Throws if an object is missing.
void collect_objects(const FitRepository& repo, const std::string& sha, std::set<std::string>& objs,
                     const std::set<std::string>* known = nullptr);
// Objects reachable from `local` but not from `remote` ("" = nothing).
std::vector<std::string> find_missing_objects(const FitRepository& repo, const std::string& local, const std::string& remote);
std::vector<uint8_t> encode_pack_object_header(PackObjectType type, size_t size);
std::vector<uint8_t> create_pack(const FitRepository& repo, const std::vector<std::string>& objects);

// One pkt-line payload; std::nullopt is a flush-pkt ("0000"), distinct from
// an empty payload ("0004").
using PktLine = std::optional<std::string>;
std::vector<uint8_t> pkt_build_lines(const std::vector<PktLine>& lines);
std::vector<PktLine> pkt_read_lines(const std::vector<uint8_t>& data);

// SHA of `ref` (e.g. "refs/heads/master") in decoded info/refs pkt-lines, or "".
std::string parse_ref_advertisement(const std::vector<PktLine>& lines, const std::string& ref);
std::string get_remote_ref_hash(const std::string& git_url, const std::string& username, const std::string& password,
                                const std::string& ref);
// Pushes HEAD's branch to the same branch on the remote. Throws if the remote
// branch has commits missing locally, or the server rejects the update.
void push(const FitRepository& repo, const std::string& git_url, const std::string& username, const std::string& password);
