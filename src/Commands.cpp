#include "Commands.hpp"
#include "Config.hpp"
#include "Ignore.hpp"
#include "Index.hpp"
#include "Objects.hpp"
#include "Refs.hpp"
#include "Remote.hpp"
#include "Repository.hpp"
#include "Utils.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static constexpr uint16_t kModeRegular = 0b1000;
static constexpr uint16_t kModeSymlink = 0b1010;
static constexpr uint16_t kModeGitlink = 0b1110;

// What git stores for a worktree path: a symlink's target, or a file's bytes.
static std::vector<uint8_t> worktree_blob(const fs::path &p) {
  if (fs::is_symlink(p)) {
    std::string target = fs::read_symlink(p).string();
    return {target.begin(), target.end()};
  }
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot read " + p.string());
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static std::string blob_sha(const std::vector<uint8_t> &data) {
  FitBlob b;
  b.blobdata = data;
  return object_sha(b);
}

// Tree mode for an index entry; like git, files are only ever 644 or 755.
static std::string tree_mode(const FitIndexEntry &e) {
  if (e.mode_type == kModeSymlink)
    return "120000";
  if (e.mode_type == kModeGitlink)
    return "160000";
  return (e.mode_perms & 0111) ? "100755" : "100644";
}

// "<unix-seconds> <+hhmm>" for now, in the local timezone, as git records it.
static std::string now_timestamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  long offset = tm.tm_gmtoff / 60;
  char tz[8];
  std::snprintf(tz, sizeof(tz), "%c%02ld%02ld", offset < 0 ? '-' : '+',
                std::labs(offset) / 60, std::labs(offset) % 60);
  return std::to_string(t) + " " + tz;
}

// The configured "Name <email>" followed by the current timestamp.
static std::string signature_now() {
  std::string identity = fitconfig_user_get(fitconfig_read());
  if (identity.empty())
    throw std::runtime_error(
        "User identity not configured in configuration context.");
  return identity + " " + now_timestamp();
}

static void print_tree(const FitRepository &repo, const std::string &sha,
                       bool recursive, const std::string &prefix) {
  auto obj = std::dynamic_pointer_cast<FitTree>(object_read(repo, sha));
  if (!obj)
    throw std::runtime_error("Not a tree: " + sha);
  for (auto &item : obj->items) {
    std::string path = prefix + item.path;
    if (recursive && item.mode.rfind("04", 0) == 0) {
      print_tree(repo, item.sha, true, path + "/");
      continue;
    }
    std::cout << item.mode << " " << item.sha << "\t" << path << "\n";
  }
}

void cmd_init(const std::string &path) { repo_create(path); }

void cmd_cat_file(const std::string &type, const std::string &object) {
  auto repo = repo_find();
  std::string sha = object_find(repo, object, type);
  auto obj = sha.empty() ? nullptr : object_read(repo, sha);
  if (!obj)
    throw std::runtime_error("Not a valid object name " + object);
  if (obj->get_fmt() == "tree") { // binary on disk; print it like ls-tree
    print_tree(repo, sha, false, "");
    return;
  }
  std::vector<uint8_t> data = obj->serialize();
  std::cout.write(reinterpret_cast<char *>(data.data()), data.size());
}

void cmd_hash_object(const std::string &type, bool write,
                     const std::string &path) {
  auto repo = repo_find();
  std::ifstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot open file");
  std::cout << object_hash(f, type, repo, write) << "\n";
}

std::vector<std::string> log_walk(const FitRepository &repo,
                                  const std::string &start) {
  std::vector<std::string> out;
  std::string sha = object_find(repo, start, "commit");
  while (!sha.empty()) {
    auto c = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));
    if (!c)
      throw std::runtime_error("Missing commit " + sha);
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
  if (sha.empty())
    throw std::runtime_error("Not a valid object name " + tree);
  print_tree(repo, sha, recursive, "");
}

void checkout_tree(const FitRepository &repo, const std::string &tree_sha,
                   const fs::path &dest) {
  if (fs::exists(dest) && !fs::is_empty(dest))
    throw std::runtime_error("Directory not empty");
  fs::create_directories(dest);

  std::function<void(const std::string &, const fs::path &)> walk;
  walk = [&](const std::string &t_sha, const fs::path &base) {
    auto t = std::dynamic_pointer_cast<FitTree>(object_read(repo, t_sha));
    if (!t)
      throw std::runtime_error("Missing tree " + t_sha);
    for (auto &item : t->items) {
      fs::path target = base / item.path;
      if (item.mode.rfind("04", 0) == 0) { // tree
        fs::create_directories(target);
        walk(item.sha, target);
        continue;
      }
      if (item.mode == "160000") { // submodule: commit lives in another repo
        fs::create_directories(target);
        continue;
      }
      auto b = std::dynamic_pointer_cast<FitBlob>(object_read(repo, item.sha));
      if (!b)
        throw std::runtime_error("Missing blob " + item.sha);
      if (item.mode == "120000") {
        fs::create_symlink(std::string(b->blobdata.begin(), b->blobdata.end()),
                           target);
        continue;
      }
      {
        std::ofstream out(target, std::ios::binary);
        out.write(reinterpret_cast<char *>(b->blobdata.data()),
                  b->blobdata.size());
        if (!out)
          throw std::runtime_error("Could not write " + target.string());
      }
      if (item.mode == "100755")
        fs::permissions(target,
                        fs::perms::owner_exec | fs::perms::group_exec |
                            fs::perms::others_exec,
                        fs::perm_options::add);
    }
  };

  walk(tree_sha, dest);
}

void cmd_checkout(const std::string &commit, const std::string &path) {
  auto repo = repo_find();
  std::string sha = object_find(repo, commit, "commit");
  if (sha.empty())
    throw std::runtime_error("Not a valid commit " + commit);
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

void cmd_tag(bool create_tag_object, const std::vector<std::string> &args,
             const std::string &message) {
  auto repo = repo_find();
  if (args.empty()) {
    if (fs::exists(repo.fitdir / "refs/tags")) {
      for (auto &p : fs::directory_iterator(repo.fitdir / "refs/tags")) {
        std::cout << p.path().filename().string() << "\n";
      }
    }
    return;
  }
  std::string target = object_find(repo, args.size() > 1 ? args[1] : "HEAD");
  if (target.empty())
    throw std::runtime_error("Nothing to tag: " +
                             (args.size() > 1 ? args[1] : "HEAD"));
  if (create_tag_object) {
    if (message.empty())
      throw std::runtime_error("Annotated tags need a message (-m)");
    FitTag tag;
    tag.kvlm["object"].push_back(target);
    tag.kvlm["type"].push_back(object_read(repo, target)->get_fmt());
    tag.kvlm["tag"].push_back(args[0]);
    tag.kvlm["tagger"].push_back(signature_now());
    tag.kvlm[""].push_back(message + "\n");
    target = object_write(tag, repo);
  }
  ref_create(repo, "tags/" + args[0], target);
}

void cmd_rm(const std::vector<std::string> &paths) {
  auto repo = repo_find();
  FitIndex idx = index_read(repo);
  std::set<std::string> names;
  for (auto &p : paths) {
    fs::path rel = fs::absolute(p).lexically_normal().lexically_relative(
        repo.worktree.lexically_normal());
    if (rel.empty() || *rel.begin() == "..")
      throw std::runtime_error(p + " is outside the worktree");
    bool tracked = std::any_of(idx.entries.begin(), idx.entries.end(),
                               [&](auto &e) { return e.name == rel.string(); });
    if (!tracked)
      throw std::runtime_error(p + " is not tracked");
    names.insert(rel.string());
  }
  std::erase_if(idx.entries, [&](auto &e) { return names.count(e.name) > 0; });
  index_write(repo, idx);
  for (auto &name : names)
    fs::remove(repo.worktree / name);
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
      if (!entry.is_regular_file() && !entry.is_symlink())
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
    struct stat st;
    if (lstat(ap.string().c_str(), &st) != 0)
      throw std::runtime_error("pathspec '" + p + "' did not match any files");
    if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode))
      throw std::runtime_error(p + " is not a regular file or symlink");
    FitBlob blob;
    blob.blobdata = worktree_blob(ap);
    FitIndexEntry e;
    e.name = fs::relative(ap, repo.worktree).string();
    e.sha = object_write(blob, repo);
    e.ctime = {static_cast<uint32_t>(st.st_ctim.tv_sec),
               static_cast<uint32_t>(st.st_ctim.tv_nsec)};
    e.mtime = {static_cast<uint32_t>(st.st_mtim.tv_sec),
               static_cast<uint32_t>(st.st_mtim.tv_nsec)};
    e.dev = st.st_dev;
    e.ino = st.st_ino;
    e.mode_type = S_ISLNK(st.st_mode) ? kModeSymlink : kModeRegular;
    e.mode_perms = S_ISLNK(st.st_mode)      ? 0
                   : (st.st_mode & S_IXUSR) ? 0755
                                            : 0644;
    e.uid = st.st_uid;
    e.gid = st.st_gid;
    e.fsize = static_cast<uint32_t>(st.st_size);
    std::erase_if(idx.entries,
                  [&](auto &entry) { return entry.name == e.name; });
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
      tree.items.push_back({tree_mode(e), name, e.sha});
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

  std::string parent = ref_resolve(repo, "HEAD");
  std::string parent_tree;
  if (!parent.empty()) {
    auto p = std::dynamic_pointer_cast<FitCommit>(object_read(repo, parent));
    if (!p)
      throw std::runtime_error("HEAD points at missing commit " + parent);
    parent_tree = p->kvlm["tree"][0];
  }
  if (tree_sha == parent_tree || (parent.empty() && idx.entries.empty()))
    throw std::runtime_error("Nothing to commit");

  FitCommit commit;
  commit.kvlm["tree"].push_back(tree_sha);
  if (!parent.empty())
    commit.kvlm["parent"].push_back(parent);
  commit.kvlm["author"].push_back(identity);
  commit.kvlm["committer"].push_back(identity);
  commit.kvlm[""].push_back(message + "\n");
  std::string commit_sha = object_write(commit, repo);

  // Move the branch HEAD names, or HEAD itself when detached.
  std::string branch_ref = ref_head_target(repo);
  write_file_atomic(
      repo_file(repo, branch_ref.empty() ? "HEAD" : branch_ref, true),
      commit_sha + "\n");
  return commit_sha;
}

void cmd_commit(const std::string &message) {
  auto repo = repo_find();
  std::cout << "commit " << cmd_commit(repo, message, signature_now()) << "\n";
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
  std::set<std::string> in_index;
  for (auto &e : idx.entries) {
    in_index.insert(e.name);
    auto it = head_tree.find(e.name);
    if (it == head_tree.end())
      report.staged_new.push_back(e.name);
    else if (it->second != e.sha)
      report.staged_modified.push_back(e.name);
  }
  for (auto &[path, sha] : head_tree)
    if (!in_index.count(path))
      report.staged_deleted.push_back(path);

  // Changes not staged: compare working tree vs index. Matching stat data
  // (mtime to the nanosecond, and size) means unchanged without reading the
  // file; otherwise the content is re-hashed, so a touched-but-identical
  // file is not reported.
  for (auto &e : idx.entries) {
    fs::path p = repo.worktree / e.name;
    struct stat st;
    if (lstat(p.string().c_str(), &st) != 0) {
      report.unstaged_deleted.push_back(e.name);
      continue;
    }
    // ponytail: an edit that keeps size and mtime identical to the
    // nanosecond is missed (git's "racy" case); re-hash always to close it.
    bool stat_same =
        static_cast<uint32_t>(st.st_mtim.tv_sec) == e.mtime.sec &&
        static_cast<uint32_t>(st.st_mtim.tv_nsec) == e.mtime.nsec &&
        static_cast<uint32_t>(st.st_size) == e.fsize;
    if (!stat_same && blob_sha(worktree_blob(p)) != e.sha)
      report.unstaged_modified.push_back(e.name);
  }
  return report;
}

void cmd_status() {
  auto repo = repo_find();
  FitIndex idx = index_read(repo);
  FitIgnore rules = fitignore_read(repo);

  std::string branch_name = ref_head_target(repo);
  if (branch_name.empty())
    branch_name = "HEAD (detached)";
  else if (branch_name.rfind("refs/heads/", 0) == 0)
    branch_name = branch_name.substr(11);

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
  std::set<std::string> tracked;
  for (auto &e : idx.entries)
    tracked.insert(e.name);
  std::cout << "\nUntracked files:\n";
  for (auto it = fs::recursive_directory_iterator(repo.worktree);
       it != fs::recursive_directory_iterator(); ++it) {
    auto rel = it->path().lexically_relative(repo.worktree);
    if (it->is_directory() && (rel == ".fit" || rel == ".git")) {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file() && !it->is_symlink())
      continue;
    if (!tracked.count(rel.string()) && !check_ignore(rules, rel.string()))
      std::cout << "\t" << rel.string() << "\n";
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

std::vector<std::string> split_diff_lines(const std::string &text) {
  std::vector<std::string> lines;
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line))
    lines.push_back(line);
  if (!text.empty() && text.back() != '\n')
    lines.back() += "\n\\ No newline at end of file";
  return lines;
}

void cmd_diff() {
  auto repo = repo_find();
  FitIndex idx = index_read(repo);

  for (auto &e : idx.entries) {
    fs::path abs_path = repo.worktree / e.name;
    if (!fs::exists(fs::symlink_status(abs_path))) {
      std::cout << "deleted: " << e.name << "\n";
      continue;
    }

    std::vector<uint8_t> live = worktree_blob(abs_path);
    if (blob_sha(live) == e.sha)
      continue;

    std::cout << "diff --fit a/" << e.name << " b/" << e.name << "\n";
    std::cout << "--- a/" << e.name << "\n";
    std::cout << "+++ b/" << e.name << "\n";

    auto index_obj =
        std::dynamic_pointer_cast<FitBlob>(object_read(repo, e.sha));
    if (!index_obj)
      throw std::runtime_error("Missing blob " + e.sha);
    std::string old_text(index_obj->blobdata.begin(),
                         index_obj->blobdata.end());
    std::string new_text(live.begin(), live.end());

    for (auto &l :
         diff_lines(split_diff_lines(old_text), split_diff_lines(new_text)))
      std::cout << l.tag << l.text << "\n";
  }
}

void cmd_branch(const std::string &name) {
  auto repo = repo_find();
  if (name.empty()) {
    std::string current = ref_head_target(repo);
    if (current.rfind("refs/heads/", 0) == 0)
      current = current.substr(11);
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
  write_file_atomic(repo_file(repo, "HEAD"), "ref: refs/heads/" + name + "\n");
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

// Reads one line from the terminal after `label`; with echo off for secrets.
static std::string prompt(const std::string &label, bool secret) {
  std::cerr << label << std::flush;
  termios saved{};
  bool tty = secret && tcgetattr(STDIN_FILENO, &saved) == 0;
  if (tty) {
    termios quiet = saved;
    quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
  }
  std::string line;
  std::getline(std::cin, line);
  if (tty) {
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    std::cerr << "\n";
  }
  return line;
}

void cmd_push(const std::string &remote_url, const std::string &username,
              const std::string &password) {
  auto repo = repo_find();
  std::string user = username.empty() ? prompt("Username: ", false) : username;
  std::string pass =
      password.empty() ? prompt("Password/token: ", true) : password;
  push(repo, remote_url, user, pass);
}

void cmd_rev_parse(const std::string &type, const std::string &name) {
  auto repo = repo_find();
  std::string sha = object_find(repo, name, type);
  if (sha.empty())
    throw std::runtime_error("Not a valid object name " + name);
  std::cout << sha << "\n";
}

void cmd_ls_files(bool verbose) {
  auto repo = repo_find();
  for (auto &e : index_read(repo).entries) {
    std::cout << e.name << "\n";
    if (!verbose)
      continue;
    std::cout << "  " << tree_mode(e) << " " << e.sha << "\n"
              << "  ctime: " << e.ctime.sec << ":" << e.ctime.nsec
              << "  mtime: " << e.mtime.sec << ":" << e.mtime.nsec << "\n"
              << "  dev: " << e.dev << "  ino: " << e.ino << "\n"
              << "  uid: " << e.uid << "  gid: " << e.gid
              << "  size: " << e.fsize << "\n"
              << "  flags: assume-valid=" << e.flag_assume_valid
              << " stage=" << e.flag_stage << "\n";
  }
}

void cmd_check_ignore(const std::vector<std::string> &paths) {
  auto repo = repo_find();
  FitIgnore rules = fitignore_read(repo);
  for (auto &p : paths)
    if (check_ignore(rules, p))
      std::cout << p << "\n";
}
