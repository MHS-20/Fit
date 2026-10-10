#include "Commands.hpp"
#include "Config.hpp"
#include "Ignore.hpp"
#include "Index.hpp"
#include "Objects.hpp"
#include "Refs.hpp"
#include "Remote.hpp"
#include "Repository.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

void cmd_init(const std::string &path) { repo_create(path); }

void cmd_cat_file(const std::string &type, const std::string &object) {
  auto repo = repo_find();
  std::string sha = object_find(repo, object, type);
  auto obj = object_read(repo, sha);
  if (obj && obj->get_fmt() == "blob") {
    auto b = std::dynamic_pointer_cast<FitBlob>(obj);
    std::cout.write(reinterpret_cast<char *>(b->blobdata.data()),
                    b->blobdata.size());
  }
}

void cmd_hash_object(const std::string &type, bool write,
                     const std::string &path) {
  auto repo = repo_find();
  std::ifstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot open file");
  std::string sha = object_hash(f, type, repo);
  std::cout << sha << "\n";
}

std::vector<std::string> log_walk(const FitRepository &repo,
                                  const std::string &start) {
  std::vector<std::string> out;
  std::string sha = object_find(repo, start, "commit");
  while (!sha.empty()) {
    auto c = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));
    out.push_back(sha);
    sha = c->kvlm.count("parent") ? c->kvlm["parent"][0] : "";
  }
  return out;
}

void cmd_log(const std::string &commit) {
  auto repo = repo_find();
  for (auto &sha : log_walk(repo, commit)) {
    auto c = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));
    std::cout << "commit " << sha << "\n";
    if (c->kvlm.count("author"))
      std::cout << "Author: " << c->kvlm["author"][0] << "\n";
    std::cout << "\n    " << (c->kvlm.count("") ? c->kvlm[""][0] : "") << "\n";
  }
}

void cmd_ls_tree(bool recursive, const std::string &tree) {
  auto repo = repo_find();
  std::string sha = object_find(repo, tree, "tree");
  auto obj = std::dynamic_pointer_cast<FitTree>(object_read(repo, sha));
  for (auto &item : obj->items) {
    std::cout << item.mode << " " << item.sha << "\t" << item.path << "\n";
  }
}

void checkout_tree(const FitRepository &repo, const std::string &tree_sha,
                   const fs::path &dest) {
  if (fs::exists(dest) && !fs::is_empty(dest))
    throw std::runtime_error("Directory not empty");

  std::function<void(const std::string &, const fs::path &)> walk;
  walk = [&](const std::string &t_sha, const fs::path &base) {
    auto t = std::dynamic_pointer_cast<FitTree>(object_read(repo, t_sha));
    for (auto &item : t->items) {
      fs::path target = base / item.path;
      if (item.mode.rfind("04", 0) == 0) { // tree
        fs::create_directories(target);
        walk(item.sha, target);
      } else {
        auto b =
            std::dynamic_pointer_cast<FitBlob>(object_read(repo, item.sha));
        std::ofstream out(target, std::ios::binary);
        out.write(reinterpret_cast<char *>(b->blobdata.data()),
                  b->blobdata.size());
      }
    }
  };

  walk(tree_sha, dest);
}

void cmd_checkout(const std::string &commit, const std::string &path) {
  auto repo = repo_find();
  std::string sha = object_find(repo, commit, "commit");
  auto c = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));
  checkout_tree(repo, c->kvlm["tree"][0], path);
}

void show_ref_internal(const RefMap &refs, bool with_hash,
                       const std::string &prefix) {
  std::string current_prefix = prefix.empty() ? "" : prefix + "/";
  for (const auto &[k, node] : refs) {
    if (std::holds_alternative<std::string>(node.value)) {
      std::string sha = std::get<std::string>(node.value);
      if (with_hash) {
        std::cout << sha << " " << current_prefix << k << "\n";
      } else {
        std::cout << current_prefix << k << "\n";
      }
    } else {
      const auto &nested_map =
          std::get<std::map<std::string, RefNode>>(node.value);
      show_ref_internal(nested_map, with_hash, current_prefix + k);
    }
  }
}

void cmd_show_ref() {
  auto repo = repo_find();
  RefMap refs = ref_list(repo);
  show_ref_internal(refs, true, "refs");
}

void cmd_tag(bool create_tag_object, const std::vector<std::string> &args) {
  auto repo = repo_find();
  if (args.empty()) {
    if (fs::exists(repo.fitdir / "refs/tags")) {
      for (auto &p : fs::directory_iterator(repo.fitdir / "refs/tags")) {
        std::cout << p.path().filename().string() << "\n";
      }
    }
  } else {
    ref_create(repo, "tags/" + args[0],
               object_find(repo, args.size() > 1 ? args[1] : "HEAD"));
  }
}

void cmd_rm(const std::vector<std::string> &paths) {
  auto repo = repo_find();
  FitIndex idx = index_read(repo);
  std::vector<FitIndexEntry> kept;
  for (auto &e : idx.entries) {
    bool remove = false;
    for (auto &p : paths) {
      if (e.name == p) {
        remove = true;
        fs::remove(fs::path(repo.worktree) / p);
        break;
      }
    }
    if (!remove)
      kept.push_back(e);
  }
  idx.entries = kept;
  index_write(repo, idx);
}

std::vector<std::string> collect_all_files(const FitRepository &repo) {
  std::vector<std::string> paths;
  for (const auto &entry : fs::recursive_directory_iterator(repo.worktree)) {
    if (!entry.is_regular_file())
      continue;
    const fs::path &p = entry.path();
    // skip .fit/ and .git/
    for (const auto &part : p.lexically_relative(repo.worktree)) {
      if (part == ".fit" || part == ".git")
        goto next;
    }
    paths.push_back(p.string());
  next:;
  }
  return paths;
}

void cmd_add(const std::vector<std::string> &paths, bool all) {
  auto repo = repo_find();

  std::vector<std::string> resolved_paths = paths;
  if (all) {
    FitIgnore ignore = fitignore_read(repo);
    for (const auto &entry : fs::recursive_directory_iterator(repo.worktree)) {
      if (!entry.is_regular_file())
        continue;
      auto rel = entry.path().lexically_relative(repo.worktree);
      if (*rel.begin() == ".fit" || *rel.begin() == ".git")
        continue;
      std::string rel_str = rel.string();
      if (check_ignore(ignore, rel_str))
        continue;
      resolved_paths.push_back(entry.path().string());
    }
  }
  if (resolved_paths.empty()) {
    std::cerr << "Nothing specified, nothing added.\n";
    return;
  }

  FitIndex idx = index_read(repo);
  for (auto &p : resolved_paths) {
    fs::path ap = fs::absolute(p);
    std::ifstream f(ap, std::ios::binary);
    std::string sha = object_hash(f, "blob", repo);
    struct stat st;
    stat(ap.string().c_str(), &st);
    FitIndexEntry e;
    e.name = fs::relative(ap, repo.worktree).string();
    e.sha = sha;
    e.ctime = {static_cast<uint32_t>(st.st_ctime), 0};
    e.mtime = {static_cast<uint32_t>(st.st_mtime), 0};
    e.dev = st.st_dev;
    e.ino = st.st_ino;
    e.mode_type = 0b1000;
    e.mode_perms = st.st_mode & 0777;
    e.uid = st.st_uid;
    e.gid = st.st_gid;
    e.fsize = fs::file_size(ap);
    idx.entries.erase(std::remove_if(idx.entries.begin(), idx.entries.end(),
                                     [&](const FitIndexEntry &entry) {
                                       return entry.name == e.name;
                                     }),
                      idx.entries.end());
    idx.entries.push_back(e);
  }
  index_write(repo, idx);
}

std::string write_tree_from_entries(const FitRepository &repo,
                                    const std::vector<FitIndexEntry> &entries,
                                    const std::string &prefix) {
  FitTree tree;
  std::map<std::string, std::vector<FitIndexEntry>> subdirs;

  for (auto &e : entries) {
    std::string name = e.name;
    if (!prefix.empty()) {
      if (name.rfind(prefix + "/", 0) != 0)
        continue;
      name = name.substr(prefix.size() + 1);
    }
    auto slash = name.find('/');
    if (slash == std::string::npos) {
      // direct file in this directory
      tree.items.push_back({"100644", name, e.sha});
    } else {
      // belongs to a subdir
      std::string subdir = name.substr(0, slash);
      subdirs[subdir].push_back(e);
    }
  }

  // recursively build subtrees
  for (auto &[subdir, sub_entries] : subdirs) {
    std::string sub_prefix = prefix.empty() ? subdir : prefix + "/" + subdir;
    std::string sub_sha =
        write_tree_from_entries(repo, sub_entries, sub_prefix);
    tree.items.push_back({"040000", subdir, sub_sha});
  }

  return object_write(tree, repo);
}

std::string cmd_commit(const FitRepository &repo, const std::string &message,
                       const std::string &identity) {
  FitIndex idx = index_read(repo);

  std::string tree_sha = write_tree_from_entries(repo, idx.entries, "");

  FitCommit commit;
  commit.kvlm["tree"].push_back(tree_sha);
  std::string parent = ref_resolve(repo, "HEAD");
  if (!parent.empty())
    commit.kvlm["parent"].push_back(parent);
  commit.kvlm["author"].push_back(identity);
  commit.kvlm["committer"].push_back(identity);
  commit.kvlm[""].push_back(message + "\n");
  std::string commit_sha = object_write(commit, repo);

  std::ifstream head_f(repo_file(repo, "HEAD"));
  std::string head_line;
  std::getline(head_f, head_line);
  if (head_line.rfind("ref: ", 0) == 0) {
    std::string branch_ref = head_line.substr(5);
    branch_ref.erase(branch_ref.find_last_not_of(" \t\r\n") + 1);
    std::ofstream h(repo_file(repo, branch_ref, true));
    h << commit_sha << "\n";
  } else {
    std::ofstream h(repo_file(repo, "HEAD"));
    h << commit_sha << "\n";
  }
  return commit_sha;
}

void cmd_commit(const std::string &message) {
  auto repo = repo_find();
  std::string identity = fitconfig_user_get(fitconfig_read());
  if (identity.empty())
    throw std::runtime_error(
        "User identity not configured in configuration context.");
  std::cout << "commit " << cmd_commit(repo, message, identity) << "\n";
}

std::map<std::string, std::string> flatten_tree(const FitRepository &repo,
                                                const std::string &tree_sha) {
  std::map<std::string, std::string> out;
  std::function<void(const std::string &, const std::string &)> walk_tree;
  walk_tree = [&](const std::string &sha, const std::string &prefix) {
    auto obj = std::dynamic_pointer_cast<FitTree>(object_read(repo, sha));
    if (!obj)
      return;
    for (auto &item : obj->items) {
      std::string full = prefix.empty() ? item.path : prefix + "/" + item.path;
      if (item.mode.rfind("04", 0) == 0) {
        walk_tree(item.sha, full);
      } else {
        out[full] = item.sha;
      }
    }
  };
  walk_tree(tree_sha, "");
  return out;
}

StatusReport compute_status(const FitRepository &repo) {
  StatusReport report;
  FitIndex idx = index_read(repo);

  // Build a map of path -> sha from the HEAD commit tree
  std::map<std::string, std::string> head_tree;
  std::string head_sha = ref_resolve(repo, "HEAD");
  if (!head_sha.empty()) {
    auto head_commit =
        std::dynamic_pointer_cast<FitCommit>(object_read(repo, head_sha));
    if (head_commit && head_commit->kvlm.count("tree"))
      head_tree = flatten_tree(repo, head_commit->kvlm["tree"][0]);
  }

  // Changes to be committed: compare index vs HEAD
  for (auto &e : idx.entries) {
    if (head_tree.count(e.name) == 0) {
      report.staged_new.push_back(e.name);
    } else if (head_tree[e.name] != e.sha) {
      report.staged_modified.push_back(e.name);
    }
  }
  // Files in HEAD but not in index = deleted
  for (auto &[path, sha] : head_tree) {
    bool in_index = false;
    for (auto &e : idx.entries)
      if (e.name == path) {
        in_index = true;
        break;
      }
    if (!in_index)
      report.staged_deleted.push_back(path);
  }

  // Changes not staged: compare working tree vs index
  for (auto &e : idx.entries) {
    if (!fs::exists(repo.worktree / e.name)) {
      report.unstaged_deleted.push_back(e.name);
    } else {
      struct stat st;
      stat((repo.worktree / e.name).string().c_str(), &st);
      if (static_cast<uint32_t>(st.st_mtime) != e.mtime.sec) {
        report.unstaged_modified.push_back(e.name);
      }
    }
  }
  return report;
}

void cmd_status() {
  auto repo = repo_find();
  FitIndex idx = index_read(repo);
  FitIgnore rules = fitignore_read(repo);

  std::string branch_name = "HEAD (detached)";
  {
    std::ifstream head_f(repo_file(repo, "HEAD"));
    std::string head_line;
    std::getline(head_f, head_line);
    if (head_line.rfind("ref: refs/heads/", 0) == 0)
      branch_name = head_line.substr(16);
    else if (head_line.rfind("ref: ", 0) == 0)
      branch_name = head_line.substr(5);
    branch_name.erase(branch_name.find_last_not_of(" \t\r\n") + 1);
  }

  StatusReport report = compute_status(repo);
  std::cout << "On branch " << branch_name << "\n\nChanges to be committed:\n";
  for (auto &p : report.staged_new)
    std::cout << "\tnew file:   " << p << "\n";
  for (auto &p : report.staged_modified)
    std::cout << "\tmodified:   " << p << "\n";
  for (auto &p : report.staged_deleted)
    std::cout << "\tdeleted:    " << p << "\n";

  std::cout << "\nChanges not staged for commit:\n";
  for (auto &p : report.unstaged_deleted)
    std::cout << "\tdeleted:    " << p << "\n";
  for (auto &p : report.unstaged_modified)
    std::cout << "\tmodified:   " << p << "\n";

  // Untracked files
  std::cout << "\nUntracked files:\n";
  for (auto &entry : fs::recursive_directory_iterator(repo.worktree)) {
    if (entry.is_regular_file()) {
      std::string rel = fs::relative(entry.path(), repo.worktree).string();
      if (rel.rfind(".fit", 0) == 0)
        continue;
      bool tracked = false;
      for (auto &e : idx.entries) {
        if (e.name == rel) {
          tracked = true;
          break;
        }
      }
      if (!tracked && !check_ignore(rules, rel)) {
        std::cout << "\t" << rel << "\n";
      }
    }
  }
}
std::vector<DiffLine> diff_lines(const std::vector<std::string> &old_lines,
                                 const std::vector<std::string> &new_lines) {
  std::vector<DiffLine> out;
  size_t i = 0, j = 0;
  while (i < old_lines.size() || j < new_lines.size()) {
    if (i < old_lines.size() && j < new_lines.size() &&
        old_lines[i] == new_lines[j]) {
      out.push_back({' ', old_lines[i]});
      ++i;
      ++j;
    } else if (i < old_lines.size()) {
      out.push_back({'-', old_lines[i]});
      ++i;
    } else {
      out.push_back({'+', new_lines[j]});
      ++j;
    }
  }
  return out;
}

void cmd_diff() {
  auto repo = repo_find();
  FitIndex idx = index_read(repo);

  for (auto &e : idx.entries) {
    fs::path abs_path = repo.worktree / e.name;
    if (!fs::exists(abs_path)) {
      std::cout << "deleted: " << e.name << "\n";
      continue;
    }

    std::ifstream f(abs_path, std::ios::binary);
    std::string live_sha = object_hash(f, "blob", repo);
    if (live_sha == e.sha)
      continue;

    std::cout << "diff --fit a/" << e.name << " b/" << e.name << "\n";
    std::cout << "--- a/" << e.name << "\n";
    std::cout << "+++ b/" << e.name << "\n";

    auto index_obj =
        std::dynamic_pointer_cast<FitBlob>(object_read(repo, e.sha));
    std::string old_text(index_obj->blobdata.begin(),
                         index_obj->blobdata.end());

    std::ifstream live_f(abs_path, std::ios::binary);
    std::string new_text((std::istreambuf_iterator<char>(live_f)),
                         std::istreambuf_iterator<char>());

    auto split_lines = [](const std::string &s) {
      std::vector<std::string> lines;
      std::istringstream ss(s);
      std::string line;
      while (std::getline(ss, line))
        lines.push_back(line);
      return lines;
    };

    auto old_lines = split_lines(old_text);
    auto new_lines = split_lines(new_text);

    for (auto &l : diff_lines(old_lines, new_lines))
      std::cout << l.tag << l.text << "\n";
  }
}

void cmd_branch(const std::string &name) {
  auto repo = repo_find();
  if (name.empty()) {
    std::string current;
    {
      std::ifstream head_f(repo_file(repo, "HEAD"));
      std::string line;
      std::getline(head_f, line);
      if (line.rfind("ref: refs/heads/", 0) == 0)
        current = line.substr(16);
      current.erase(current.find_last_not_of(" \t\r\n") + 1);
    }
    fs::path heads_dir = repo.fitdir / "refs/heads";
    if (fs::exists(heads_dir)) {
      std::vector<std::string> branches;
      for (auto &p : fs::directory_iterator(heads_dir))
        branches.push_back(p.path().filename().string());
      std::sort(branches.begin(), branches.end());
      for (auto &b : branches)
        std::cout << (b == current ? "* " : "  ") << b << "\n";
    }
    return;
  }
  std::string head_sha = ref_resolve(repo, "HEAD");
  if (head_sha.empty())
    throw std::runtime_error("Cannot create branch: no commits yet");
  ref_create(repo, "heads/" + name, head_sha);
  std::cout << "Branch '" << name << "' created at " << head_sha.substr(0, 7)
            << "\n";
}

void cmd_switch(const std::string &name) {
  auto repo = repo_find();
  fs::path branch_ref = repo.fitdir / "refs/heads" / name;
  if (!fs::exists(branch_ref))
    throw std::runtime_error("Branch '" + name + "' not found");
  std::ofstream head(repo_file(repo, "HEAD"));
  head << "ref: refs/heads/" << name << "\n";
  std::cout << "Switched to branch '" << name << "'\n";
}

void cmd_config(bool global_flag, const std::string &key,
                const std::string &value) {
  auto dot = key.find('.');
  if (dot == std::string::npos)
    throw std::runtime_error(
        "Key must be in section.key format (e.g. user.name)");
  std::string section = key.substr(0, dot);
  std::string k = key.substr(dot + 1);

  fs::path config_path;
  if (global_flag) {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path home = std::getenv("HOME") ? std::getenv("HOME") : "";
    config_path =
        xdg ? fs::path(xdg) / "fit/config" : home / ".config/fit/config";
  } else {
    auto repo = repo_find();
    config_path = repo.fitdir / "config";
  }

  fitconfig_set(config_path, section, k, value);
  std::cout << "Set " << key << " = " << value << " in " << config_path.string()
            << "\n";
}

void cmd_push(const std::string &remote_url, const std::string &username,
              const std::string &password) {
  auto repo = repo_find();
  push(repo, remote_url, username, password);
}
