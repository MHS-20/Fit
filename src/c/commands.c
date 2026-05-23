#include "commands.h"
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <time.h>

static fit_repository_t *find_repo(void) { return repo_find(".", true); }

int cmd_init(const char *path) {
  fit_repository_t *repo = repo_create(path ? path : ".");
  if (!repo)
    return FIT_ERROR;
  printf("Initialized empty Fit repository in %s/.fit/\n", repo->worktree);
  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_cat_file(const char *type, const char *object) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  char *sha = object_find(repo, object, type, true);
  if (!sha) {
    fit_repository_free(repo);
    return FIT_ERROR;
  }

  fit_object_t *obj = object_read(repo, sha);
  free(sha);
  if (!obj) {
    fit_repository_free(repo);
    return FIT_ERROR;
  }

  if (obj->type == OBJ_BLOB) {
    fwrite(obj->blob.data, 1, obj->blob.size, stdout);
  } else if (obj->type == OBJ_COMMIT || obj->type == OBJ_TAG) {
    fwrite(obj->raw.data, 1, obj->raw.size, stdout);
  } else if (obj->type == OBJ_TREE) {
    for (size_t i = 0; i < obj->tree.count; i++) {
      tree_leaf_t *l = &obj->tree.items[i];
      printf("%s %s %s\t%s\n", l->mode, (l->mode[1] == '4') ? "tree" : "blob",
             l->sha, l->path);
    }
  }

  object_free(obj);
  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_hash_object(const char *type, bool write, const char *path) {
  fit_repository_t *repo = NULL;
  if (write) {
    repo = find_repo();
    if (!repo)
      return FIT_ERROR;
  }

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "Cannot open %s\n", path);
    return FIT_ERROR;
  }

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  unsigned char *data = malloc(fsize);
  fread(data, 1, fsize, fp);
  fclose(fp);

  fit_object_t obj;
  obj.type = obj_type_from_str(type);
  if (obj.type == OBJ_BLOB) {
    obj.blob.data = data;
    obj.blob.size = fsize;
  } else {
    obj.raw.data = data;
    obj.raw.size = fsize;
  }

  char *sha = object_write(&obj, repo);
  printf("%s\n", sha);
  free(sha);
  free(data);
  if (repo)
    fit_repository_free(repo);
  return FIT_OK;
}

static void log_graphviz(fit_repository_t *repo, const char *sha) {
  if (!sha)
    return;
  fit_object_t *obj = object_read(repo, sha);
  if (!obj)
    return;

  kvlm_t *kv = kvlm_parse(obj->raw.data, obj->raw.size);
  object_free(obj);

  kvlm_entry_t *e = kv->head;
  while (e) {
    if (strcmp(e->key, "parent") == 0) {
      printf("  c_%s -> c_%s;\n", sha, (char *)e->value);
      log_graphviz(repo, (char *)e->value);
    }
    e = (kvlm_entry_t *)e->next;
  }

  const char *msg = (const char *)kv->message;
  char short_msg[64] = "";
  if (msg) {
    strncpy(short_msg, msg, 63);
    char *nl = strchr(short_msg, '\n');
    if (nl)
      *nl = '\0';
  }

  printf("  c_%s [label=\"%s\"]\n", sha, short_msg);
  kvlm_free(kv);
}

int cmd_log(const char *commit) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  char *sha = object_find(repo, commit ? commit : "HEAD", "commit", true);
  if (!sha) {
    fit_repository_free(repo);
    return FIT_ERROR;
  }

  printf("digraph wyag {\n  node[shape=rect]\n");
  log_graphviz(repo, sha);
  printf("}\n");

  free(sha);
  fit_repository_free(repo);
  return FIT_OK;
}

static void ls_tree_recursive(fit_repository_t *repo, const char *sha,
                              bool recursive, const char *prefix) {
  fit_object_t *obj = object_read(repo, sha);
  if (!obj)
    return;

  for (size_t i = 0; i < obj->tree.count; i++) {
    tree_leaf_t *l = &obj->tree.items[i];
    bool is_tree = (strncmp(l->mode, "04", 2) == 0);

    char full_path[MAX_PATH];
    if (prefix && prefix[0])
      snprintf(full_path, sizeof(full_path), "%s/%s", prefix, l->path);
    else
      strncpy(full_path, l->path, sizeof(full_path) - 1);

    if (recursive && is_tree) {
      ls_tree_recursive(repo, l->sha, recursive, full_path);
    } else {
      printf("%s %s %s\t%s\n", l->mode, is_tree ? "tree" : "blob", l->sha,
             full_path);
    }
  }
  object_free(obj);
}

int cmd_ls_tree(bool recursive, const char *tree) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  char *sha = object_find(repo, tree, "tree", true);
  if (!sha) {
    fit_repository_free(repo);
    return FIT_ERROR;
  }

  ls_tree_recursive(repo, sha, recursive, "");
  free(sha);
  fit_repository_free(repo);
  return FIT_OK;
}

static int tree_checkout(fit_repository_t *repo, fit_object_t *tree,
                         const char *path) {
  for (size_t i = 0; i < tree->tree.count; i++) {
    tree_leaf_t *l = &tree->tree.items[i];
    fit_object_t *obj = object_read(repo, l->sha);
    if (!obj)
      continue;

    char dest[MAX_PATH];
    snprintf(dest, sizeof(dest), "%s/%s", path, l->path);

    if (obj->type == OBJ_TREE) {
      mkdir(dest, 0755);
      tree_checkout(repo, obj, dest);
    } else {
      FILE *fp = fopen(dest, "wb");
      if (fp) {
        fwrite(obj->blob.data, 1, obj->blob.size, fp);
        fclose(fp);
      }
    }
    object_free(obj);
  }
  return FIT_OK;
}

int cmd_checkout(const char *commit, const char *path) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  char *sha = object_find(repo, commit, NULL, true);
  if (!sha) {
    fit_repository_free(repo);
    return FIT_ERROR;
  }

  fit_object_t *obj = object_read(repo, sha);
  if (!obj) {
    free(sha);
    fit_repository_free(repo);
    return FIT_ERROR;
  }

  if (obj->type == OBJ_COMMIT) {
    kvlm_t *kv = kvlm_parse(obj->raw.data, obj->raw.size);
    const char *tree_sha = kvlm_get(kv, "tree");
    object_free(obj);
    obj = object_read(repo, tree_sha);
    kvlm_free(kv);
  }

  struct stat st;
  if (stat(path, &st) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      fprintf(stderr, "Not a directory: %s\n", path);
      object_free(obj);
      free(sha);
      fit_repository_free(repo);
      return FIT_ERROR;
    }
  } else {
    mkdir(path, 0755);
  }

  tree_checkout(repo, obj, path);
  object_free(obj);
  free(sha);
  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_show_ref(void) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  ref_entry_t *refs = ref_list(repo, NULL);
  for (ref_entry_t *r = refs; r; r = r->next) {
    const char *fitdir = repo->fitdir;
    const char *name = r->name;
    if (strncmp(name, fitdir, strlen(fitdir)) == 0)
      name += strlen(fitdir) + 1;
    printf("%s %s\n", r->sha, name);
  }
  ref_list_free(refs);
  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_tag(bool create_obj, const char *name, const char *object) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  if (!name) {
    ref_entry_t *refs = ref_list(repo, NULL);
    for (ref_entry_t *r = refs; r; r = r->next) {
      const char *n = strstr(r->name, "refs/tags/");
      if (n)
        printf("%s\n", n + 10);
    }
    ref_list_free(refs);
  } else if (!create_obj) {
    char *sha = object_find(repo, object ? object : "HEAD", NULL, true);
    if (sha) {
      ref_create(repo, name, sha);
      free(sha);
    }
  } else {
    char *sha = object_find(repo, object ? object : "HEAD", NULL, true);
    if (!sha) {
      fit_repository_free(repo);
      return FIT_ERROR;
    }

    fit_config_t *cfg = fitconfig_read();
    char *user = fitconfig_user_get(cfg);
    fitconfig_free(cfg);

    time_t now = time(NULL);
    char tstamp[64];
    snprintf(tstamp, sizeof(tstamp), "%ld +0000", (long)now);

    char tag_data[4096];
    snprintf(tag_data, sizeof(tag_data),
             "object %s\ntype commit\ntag %s\ntagger %s %s\n\n", sha, name,
             user ? user : "Unknown", tstamp);
    free(user);
    free(sha);

    fit_object_t obj;
    obj.type = OBJ_TAG;
    obj.raw.data = (unsigned char *)tag_data;
    obj.raw.size = strlen(tag_data);

    char *tag_sha = object_write(&obj, repo);
    ref_create(repo, name, tag_sha);
    free(tag_sha);
  }

  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_rev_parse(const char *type, const char *name) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  char *sha = object_find(repo, name, type, type != NULL);
  if (sha) {
    printf("%s\n", sha);
    free(sha);
  }

  fit_repository_free(repo);
  return sha ? FIT_OK : FIT_ERROR;
}

int cmd_ls_files(bool verbose) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  fit_index_t *index = index_read(repo);
  for (size_t i = 0; i < index->count; i++) {
    fit_index_entry_t *e = &index->entries[i];
    if (verbose) {
      printf("%s with perms: %04o\n", e->name, e->mode_perms);
      printf("  %s\n", e->sha);
      printf("  ctime: %u.%u, mtime: %u.%u, dev: %u, ino: %u\n", e->ctime_s,
             e->ctime_ns, e->mtime_s, e->mtime_ns, e->dev, e->ino);
      printf("  uid: %u, gid: %u, size: %u\n", e->uid, e->gid, e->fsize);
    } else {
      printf("%s\n", e->name);
    }
  }

  index_free(index);
  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_check_ignore(char **paths, int count) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  fit_ignore_t *rules = fitignore_read(repo);
  for (int i = 0; i < count; i++) {
    if (check_ignore(rules, paths[i]))
      printf("%s\n", paths[i]);
  }
  fitignore_free(rules);
  fit_repository_free(repo);
  return FIT_OK;
}

static void branch_get_active(fit_repository_t *repo, char *branch_out,
                              size_t sz) {
  char head_path[MAX_PATH];
  snprintf(head_path, sizeof(head_path), "%s/HEAD", repo->fitdir);

  FILE *fp = fopen(head_path, "r");
  if (!fp) {
    branch_out[0] = '\0';
    return;
  }

  char data[256];
  if (!fgets(data, sizeof(data), fp)) {
    fclose(fp);
    branch_out[0] = '\0';
    return;
  }
  fclose(fp);

  size_t len = strlen(data);
  if (len > 0 && data[len - 1] == '\n')
    data[--len] = '\0';

  const char *prefix = "ref: refs/heads/";
  if (strncmp(data, prefix, strlen(prefix)) == 0)
    strncpy(branch_out, data + strlen(prefix), sz - 1);
  else
    branch_out[0] = '\0';
}

static void index_to_set(fit_index_t *index, char ***out, size_t *count) {
  *out = malloc(index->count * sizeof(char *));
  *count = index->count;
  for (size_t i = 0; i < index->count; i++)
    (*out)[i] = index->entries[i].name;
}

static int walk_dir(const char *base, const char *rel, char ***files,
                    size_t *count, size_t *cap) {
  char full[MAX_PATH];
  if (rel[0])
    snprintf(full, sizeof(full), "%s/%s", base, rel);
  else
    strncpy(full, base, sizeof(full) - 1);

  DIR *d = opendir(full);
  if (!d)
    return FIT_ERROR;

  struct dirent *de;
  while ((de = readdir(d))) {
    if (de->d_name[0] == '.')
      continue;

    char child_rel[MAX_PATH];
    if (rel[0])
      snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name);
    else
      strncpy(child_rel, de->d_name, sizeof(child_rel) - 1);

    char child_full[MAX_PATH];
    snprintf(child_full, sizeof(child_full), "%s/%s", full, de->d_name);

    struct stat st;
    if (stat(child_full, &st) != 0)
      continue;

    if (S_ISDIR(st.st_mode)) {
      walk_dir(base, child_rel, files, count, cap);
    } else {
      if (*count >= *cap) {
        *cap *= 2;
        *files = realloc(*files, *cap * sizeof(char *));
      }
      (*files)[(*count)++] = strdup(child_rel);
    }
  }
  closedir(d);
  return FIT_OK;
}

int cmd_status(void) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  char branch[256];
  branch_get_active(repo, branch, sizeof(branch));
  printf("On branch %s\n\n", branch[0] ? branch : "(detached HEAD)");

  char *head_sha = ref_resolve(repo, "HEAD");
  fit_index_t *index = index_read(repo);

  if (head_sha) {
    char *tree_sha = object_find(repo, head_sha, "tree", true);
    if (tree_sha) {
      fit_object_t *tree = object_read(repo, tree_sha);
      if (tree) {
        printf("Changes to be committed:\n");
        for (size_t i = 0; i < tree->tree.count; i++) {
          tree_leaf_t *l = &tree->tree.items[i];
          bool found = false;
          for (size_t j = 0; j < index->count; j++) {
            if (strcmp(index->entries[j].name, l->path) == 0) {
              found = true;
              if (strcmp(index->entries[j].sha, l->sha) != 0)
                printf("  modified: %s\n", l->path);
              break;
            }
          }
          if (!found)
            printf("  deleted: %s\n", l->path);
        }
        for (size_t j = 0; j < index->count; j++) {
          bool found = false;
          for (size_t i = 0; i < tree->tree.count; i++) {
            if (strcmp(tree->tree.items[i].path, index->entries[j].name) == 0) {
              found = true;
              break;
            }
          }
          if (!found)
            printf("  new file: %s\n", index->entries[j].name);
        }
        object_free(tree);
      }
      free(tree_sha);
    }
    free(head_sha);
  } else {
    printf("Changes to be committed:\n");
    for (size_t i = 0; i < index->count; i++)
      printf("  new file: %s\n", index->entries[i].name);
  }

  printf("\nChanges not staged for commit:\n");
  fit_ignore_t *rules = fitignore_read(repo);

  for (size_t i = 0; i < index->count; i++) {
    fit_index_entry_t *e = &index->entries[i];
    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s/%s", repo->worktree, e->name);

    struct stat st;
    if (stat(full, &st) != 0) {
      printf("  deleted: %s\n", e->name);
    } else {
      if ((uint32_t)st.st_mtime != e->mtime_s) {
        printf("  modified: %s\n", e->name);
      }
    }
  }

  printf("\nUntracked files:\n");
  size_t all_cap = 256;
  char **all_files = malloc(all_cap * sizeof(char *));
  size_t all_count = 0;
  walk_dir(repo->worktree, "", &all_files, &all_count, &all_cap);

  for (size_t i = 0; i < all_count; i++) {
    bool in_index = false;
    for (size_t j = 0; j < index->count; j++) {
      if (strcmp(all_files[i], index->entries[j].name) == 0) {
        in_index = true;
        break;
      }
    }
    if (!in_index && !check_ignore(rules, all_files[i]))
      printf("  %s\n", all_files[i]);
    free(all_files[i]);
  }
  free(all_files);

  fitignore_free(rules);
  index_free(index);
  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_rm(char **paths, int count) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  fit_index_t *index = index_read(repo);

  for (int p = 0; p < count; p++) {
    bool found = false;
    for (size_t i = 0; i < index->count; i++) {
      if (strcmp(index->entries[i].name, paths[p]) == 0) {
        free(index->entries[i].name);
        memmove(&index->entries[i], &index->entries[i + 1],
                (index->count - i - 1) * sizeof(fit_index_entry_t));
        index->count--;
        found = true;

        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", repo->worktree, paths[p]);
        unlink(full);
        break;
      }
    }
    if (!found)
      fprintf(stderr, "error: pathspec '%s' did not match any files\n",
              paths[p]);
  }

  index_write(repo, index);
  index_free(index);
  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_add(char **paths, int count) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  fit_index_t *index = index_read(repo);

  for (int p = 0; p < count; p++) {
    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s/%s", repo->worktree, paths[p]);

    struct stat st;
    if (stat(full, &st) != 0) {
      fprintf(stderr, "Cannot stat %s\n", paths[p]);
      continue;
    }

    FILE *fp = fopen(full, "rb");
    if (!fp)
      continue;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *data = malloc(fsize);
    fread(data, 1, fsize, fp);
    fclose(fp);

    fit_object_t obj;
    obj.type = OBJ_BLOB;
    obj.blob.data = data;
    obj.blob.size = fsize;

    char *sha = object_write(&obj, repo);
    free(data);

    bool found = false;
    for (size_t i = 0; i < index->count; i++) {
      if (strcmp(index->entries[i].name, paths[p]) == 0) {
        strncpy(index->entries[i].sha, sha, 41);
        index->entries[i].mtime_s = (uint32_t)st.st_mtime;
        index->entries[i].fsize = (uint32_t)st.st_size;
        found = true;
        break;
      }
    }

    if (!found) {
      index->entries = realloc(index->entries,
                               (index->count + 1) * sizeof(fit_index_entry_t));
      fit_index_entry_t *e = &index->entries[index->count++];
      memset(e, 0, sizeof(fit_index_entry_t));
      e->name = strdup(paths[p]);
      strncpy(e->sha, sha, 41);
      e->ctime_s = (uint32_t)st.st_ctime;
      e->mtime_s = (uint32_t)st.st_mtime;
      e->dev = (uint32_t)st.st_dev;
      e->ino = (uint32_t)st.st_ino;
      e->mode_type = 0b1000;
      e->mode_perms = 0644;
      e->uid = st.st_uid;
      e->gid = st.st_gid;
      e->fsize = (uint32_t)st.st_size;
    }
    free(sha);
  }

  index_write(repo, index);
  index_free(index);
  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_commit(const char *message) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  fit_config_t *cfg = fitconfig_read();
  char *user = fitconfig_user_get(cfg);
  fitconfig_free(cfg);

  if (!user) {
    fprintf(stderr,
            "Your identity is not set. Configure user.name and user.email.\n");
    fit_repository_free(repo);
    return FIT_ERROR;
  }

  fit_index_t *index = index_read(repo);

  fit_object_t tree;
  tree.type = OBJ_TREE;
  size_t count = index->count;
  tree.tree.items = malloc(count * sizeof(tree_leaf_t));
  tree.tree.count = count;

  for (size_t i = 0; i < count; i++) {
    fit_index_entry_t *e = &index->entries[i];
    tree_leaf_t *l = &tree.tree.items[i];
    l->path = e->name;
    snprintf(l->mode, sizeof(l->mode), "%04o%02o", (unsigned)e->mode_type,
             (unsigned)e->mode_perms);
    strncpy(l->sha, e->sha, 41);
  }

  char *tree_sha = object_write(&tree, repo);
  free(tree.tree.items);
  index_free(index);

  char *parent_sha = ref_resolve(repo, "HEAD");

  time_t now = time(NULL);
  struct tm *tm = gmtime(&now);
  char tstamp[64];
  long offset = 0;
  snprintf(tstamp, sizeof(tstamp), "%ld %+03ld00", (long)now, offset);

  char commit_data[8192];
  int pos = 0;
  pos += snprintf(commit_data + pos, sizeof(commit_data) - pos, "tree %s\n",
                  tree_sha);
  if (parent_sha)
    pos += snprintf(commit_data + pos, sizeof(commit_data) - pos, "parent %s\n",
                    parent_sha);
  pos += snprintf(commit_data + pos, sizeof(commit_data) - pos,
                  "author %s %s\ncommitter %s %s\n\n%s\n", user, tstamp, user,
                  tstamp, message ? message : "");

  free(tree_sha);
  free(parent_sha);
  free(user);

  fit_object_t commit_obj;
  commit_obj.type = OBJ_COMMIT;
  commit_obj.raw.data = (unsigned char *)commit_data;
  commit_obj.raw.size = (size_t)pos;

  char *commit_sha = object_write(&commit_obj, repo);

  char branch[256];
  branch_get_active(repo, branch, sizeof(branch));

  char ref_name[512];
  snprintf(ref_name, sizeof(ref_name), "refs/heads/%s",
           branch[0] ? branch : "master");

  char ref_path[MAX_PATH];
  snprintf(ref_path, sizeof(ref_path), "%s/%s", repo->fitdir, ref_name);
  FILE *fp = fopen(ref_path, "w");
  if (fp) {
    fprintf(fp, "%s\n", commit_sha);
    fclose(fp);
  }

  printf("[%s %.7s] %s\n", branch[0] ? branch : "master", commit_sha,
         message ? message : "");
  free(commit_sha);
  fit_repository_free(repo);
  return FIT_OK;
}

int cmd_push(const char *remote, const char *username, const char *password) {
  fit_repository_t *repo = find_repo();
  if (!repo)
    return FIT_ERROR;

  char user_buf[256] = "", pass_buf[256] = "";

  if (!username) {
    printf("Username: ");
    fflush(stdout);
    if (fgets(user_buf, sizeof(user_buf), stdin)) {
      size_t l = strlen(user_buf);
      if (l > 0 && user_buf[l - 1] == '\n')
        user_buf[l - 1] = '\0';
    }
    username = user_buf;
  }

  if (!password) {
    printf("Password: ");
    fflush(stdout);
    if (fgets(pass_buf, sizeof(pass_buf), stdin)) {
      size_t l = strlen(pass_buf);
      if (l > 0 && pass_buf[l - 1] == '\n')
        pass_buf[l - 1] = '\0';
    }
    password = pass_buf;
  }

  int ret = push(repo, remote, username, password);
  fit_repository_free(repo);
  return ret;
}
