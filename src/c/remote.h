#ifndef FIT_REMOTE_H
#define FIT_REMOTE_H

#include "fit.h"
#include "repository.h"

char *get_local_master_hash(fit_repository_t *repo);
char *get_remote_master_hash(const char *git_url, const char *username,
                             const char *password);
int push(fit_repository_t *repo, const char *git_url, const char *username,
         const char *password);

#endif
