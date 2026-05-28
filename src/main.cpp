#include "Commands.hpp"
#include <CLI/CLI.hpp>
#include <iostream>

int main(int argc, char **argv) {
  CLI::App app{"Fit version control system"};
  app.require_subcommand(1);

  auto init_cmd = app.add_subcommand("init", "Initialize empty repo");
  std::string init_path = ".";
  init_cmd->add_option("path", init_path);

  auto cat_cmd = app.add_subcommand("cat-file", "Provide content of objects");
  std::string cat_type, cat_obj;
  cat_cmd->add_option("type", cat_type)->required();
  cat_cmd->add_option("object", cat_obj)->required();

  auto hash_cmd = app.add_subcommand("hash-object", "Compute object ID");
  std::string hash_type = "blob", hash_path;
  bool hash_write = false;
  hash_cmd->add_option("-t", hash_type);
  hash_cmd->add_flag("-w", hash_write);
  hash_cmd->add_option("path", hash_path)->required();

  auto log_cmd = app.add_subcommand("log", "Display commit history");
  std::string log_commit = "HEAD";
  log_cmd->add_option("commit", log_commit);

  auto ls_cmd = app.add_subcommand("ls-tree", "Print tree objects");
  bool ls_recurse = false;
  std::string ls_tree_obj;
  ls_cmd->add_flag("-r", ls_recurse);
  ls_cmd->add_option("tree", ls_tree_obj)->required();

  auto checkout_cmd = app.add_subcommand("checkout", "Checkout working tree");
  std::string co_commit, co_path;
  checkout_cmd->add_option("commit", co_commit)->required();
  checkout_cmd->add_option("path", co_path)->required();

  auto show_ref_cmd = app.add_subcommand("show-ref", "List references");

  auto tag_cmd = app.add_subcommand("tag", "Handle tagging operations");
  bool tag_obj_flag = false;
  std::vector<std::string> tag_args;
  tag_cmd->add_flag("-a", tag_obj_flag);
  tag_cmd->add_option("args", tag_args);

  auto rm_cmd = app.add_subcommand("rm", "Remove paths");
  std::vector<std::string> rm_paths;
  rm_cmd->add_option("paths", rm_paths)->required();

  auto add_cmd = app.add_subcommand("add", "Add files to index");
  bool add_all = false;
  std::vector<std::string> add_paths;
  add_cmd->add_flag("-A,--all", add_all);
  add_cmd->add_option("paths", add_paths);

  auto commit_cmd = app.add_subcommand("commit", "Create new commit");
  std::string commit_msg;
  commit_cmd->add_option("-m", commit_msg)->required();

  auto status_cmd = app.add_subcommand("status", "Show tracking changes state");

  auto push_cmd =
      app.add_subcommand("push", "Push changes to remote network server");
  std::string remote_url, user, pass;
  push_cmd->add_option("url", remote_url)->required();
  push_cmd->add_option("-u", user);
  push_cmd->add_option("-p", pass);

  CLI11_PARSE(app, argc, argv);

  try {
    if (*init_cmd)
      cmd_init(init_path);
    else if (*cat_cmd)
      cmd_cat_file(cat_type, cat_obj);
    else if (*hash_cmd)
      cmd_hash_object(hash_type, hash_write, hash_path);
    else if (*log_cmd)
      cmd_log(log_commit);
    else if (*ls_cmd)
      cmd_ls_tree(ls_recurse, ls_tree_obj);
    else if (*checkout_cmd)
      cmd_checkout(co_commit, co_path);
    else if (*show_ref_cmd)
      cmd_show_ref();
    else if (*tag_cmd)
      cmd_tag(tag_obj_flag, tag_args);
    else if (*rm_cmd)
      cmd_rm(rm_paths);
    else if (*add_cmd)
      cmd_add(add_paths, add_all);
    else if (*commit_cmd)
      cmd_commit(commit_msg);
    else if (*status_cmd)
      cmd_status();
    else if (*push_cmd)
      cmd_push(remote_url, user, pass);
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
