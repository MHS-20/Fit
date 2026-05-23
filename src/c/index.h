#ifndef FIT_INDEX_H
#define FIT_INDEX_H

#include "fit.h"
#include "repository.h"
#include <stdint.h>

typedef struct {
  uint32_t ctime_s;
  uint32_t ctime_ns;
  uint32_t mtime_s;
  uint32_t mtime_ns;
  uint32_t dev;
  uint32_t ino;
  uint16_t mode_type;
  uint16_t mode_perms;
  uint32_t uid;
  uint32_t gid;
  uint32_t fsize;
  char sha[41];
  bool flag_assume_valid;
  uint16_t flag_stage;
  char *name;
} fit_index_entry_t;

typedef struct {
  uint32_t version;
  fit_index_entry_t *entries;
  size_t count;
} fit_index_t;

fit_index_t *index_read(fit_repository_t *repo);
int index_write(fit_repository_t *repo, fit_index_t *index);
void index_free(fit_index_t *index);

#endif
