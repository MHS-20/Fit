#include "refs.h"
#include <dirent.h>
#include <sys/stat.h>

char *ref_resolve(fit_repository_t *repo, const char *ref) {
  char *path;
  if (ref[0] == '/') {
    path = strdup(ref);
  } else {
    path = repo_file(repo, false, ref, NULL);
  }
  if (!path)
    return NULL;

  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    free(path);
    return NULL;
  }

  FILE *fp = fopen(path, "r");
  free(path);
  if (!fp)
    return NULL;

  char data[MAX_PATH];
  if (!fgets(data, sizeof(data), fp)) {
    fclose(fp);
    return NULL;
  }
  fclose(fp);

  size_t len = strlen(data);
  if (len > 0 && data[len - 1] == '\n')
    data[len - 1] = '\0';

  if (strncmp(data, "ref: ", 5) == 0) {
    return ref_resolve(repo, data + 5);
  }
  return strdup(data);
}

static ref_entry_t *ref_list_dir(fit_repository_t *repo, const char *path) {
  DIR *dir = opendir(path);
  if (!dir)
    return NULL;

  ref_entry_t *head = NULL;
  ref_entry_t **tail = &head;

  struct dirent **namelist;
  int n = scandir(path, &namelist, NULL, alphasort);
  if (n < 0) {
    closedir(dir);
    return NULL;
  }

  for (int i = 0; i < n; i++) {
    struct dirent *entry = namelist[i];
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      free(entry);
      continue;
    }

    char child[MAX_PATH];
    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

    struct stat st;
    if (stat(child, &st) != 0) {
      free(entry);
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      ref_entry_t *sub = ref_list_dir(repo, child);
      while (sub) {
        ref_entry_t *next = sub->next;
        sub->next = NULL;
        *tail = sub;
        tail = &sub->next;
        sub = next;
      }
    } else {
      char *sha = ref_resolve(repo, child);
      if (sha) {
        ref_entry_t *e = malloc(sizeof(ref_entry_t));
        e->name = strdup(child);
        e->sha = sha;
        e->next = NULL;
        *tail = e;
        tail = &e->next;
      }
    }
    free(entry);
  }
  free(namelist);
  closedir(dir);
  return head;
}

ref_entry_t *ref_list(fit_repository_t *repo, const char *path) {
  char refs_path[MAX_PATH];
  if (!path) {
    snprintf(refs_path, sizeof(refs_path), "%s/refs", repo->fitdir);
    path = refs_path;
  }
  return ref_list_dir(repo, path);
}

void ref_list_free(ref_entry_t *list) {
  while (list) {
    ref_entry_t *next = list->next;
    free(list->name);
    free(list->sha);
    free(list);
    list = next;
  }
}

int ref_create(fit_repository_t *repo, const char *ref_name, const char *sha) {
  char ref_path[MAX_PATH];
  snprintf(ref_path, sizeof(ref_path), "refs/%s", ref_name);
  char *path = repo_file(repo, true, ref_path, NULL);
  if (!path)
    return FIT_ERROR;

  FILE *fp = fopen(path, "w");
  free(path);
  if (!fp)
    return FIT_ERROR;

  fprintf(fp, "%s\n", sha);
  fclose(fp);
  return FIT_OK;
}
