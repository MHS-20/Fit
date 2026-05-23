#include "Commands.hpp"
#include "Config.hpp"
#include "Ignore.hpp"
#include "Index.hpp"
#include "Objects.hpp"
#include "Refs.hpp"
#include "Remote.hpp"
#include "Repository.hpp"
#include <fstream>
#include <iostream>
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

void cmd_log(const std::string &commit) {
  auto repo = repo_find();
  std::string sha = object_find(repo, commit, "commit");
  while (!sha.empty()) {
    auto obj = object_read(repo, sha);
    auto c = std::dynamic_pointer_cast<FitCommit>(obj);
    std::cout << "commit " << sha << "\n";
    if (c->kvlm.count("author"))
      std::cout << "Author: " << c->kvlm["author"][0] << "\n";
    std::cout << "\n    " << (c->kvlm.count("") ? c->kvlm[""][0] : "") << "\n";
    sha = c->kvlm.count("parent") ? c->kvlm["parent"][0] : "";
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

void cmd_checkout(const std::string &commit, const std::string &path) {
  auto repo = repo_find();
  fs::path dest(path);
  if (fs::exists(dest) && !fs::is_empty(dest))
    throw std::runtime_error("Directory not empty");
  std::string sha = object_find(repo, commit, "commit");
  auto c = std::dynamic_pointer_cast<FitCommit>(object_read(repo, sha));
  std::string tree_sha = c->kvlm["tree"][0];
  auto t = std::dynamic_pointer_cast<FitTree>(object_read(repo, tree_sha));
  for (auto &item : t->items) {
    auto b = std::dynamic_pointer_cast<FitBlob>(object_read(repo, item.sha));
    std::ofstream out(dest / item.path, std::ios::binary);
    out.write(reinterpret_cast<char *>(b->blobdata.data()), b->blobdata.size());
  }
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

void cmd_add(const std::vector<std::string> &paths) {
  auto repo = repo_find();
  FitIndex idx = index_read(repo);
  for (auto &p : paths) {
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
    idx.entries.push_back(e);
  }
  index_write(repo, idx);
}

void cmd_commit(const std::string &message) {
  auto repo = repo_find();
  FitIndex idx = index_read(repo);
  FitTree tree;
  for (auto &e : idx.entries) {
    tree.items.push_back({"100644", e.name, e.sha});
  }
  std::string tree_sha = object_write(tree, repo);
  FitCommit commit;
  commit.kvlm["tree"].push_back(tree_sha);
  std::string parent = ref_resolve(repo, "HEAD");
  if (!parent.empty())
    commit.kvlm["parent"].push_back(parent);
  std::string identity = fitconfig_user_get(fitconfig_read());
  if (identity.empty())
    throw std::runtime_error(
        "User identity not configured in configuration context.");
  commit.kvlm["author"].push_back(identity);
  commit.kvlm["committer"].push_back(identity);
  commit.kvlm[""].push_back(message + "\n");
  std::string commit_sha = object_write(commit, repo);
  std::ofstream h(repo_file(repo, "refs/heads/master", true));
  h << commit_sha << "\n";
}

void cmd_status() {
  auto repo = repo_find();
  FitIndex idx = index_read(repo);
  FitIgnore rules = fitignore_read(repo);
  std::cout << "On branch master\n\nChanges to be committed:\n";
  for (auto &e : idx.entries) {
    if (!fs::exists(repo.worktree / e.name)) {
      std::cout << "\tdeleted:    " << e.name << "\n";
    } else {
      struct stat st;
      stat((repo.worktree / e.name).string().c_str(), &st);
      if (static_cast<uint32_t>(st.st_mtime) != e.mtime.first) {
        std::cout << "\tmodified:   " << e.name << "\n";
      }
    }
  }
  std::cout << "\nUntracked files:\n";
  for (auto &entry : fs::recursive_directory_iterator(repo.worktree)) {
    if (entry.is_regular_file()) {
      std::string rel = fs::relative(entry.path(), repo.worktree).string();
      if (rel.rfind(".fit", 0) == 0)
        continue;
      bool tracked = false;
      for (auto &e : idx.entries) {
        if (e.name == rel)
          tracked = true;
      }
      if (!tracked && !check_ignore(rules, rel)) {
        std::cout << "\t" << rel << "\n";
      }
    }
  }
}

void cmd_push(const std::string &remote_url, const std::string &username,
              const std::string &password) {
  auto repo = repo_find();
  push(repo, remote_url, username, password);
}
