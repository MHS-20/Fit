#ifndef FIT_REFS_H
#define FIT_REFS_H

#include "fit.h"
#include "repository.h"

typedef struct ref_entry {
  char *name;
  char *sha;
  struct ref_entry *next;
} ref_entry_t;

char *ref_resolve(fit_repository_t *repo, const char *ref);
ref_entry_t *ref_list(fit_repository_t *repo, const char *path);
void ref_list_free(ref_entry_t *list);
int ref_create(fit_repository_t *repo, const char *ref_name, const char *sha);

#endif
