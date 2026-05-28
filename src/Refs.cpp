#include "Refs.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

std::string ref_resolve(const FitRepository &repo, const std::string &ref) {
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

  if (data.rfind("ref: ", 0) == 0) {
    return ref_resolve(repo, data.substr(5));
  } else {
    return data;
  }
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
  fs::path target = repo_file(repo, fs::path("refs") / ref_name, true);
  std::ofstream fp(target);
  if (!fp)
    throw std::runtime_error("Could not create reference file: " +
                             target.string());
  fp << sha << "\n";
}
