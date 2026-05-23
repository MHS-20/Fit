#ifndef FIT_IGNORE_H
#define FIT_IGNORE_H

#include "fit.h"
#include "index.h"
#include "repository.h"

typedef struct ignore_rule {
  char *pattern;
  bool include;
  struct ignore_rule *next;
} ignore_rule_t;

typedef struct scoped_rules {
  char *dir;
  ignore_rule_t *rules;
  struct scoped_rules *next;
} scoped_rules_t;

typedef struct {
  ignore_rule_t **absolute;
  size_t absolute_count;
  scoped_rules_t *scoped;
} fit_ignore_t;

fit_ignore_t *fitignore_read(fit_repository_t *repo);
void fitignore_free(fit_ignore_t *ig);
bool check_ignore(fit_ignore_t *rules, const char *path);

#endif
