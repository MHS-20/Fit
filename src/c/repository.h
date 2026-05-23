#ifndef FIT_REPOSITORY_H
#define FIT_REPOSITORY_H

#include "fit.h"
#include "config.h"

/* Repository structure */
typedef struct fit_repository {
    char *worktree;
    char *fitdir;
    fit_config_t *conf;
} fit_repository_t;

/* Function prototypes */
fit_repository_t *fit_repository_new(const char *path, bool force);
void fit_repository_free(fit_repository_t *repo);
char *repo_path(fit_repository_t *repo, ...);
char *repo_file(fit_repository_t *repo, bool mkdir, ...);
char *repo_dir(fit_repository_t *repo, bool mkdir, ...);
fit_repository_t *repo_create(const char *path);
fit_repository_t *repo_find(const char *path, bool required);

#endif /* FIT_REPOSITORY_H */
