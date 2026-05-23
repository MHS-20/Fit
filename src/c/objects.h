#ifndef FIT_OBJECTS_H
#define FIT_OBJECTS_H

#include "fit.h"
#include "repository.h"

typedef enum {
  OBJ_BLOB,
  OBJ_COMMIT,
  OBJ_TREE,
  OBJ_TAG,
} obj_type_t;

typedef struct {
  char *path;
  char mode[8];
  char sha[41];
} tree_leaf_t;

typedef struct {
  obj_type_t type;
  union {
    struct {
      unsigned char *data;
      size_t size;
    } blob;
    struct {
      unsigned char *data;
      size_t size;
    } raw;
    struct {
      tree_leaf_t *items;
      size_t count;
    } tree;
  };
} fit_object_t;

fit_object_t *object_read(fit_repository_t *repo, const char *sha);
char *object_write(fit_object_t *obj, fit_repository_t *repo);
void object_free(fit_object_t *obj);

char *object_find(fit_repository_t *repo, const char *name, const char *fmt,
                  bool follow);
char **object_resolve(fit_repository_t *repo, const char *name, int *count);

const char *obj_type_str(obj_type_t type);
obj_type_t obj_type_from_str(const char *s);

typedef struct {
  char *key;
  unsigned char *value;
  size_t value_len;
  struct kvlm_entry *next;
} kvlm_entry_t;

typedef struct {
  kvlm_entry_t *head;
  unsigned char *message;
  size_t message_len;
} kvlm_t;

kvlm_t *kvlm_parse(const unsigned char *raw, size_t len);
unsigned char *kvlm_serialize(kvlm_t *kv, size_t *out_len);
void kvlm_free(kvlm_t *kv);
const char *kvlm_get(kvlm_t *kv, const char *key);

#endif
