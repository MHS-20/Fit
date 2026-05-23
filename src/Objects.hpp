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

std::shared_ptr<FitObject> object_read(const FitRepository& repo, const std::string& sha);
std::string object_write(FitObject& obj, const FitRepository& repo);
std::string object_hash(std::istream& fd, const std::string& fmt, const FitRepository& repo);
std::string object_find(const FitRepository& repo, const std::string& name, const std::string& fmt = "");