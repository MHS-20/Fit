#pragma once
#include "Repository.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>

struct FitObject {
    virtual std::string get_fmt() const = 0;
    virtual std::vector<uint8_t> serialize() = 0;
    virtual void deserialize(const std::vector<uint8_t>& data) = 0;
    virtual ~FitObject() = default;
};

struct FitBlob : public FitObject {
    std::vector<uint8_t> blobdata;
    std::string get_fmt() const override { return "blob"; }
    std::vector<uint8_t> serialize() override { return blobdata; }
    void deserialize(const std::vector<uint8_t>& data) override { blobdata = data; }
};

struct FitCommit : public FitObject {
    std::map<std::string, std::vector<std::string>> kvlm;
    std::string get_fmt() const override { return "commit"; }
    std::vector<uint8_t> serialize() override;
    void deserialize(const std::vector<uint8_t>& data) override;
};

struct FitTag : public FitCommit {
    std::string get_fmt() const override { return "tag"; }
};

struct FitTreeLeaf {
    std::string mode;
    std::string path;
    std::string sha;
};

struct FitTree : public FitObject {
    std::vector<FitTreeLeaf> items;
    std::string get_fmt() const override { return "tree"; }
    std::vector<uint8_t> serialize() override;
    void deserialize(const std::vector<uint8_t>& data) override;
};

std::vector<uint8_t> zlib_compress(const std::vector<uint8_t>& data);
// Throws if `data` is not a complete zlib stream.
std::vector<uint8_t> zlib_decompress(const std::vector<uint8_t>& data);
std::string sha1_hex(const std::vector<uint8_t>& data);

std::shared_ptr<FitObject> object_read(const FitRepository& repo, const std::string& sha);
std::string object_write(FitObject& obj, const FitRepository& repo);
// SHA of `obj` as it would be stored, without writing it.
std::string object_sha(FitObject& obj);
// Parses `fd` as an object of type `fmt` and returns its SHA; stores it only if `write`.
std::string object_hash(std::istream& fd, const std::string& fmt, const FitRepository& repo, bool write = true);
// Resolves HEAD, a short or full SHA, a tag or a branch. With `fmt`, the
// result must be (or peel to) that type. Returns "" if nothing matches;
// throws if a short SHA is ambiguous or the type is wrong.
std::string object_find(const FitRepository& repo, const std::string& name, const std::string& fmt = "");