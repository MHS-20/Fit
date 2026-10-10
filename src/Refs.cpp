#include "Refs.hpp"
#include "Utils.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

// Same limit as git: deeper chains are almost certainly a cycle.
static constexpr int kMaxSymrefDepth = 5;

static std::string ref_resolve_depth(const FitRepository &repo,
                                     const std::string &ref, int depth) {
  fs::path path = repo_file(repo, ref, false);
  if (path.empty() || !fs::is_regular_file(path)) {
    return "";
  }

  std::ifstream fp(path);
  if (!fp)
    return "";

  std::string data;
  std::getline(fp, data);

  // Trim trailing carriage returns or newlines if present
  if (!data.empty() && data.back() == '\r')
    data.pop_back();

  if (data.rfind("ref: ", 0) != 0)
    return data;
  if (depth >= kMaxSymrefDepth)
    throw std::runtime_error("Symbolic ref loop or chain too deep at " + ref);
  return ref_resolve_depth(repo, data.substr(5), depth + 1);
}

std::string ref_resolve(const FitRepository &repo, const std::string &ref) {
  return ref_resolve_depth(repo, ref, 0);
}

std::string ref_head_target(const FitRepository &repo) {
  std::ifstream head_f(repo_file(repo, "HEAD"));
  std::string line;
  std::getline(head_f, line);
  if (line.rfind("ref: ", 0) != 0)
    return "";
  line.erase(line.find_last_not_of(" \t\r\n") + 1);
  return line.substr(5);
}

RefMap ref_list(const FitRepository &repo, fs::path path) {
  if (path.empty()) {
    path = repo_dir(repo, "refs", false);
  }

  RefMap ret;
  if (path.empty() || !fs::exists(path) || !fs::is_directory(path)) {
    return ret;
  }

  // Collect elements and sort them alphabetically
  std::vector<fs::directory_entry> entries;
  for (const auto &entry : fs::directory_iterator(path)) {
    entries.push_back(entry);
  }
  std::sort(entries.begin(), entries.end(),
            [](const fs::directory_entry &a, const fs::directory_entry &b) {
              return a.path().filename().string() <
                     b.path().filename().string();
            });

  for (const auto &entry : entries) {
    std::string name = entry.path().filename().string();
    RefNode node;

    if (entry.is_directory()) {
      node.value = ref_list(repo, entry.path());
      ret[name] = node;
    } else {
      // It's a file ref, resolve it relative to the root .fit context
      fs::path rel_path = fs::relative(entry.path(), repo.fitdir);
      node.value = ref_resolve(repo, rel_path.string());
      ret[name] = node;
    }
  }
  return ret;
}

void ref_create(const FitRepository &repo, const std::string &ref_name,
                const std::string &sha) {
  write_file_atomic(repo_file(repo, fs::path("refs") / ref_name, true),
                    sha + "\n");
}
