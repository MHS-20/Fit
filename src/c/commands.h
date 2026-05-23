#ifndef FIT_COMMANDS_H
#define FIT_COMMANDS_H

#include "fit.h"
#include "ignore.h"
#include "index.h"
#include "objects.h"
#include "refs.h"
#include "remote.h"
#include "repository.h"

int cmd_init(const char *path);
int cmd_cat_file(const char *type, const char *object);
int cmd_hash_object(const char *type, bool write, const char *path);
int cmd_log(const char *commit);
int cmd_ls_tree(bool recursive, const char *tree);
int cmd_checkout(const char *commit, const char *path);
int cmd_show_ref(void);
int cmd_tag(bool create_obj, const char *name, const char *object);
int cmd_rev_parse(const char *type, const char *name);
int cmd_ls_files(bool verbose);
int cmd_check_ignore(char **paths, int count);
int cmd_status(void);
int cmd_rm(char **paths, int count);
int cmd_add(char **paths, int count);
int cmd_commit(const char *message);
int cmd_push(const char *remote, const char *username, const char *password);

#endif
