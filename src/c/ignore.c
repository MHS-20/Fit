#include "ignore.h"
#include "objects.h"
#include <fnmatch.h>

static ignore_rule_t *parse_rules(const char **lines, size_t count) {
  ignore_rule_t *head = NULL;
  ignore_rule_t **tail = &head;

  for (size_t i = 0; i < count; i++) {
    const char *line = lines[i];
    while (*line == ' ' || *line == '\t')
      line++;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' '))
      len--;
    if (len == 0 || line[0] == '#')
      continue;

    ignore_rule_t *rule = malloc(sizeof(ignore_rule_t));
    rule->next = NULL;

    if (line[0] == '!') {
      rule->include = false;
      rule->pattern = strndup(line + 1, len - 1);
    } else if (line[0] == '\\') {
      rule->include = true;
      rule->pattern = strndup(line + 1, len - 1);
    } else {
      rule->include = true;
      rule->pattern = strndup(line, len);
    }

    *tail = rule;
    tail = &rule->next;
  }
  return head;
}

static ignore_rule_t *parse_rules_from_file(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    return NULL;

  char **lines = NULL;
  size_t count = 0, cap = 0;
  char buf[1024];
  while (fgets(buf, sizeof(buf), fp)) {
    if (count >= cap) {
      cap = cap ? cap * 2 : 16;
      lines = realloc(lines, cap * sizeof(char *));
    }
    lines[count++] = strdup(buf);
  }
  fclose(fp);

  ignore_rule_t *rules = parse_rules((const char **)lines, count);
  for (size_t i = 0; i < count; i++)
    free(lines[i]);
  free(lines);
  return rules;
}

fit_ignore_t *fitignore_read(fit_repository_t *repo) {
  fit_ignore_t *ig = calloc(1, sizeof(fit_ignore_t));

  size_t abs_cap = 4;
  ig->absolute = malloc(abs_cap * sizeof(ignore_rule_t *));

  char exclude_path[MAX_PATH];
  snprintf(exclude_path, sizeof(exclude_path), "%s/info/exclude", repo->fitdir);
  ignore_rule_t *excl = parse_rules_from_file(exclude_path);
  if (excl) {
    if (ig->absolute_count >= abs_cap) {
      abs_cap *= 2;
      ig->absolute = realloc(ig->absolute, abs_cap * sizeof(ignore_rule_t *));
    }
    ig->absolute[ig->absolute_count++] = excl;
  }

  const char *xdg = getenv("XDG_CONFIG_HOME");
  char global_path[MAX_PATH];
  if (xdg) {
    snprintf(global_path, sizeof(global_path), "%s/fit/ignore", xdg);
  } else {
    const char *home = getenv("HOME");
    if (!home)
      home = "/tmp";
    snprintf(global_path, sizeof(global_path), "%s/.config/fit/ignore", home);
  }
  ignore_rule_t *global = parse_rules_from_file(global_path);
  if (global) {
    if (ig->absolute_count >= abs_cap) {
      abs_cap *= 2;
      ig->absolute = realloc(ig->absolute, abs_cap * sizeof(ignore_rule_t *));
    }
    ig->absolute[ig->absolute_count++] = global;
  }

  fit_index_t *index = index_read(repo);
  for (size_t i = 0; i < index->count; i++) {
    fit_index_entry_t *e = &index->entries[i];
    const char *name = e->name;
    bool is_fitignore = (strcmp(name, ".fitignore") == 0);
    if (!is_fitignore) {
      size_t nlen = strlen(name);
      if (nlen > 11 && strcmp(name + nlen - 11, "/.fitignore") == 0)
        is_fitignore = true;
    }
    if (!is_fitignore)
      continue;

    fit_object_t *obj = object_read(repo, e->sha);
    if (!obj)
      continue;

    const char *blob_data = (const char *)obj->blob.data;
    size_t blob_len = obj->blob.size;

    char **lines = NULL;
    size_t lcount = 0, lcap = 0;
    size_t line_start = 0;
    for (size_t j = 0; j <= blob_len; j++) {
      if (j == blob_len || blob_data[j] == '\n') {
        size_t ll = j - line_start;
        char *line = malloc(ll + 2);
        memcpy(line, blob_data + line_start, ll);
        line[ll] = '\n';
        line[ll + 1] = '\0';
        if (lcount >= lcap) {
          lcap = lcap ? lcap * 2 : 16;
          lines = realloc(lines, lcap * sizeof(char *));
        }
        lines[lcount++] = line;
        line_start = j + 1;
      }
    }

    const char *dir_name = "";
    char dir_buf[MAX_PATH];
    const char *slash = strrchr(name, '/');
    if (slash) {
      size_t dlen = slash - name;
      memcpy(dir_buf, name, dlen);
      dir_buf[dlen] = '\0';
      dir_name = dir_buf;
    }

    ignore_rule_t *rules = parse_rules((const char **)lines, lcount);
    for (size_t j = 0; j < lcount; j++)
      free(lines[j]);
    free(lines);
    object_free(obj);

    scoped_rules_t *sr = malloc(sizeof(scoped_rules_t));
    sr->dir = strdup(dir_name);
    sr->rules = rules;
    sr->next = ig->scoped;
    ig->scoped = sr;
  }
  index_free(index);

  return ig;
}

static int check_rules(ignore_rule_t *rules, const char *path) {
  int result = -1;
  for (ignore_rule_t *r = rules; r; r = r->next) {
    if (fnmatch(r->pattern, path, FNM_PATHNAME) == 0 ||
        fnmatch(r->pattern, path, 0) == 0) {
      result = r->include ? 1 : 0;
    }
  }
  return result;
}

static int check_scoped(scoped_rules_t *scoped, const char *path) {
  char parent[MAX_PATH];
  strncpy(parent, path, sizeof(parent));
  char *slash;

  while (1) {
    slash = strrchr(parent, '/');
    if (slash)
      *slash = '\0';
    else
      parent[0] = '\0';

    for (scoped_rules_t *sr = scoped; sr; sr = sr->next) {
      if (strcmp(sr->dir, parent) == 0) {
        int r = check_rules(sr->rules, path);
        if (r >= 0)
          return r;
      }
    }

    if (parent[0] == '\0')
      break;
  }

  return -1;
}

bool check_ignore(fit_ignore_t *rules, const char *path) {
  if (path[0] == '/') {
    fprintf(stderr, "check_ignore requires relative path\n");
    return false;
  }

  int r = check_scoped(rules->scoped, path);
  if (r >= 0)
    return r == 1;

  for (size_t i = 0; i < rules->absolute_count; i++) {
    int result = check_rules(rules->absolute[i], path);
    if (result >= 0)
      return result == 1;
  }

  return false;
}

static void free_rules(ignore_rule_t *rules) {
  while (rules) {
    ignore_rule_t *next = rules->next;
    free(rules->pattern);
    free(rules);
    rules = next;
  }
}

void fitignore_free(fit_ignore_t *ig) {
  if (!ig)
    return;
  for (size_t i = 0; i < ig->absolute_count; i++)
    free_rules(ig->absolute[i]);
  free(ig->absolute);
  scoped_rules_t *sr = ig->scoped;
  while (sr) {
    scoped_rules_t *next = sr->next;
    free(sr->dir);
    free_rules(sr->rules);
    free(sr);
    sr = next;
  }
  free(ig);
}
