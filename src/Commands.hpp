#pragma once
#include "Index.hpp"
#include "Repository.hpp"
#include <map>
#include <string>
#include <vector>

// Library operations behind the commands below; they return results instead
// of printing, so they can be tested directly.

// Writes a tree object for every directory under `prefix` (bottom-up) and
// returns the SHA of the tree for `prefix` itself ("" = repository root).
std::string write_tree_from_entries(const FitRepository &repo,
                                    const std::vector<FitIndexEntry> &entries,
                                    const std::string &prefix);

// Commits the index with `identity` as author and committer, moves HEAD's
// branch (or a detached HEAD) to the new commit, and returns its SHA.
std::string cmd_commit(const FitRepository &repo, const std::string &message,
                       const std::string &identity);

// SHAs from `start` back to the root commit, following first parents.
std::vector<std::string> log_walk(const FitRepository &repo,
                                  const std::string &start);

// Flattens a tree into worktree-relative path -> blob SHA.
std::map<std::string, std::string> flatten_tree(const FitRepository &repo,
                                                const std::string &tree_sha);

struct StatusReport {
  std::vector<std::string> staged_new, staged_modified, staged_deleted;
  std::vector<std::string> unstaged_modified, unstaged_deleted;
};
// Staged: index vs. HEAD's tree. Unstaged: worktree vs. index.
StatusReport compute_status(const FitRepository &repo);

struct DiffLine {
  char tag; // ' ' unchanged, '-' only in old, '+' only in new
  std::string text;
};
// Two-cursor line diff: complete but not minimal (no lookahead).
std::vector<DiffLine> diff_lines(const std::vector<std::string> &old_lines,
                                 const std::vector<std::string> &new_lines);

// Writes the tree `tree_sha` under `dest`, which must be empty or missing.
void checkout_tree(const FitRepository &repo, const std::string &tree_sha,
                   const fs::path &dest);

void cmd_init(const std::string &path);
void cmd_cat_file(const std::string &type, const std::string &object);
void cmd_hash_object(const std::string &type, bool write,
                     const std::string &path);
void cmd_log(const std::string &commit);
void cmd_ls_tree(bool recursive, const std::string &tree);
void cmd_checkout(const std::string &commit, const std::string &path);
void cmd_show_ref();
void cmd_tag(bool create_tag_object, const std::vector<std::string> &args);
void cmd_rm(const std::vector<std::string> &paths);
void cmd_add(const std::vector<std::string> &paths, bool all = false);
void cmd_commit(const std::string &message);
void cmd_status();
void cmd_diff();
void cmd_branch(const std::string &name);
void cmd_switch(const std::string &name);
void cmd_config(bool global_flag, const std::string &key,
                const std::string &value);
void cmd_push(const std::string &remote_url, const std::string &username,
              const std::string &password);
