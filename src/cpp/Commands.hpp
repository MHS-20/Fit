#pragma once
#include <vector>
#include <string>

void cmd_init(const std::string& path);
void cmd_cat_file(const std::string& type, const std::string& object);
void cmd_hash_object(const std::string& type, bool write, const std::string& path);
void cmd_log(const std::string& commit);
void cmd_ls_tree(bool recursive, const std::string& tree);
void cmd_checkout(const std::string& commit, const std::string& path);
void cmd_show_ref();
void cmd_tag(bool create_tag_object, const std::vector<std::string>& args);
void cmd_rm(const std::vector<std::string>& paths);
void cmd_add(const std::vector<std::string>& paths);
void cmd_commit(const std::string& message);
void cmd_status();
void cmd_push(const std::string& remote_url, const std::string& username, const std::string& password);