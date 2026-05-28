#include "Commands.hpp"
#include <CLI/CLI.hpp>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  CLI::App app{"Fit version control system"};
  app.require_subcommand(1);

  std::string init_path = ".";
  std::string cat_type, cat_obj;
  std::string hash_type = "blob", hash_path;
  bool hash_write = false;
  std::string log_commit = "HEAD";
  bool ls_recurse = false;
  std::string ls_tree_obj;
  std::string co_commit, co_path;
  bool tag_obj_flag = false;
  std::vector<std::string> tag_args;
  std::vector<std::string> rm_paths;
  bool add_all = false;
  std::vector<std::string> add_paths;
  std::string commit_msg;
  std::string remote_url, remote_user, remote_pass;
  std::string branch_name, switch_name;
  bool config_global = false;
  std::string config_key, config_value;

  auto *init_cmd = app.add_subcommand("init", "Initialize empty repo");
  init_cmd->add_option("path", init_path);

  auto *cat_cmd = app.add_subcommand("cat-file", "Provide content of objects");
  cat_cmd->add_option("type", cat_type)->required();
  cat_cmd->add_option("object", cat_obj)->required();

  auto *hash_cmd = app.add_subcommand("hash-object", "Compute object ID");
  hash_cmd->add_option("-t", hash_type);
  hash_cmd->add_flag("-w", hash_write);
  hash_cmd->add_option("path", hash_path)->required();

  auto *log_cmd = app.add_subcommand("log", "Display commit history");
  log_cmd->add_option("commit", log_commit);

  auto *ls_cmd = app.add_subcommand("ls-tree", "Print tree objects");
  ls_cmd->add_flag("-r", ls_recurse);
  ls_cmd->add_option("tree", ls_tree_obj)->required();

  auto *checkout_cmd = app.add_subcommand("checkout", "Checkout working tree");
  checkout_cmd->add_option("commit", co_commit)->required();
  checkout_cmd->add_option("path", co_path)->required();

  auto *show_ref_cmd = app.add_subcommand("show-ref", "List references");

  auto *tag_cmd = app.add_subcommand("tag", "Handle tagging operations");
  tag_cmd->add_flag("-a", tag_obj_flag);
  tag_cmd->add_option("args", tag_args);

  auto *rm_cmd = app.add_subcommand("rm", "Remove paths");
  rm_cmd->add_option("paths", rm_paths)->required();

  auto *add_cmd = app.add_subcommand("add", "Add files to index");
  add_cmd->add_flag("-A,--all", add_all);
  add_cmd->add_option("paths", add_paths);

  auto *commit_cmd = app.add_subcommand("commit", "Create new commit");
  commit_cmd->add_option("-m", commit_msg)->required();

  auto *status_cmd =
      app.add_subcommand("status", "Show tracking changes state");

  auto *diff_cmd =
      app.add_subcommand("diff", "Show changes in working tree vs index");

  auto *branch_cmd = app.add_subcommand("branch", "List or create branches");
  branch_cmd->add_option("name", branch_name);

  auto *switch_cmd = app.add_subcommand("switch", "Switch to a branch");
  switch_cmd->add_option("name", switch_name)->required();

  auto *config_cmd = app.add_subcommand("config", "Get or set config values");
  config_cmd->add_flag("--global", config_global);
  config_cmd->add_option("key", config_key)->required();
  config_cmd->add_option("value", config_value)->required();

  auto *push_cmd =
      app.add_subcommand("push", "Push changes to remote network server");
  push_cmd->add_option("url", remote_url)->required();
  push_cmd->add_option("-u", remote_user);
  push_cmd->add_option("-p", remote_pass);

  CLI11_PARSE(app, argc, argv);

  const std::unordered_map<CLI::App *, std::function<void()>> dispatch{
      {init_cmd, [&] { cmd_init(init_path); }},
      {cat_cmd, [&] { cmd_cat_file(cat_type, cat_obj); }},
      {hash_cmd, [&] { cmd_hash_object(hash_type, hash_write, hash_path); }},
      {log_cmd, [&] { cmd_log(log_commit); }},
      {ls_cmd, [&] { cmd_ls_tree(ls_recurse, ls_tree_obj); }},
      {checkout_cmd, [&] { cmd_checkout(co_commit, co_path); }},
      {show_ref_cmd, [&] { cmd_show_ref(); }},
      {tag_cmd, [&] { cmd_tag(tag_obj_flag, tag_args); }},
      {rm_cmd, [&] { cmd_rm(rm_paths); }},
      {add_cmd, [&] { cmd_add(add_paths, add_all); }},
      {commit_cmd, [&] { cmd_commit(commit_msg); }},
      {status_cmd, [&] { cmd_status(); }},
      {diff_cmd, [&] { cmd_diff(); }},
      {branch_cmd, [&] { cmd_branch(branch_name); }},
      {switch_cmd, [&] { cmd_switch(switch_name); }},
      {config_cmd,
       [&] { cmd_config(config_global, config_key, config_value); }},
      {push_cmd, [&] { cmd_push(remote_url, remote_user, remote_pass); }},
  };

  try {
    auto it = dispatch.find(app.get_subcommands().front());
    if (it != dispatch.end())
      it->second();
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
