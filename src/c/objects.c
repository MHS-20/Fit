#include "objects.h"
#include "refs.h"
#include <ctype.h>
#include <dirent.h>
#include <openssl/sha.h>
#include <zlib.h>

const char *obj_type_str(obj_type_t type) {
  switch (type) {
  case OBJ_BLOB:
    return "blob";
  case OBJ_COMMIT:
    return "commit";
  case OBJ_TREE:
    return "tree";
  case OBJ_TAG:
    return "tag";
  }
  return "unknown";
}

obj_type_t obj_type_from_str(const char *s) {
  if (strcmp(s, "blob") == 0)
    return OBJ_BLOB;
  if (strcmp(s, "commit") == 0)
    return OBJ_COMMIT;
  if (strcmp(s, "tree") == 0)
    return OBJ_TREE;
  if (strcmp(s, "tag") == 0)
    return OBJ_TAG;
  return OBJ_BLOB;
}

static tree_leaf_t *tree_parse(const unsigned char *raw, size_t len,
                               size_t *out_count) {
  size_t cap = 8, count = 0;
  tree_leaf_t *items = malloc(cap * sizeof(tree_leaf_t));
  size_t pos = 0;

  while (pos < len) {
    size_t sp = pos;
    while (sp < len && raw[sp] != ' ')
      sp++;
    if (sp >= len)
      break;

    size_t mode_len = sp - pos;
    char mode[8] = {0};
    if (mode_len == 5) {
      mode[0] = '0';
      memcpy(mode + 1, raw + pos, 5);
    } else {
      memcpy(mode, raw + pos, mode_len < 7 ? mode_len : 7);
    }

    size_t np = sp + 1;
    while (np < len && raw[np] != 0)
      np++;
    if (np >= len)
      break;

    char *path = malloc(np - sp);
    memcpy(path, raw + sp + 1, np - sp - 1);
    path[np - sp - 1] = '\0';

    if (np + 21 > len) {
      free(path);
      break;
    }

    unsigned long long sha_int = 0;
    char sha[41];
    const unsigned char *sha_bytes = raw + np + 1;
    for (int i = 0; i < 20; i++) {
      snprintf(sha + i * 2, 3, "%02x", sha_bytes[i]);
    }
    sha[40] = '\0';

    if (count >= cap) {
      cap *= 2;
      items = realloc(items, cap * sizeof(tree_leaf_t));
    }
    items[count].path = path;
    memcpy(items[count].mode, mode, 8);
    memcpy(items[count].sha, sha, 41);
    count++;

    pos = np + 21;
  }

  *out_count = count;
  return items;
}

static int tree_leaf_cmp(const void *a, const void *b) {
  const tree_leaf_t *la = (const tree_leaf_t *)a;
  const tree_leaf_t *lb = (const tree_leaf_t *)b;
  char ka[MAX_PATH + 2], kb[MAX_PATH + 2];
  snprintf(ka, sizeof(ka), "%s%s", la->path, la->mode[0] == '4' ? "/" : "");
  snprintf(kb, sizeof(kb), "%s%s", lb->path, lb->mode[0] == '4' ? "/" : "");
  return strcmp(ka, kb);
}

static unsigned char *tree_serialize(fit_object_t *obj, size_t *out_len) {
  qsort(obj->tree.items, obj->tree.count, sizeof(tree_leaf_t), tree_leaf_cmp);

  size_t cap = 1024;
  unsigned char *buf = malloc(cap);
  size_t pos = 0;

  for (size_t i = 0; i < obj->tree.count; i++) {
    tree_leaf_t *leaf = &obj->tree.items[i];
    const char *mode = leaf->mode;
    size_t mode_len = strlen(mode);
    size_t path_len = strlen(leaf->path);
    size_t need = mode_len + 1 + path_len + 1 + 20;

    while (pos + need > cap) {
      cap *= 2;
      buf = realloc(buf, cap);
    }

    memcpy(buf + pos, mode, mode_len);
    pos += mode_len;
    buf[pos++] = ' ';
    memcpy(buf + pos, leaf->path, path_len);
    pos += path_len;
    buf[pos++] = '\0';

    unsigned long long v = 0;
    for (int j = 0; j < 40; j += 2) {
      unsigned int byte;
      sscanf(leaf->sha + j, "%02x", &byte);
      buf[pos++] = (unsigned char)byte;
    }
  }

  *out_len = pos;
  return buf;
}

fit_object_t *object_read(fit_repository_t *repo, const char *sha) {
  char sha0[3], sha1[39];
  strncpy(sha0, sha, 2);
  sha0[2] = '\0';
  strncpy(sha1, sha + 2, 38);
  sha1[38] = '\0';

  char *path = repo_file(repo, false, "objects", sha0, sha1, NULL);
  if (!path)
    return NULL;

  FILE *fp = fopen(path, "rb");
  free(path);
  if (!fp)
    return NULL;

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  unsigned char *compressed = malloc(fsize);
  fread(compressed, 1, fsize, fp);
  fclose(fp);

  uLongf raw_size = fsize * 10 + 4096;
  unsigned char *raw = malloc(raw_size);
  while (uncompress(raw, &raw_size, compressed, fsize) == Z_BUF_ERROR) {
    raw_size *= 2;
    raw = realloc(raw, raw_size);
  }
  free(compressed);

  size_t x = 0;
  while (x < raw_size && raw[x] != ' ')
    x++;
  char fmt[16];
  memcpy(fmt, raw, x < 15 ? x : 15);
  fmt[x < 15 ? x : 15] = '\0';

  size_t y = x + 1;
  while (y < raw_size && raw[y] != '\0')
    y++;

  size_t data_size = raw_size - y - 1;
  unsigned char *data = raw + y + 1;

  fit_object_t *obj = malloc(sizeof(fit_object_t));

  if (strcmp(fmt, "blob") == 0) {
    obj->type = OBJ_BLOB;
    obj->blob.data = malloc(data_size);
    memcpy(obj->blob.data, data, data_size);
    obj->blob.size = data_size;
  } else if (strcmp(fmt, "commit") == 0 || strcmp(fmt, "tag") == 0) {
    obj->type = (strcmp(fmt, "commit") == 0) ? OBJ_COMMIT : OBJ_TAG;
    obj->raw.data = malloc(data_size);
    memcpy(obj->raw.data, data, data_size);
    obj->raw.size = data_size;
  } else if (strcmp(fmt, "tree") == 0) {
    obj->type = OBJ_TREE;
    obj->tree.items = tree_parse(data, data_size, &obj->tree.count);
  } else {
    free(obj);
    free(raw);
    return NULL;
  }

  free(raw);
  return obj;
}

char *object_write(fit_object_t *obj, fit_repository_t *repo) {
  unsigned char *data;
  size_t data_len;
  int need_free = 0;

  if (obj->type == OBJ_BLOB) {
    data = obj->blob.data;
    data_len = obj->blob.size;
  } else if (obj->type == OBJ_COMMIT || obj->type == OBJ_TAG) {
    data = obj->raw.data;
    data_len = obj->raw.size;
  } else {
    data = tree_serialize(obj, &data_len);
    need_free = 1;
  }

  const char *type_str = obj_type_str(obj->type);
  char header[64];
  int hlen = snprintf(header, sizeof(header), "%s %zu", type_str, data_len);

  size_t result_len = hlen + 1 + data_len;
  unsigned char *result = malloc(result_len);
  memcpy(result, header, hlen);
  result[hlen] = '\0';
  memcpy(result + hlen + 1, data, data_len);

  if (need_free)
    free(data);

  unsigned char sha_bytes[20];
  SHA1(result, result_len, sha_bytes);
  char sha[41];
  for (int i = 0; i < 20; i++)
    snprintf(sha + i * 2, 3, "%02x", sha_bytes[i]);
  sha[40] = '\0';

  if (repo) {
    char sha0[3], sha1[39];
    strncpy(sha0, sha, 2);
    sha0[2] = '\0';
    strncpy(sha1, sha + 2, 38);
    sha1[38] = '\0';

    char *path = repo_file(repo, true, "objects", sha0, sha1, NULL);
    if (path) {
      struct stat st;
      if (stat(path, &st) != 0) {
        uLongf comp_size = compressBound(result_len);
        unsigned char *comp = malloc(comp_size);
        compress(comp, &comp_size, result, result_len);

        FILE *fp = fopen(path, "wb");
        if (fp) {
          fwrite(comp, 1, comp_size, fp);
          fclose(fp);
        }
        free(comp);
      }
      free(path);
    }
  }

  free(result);
  return strdup(sha);
}

void object_free(fit_object_t *obj) {
  if (!obj)
    return;
  if (obj->type == OBJ_BLOB) {
    free(obj->blob.data);
  } else if (obj->type == OBJ_COMMIT || obj->type == OBJ_TAG) {
    free(obj->raw.data);
  } else if (obj->type == OBJ_TREE) {
    for (size_t i = 0; i < obj->tree.count; i++)
      free(obj->tree.items[i].path);
    free(obj->tree.items);
  }
  free(obj);
}

kvlm_t *kvlm_parse(const unsigned char *raw, size_t len) {
  kvlm_t *kv = calloc(1, sizeof(kvlm_t));
  size_t start = 0;

  while (start < len) {
    size_t spc = start;
    while (spc < len && raw[spc] != ' ' && raw[spc] != '\n')
      spc++;

    if (spc >= len || raw[spc] == '\n') {
      if (start < len && raw[start] == '\n') {
        size_t msg_start = start + 1;
        kv->message_len = len - msg_start;
        kv->message = malloc(kv->message_len + 1);
        memcpy(kv->message, raw + msg_start, kv->message_len);
        kv->message[kv->message_len] = '\0';
      }
      break;
    }

    char key[256];
    size_t klen = spc - start;
    if (klen >= sizeof(key))
      klen = sizeof(key) - 1;
    memcpy(key, raw + start, klen);
    key[klen] = '\0';

    size_t end = spc + 1;
    while (1) {
      size_t nl = end;
      while (nl < len && raw[nl] != '\n')
        nl++;
      if (nl + 1 >= len || raw[nl + 1] != ' ') {
        end = nl;
        break;
      }
      end = nl + 1;
    }

    size_t vlen = end - spc - 1;
    unsigned char *val = malloc(vlen + 1);
    size_t vi = 0;
    for (size_t i = spc + 1; i < end; i++) {
      if (raw[i] == '\n' && i + 1 < end && raw[i + 1] == ' ') {
        val[vi++] = '\n';
        i++;
      } else {
        val[vi++] = raw[i];
      }
    }
    val[vi] = '\0';

    kvlm_entry_t *e = malloc(sizeof(kvlm_entry_t));
    e->key = strdup(key);
    e->value = val;
    e->value_len = vi;
    e->next = NULL;

    kvlm_entry_t **tail = &kv->head;
    while (*tail)
      tail = (kvlm_entry_t **)&(*tail)->next;
    *tail = e;

    start = end + 1;
  }

  return kv;
}

unsigned char *kvlm_serialize(kvlm_t *kv, size_t *out_len) {
  size_t cap = 4096;
  unsigned char *buf = malloc(cap);
  size_t pos = 0;

#define ENSURE(n)                                                              \
  while (pos + (n) > cap) {                                                    \
    cap *= 2;                                                                  \
    buf = realloc(buf, cap);                                                   \
  }

  kvlm_entry_t *e = kv->head;
  while (e) {
    size_t klen = strlen(e->key);
    ENSURE(klen + 1 + e->value_len * 2 + 2);
    memcpy(buf + pos, e->key, klen);
    pos += klen;
    buf[pos++] = ' ';
    for (size_t i = 0; i < e->value_len; i++) {
      if (e->value[i] == '\n') {
        ENSURE(2);
        buf[pos++] = '\n';
        buf[pos++] = ' ';
      } else {
        ENSURE(1);
        buf[pos++] = e->value[i];
      }
    }
    ENSURE(1);
    buf[pos++] = '\n';
    e = (kvlm_entry_t *)e->next;
  }

  ENSURE(1 + kv->message_len);
  buf[pos++] = '\n';
  memcpy(buf + pos, kv->message, kv->message_len);
  pos += kv->message_len;

  *out_len = pos;
  return buf;
}

void kvlm_free(kvlm_t *kv) {
  if (!kv)
    return;
  kvlm_entry_t *e = kv->head;
  while (e) {
    kvlm_entry_t *next = (kvlm_entry_t *)e->next;
    free(e->key);
    free(e->value);
    free(e);
    e = next;
  }
  free(kv->message);
  free(kv);
}

const char *kvlm_get(kvlm_t *kv, const char *key) {
  kvlm_entry_t *e = kv->head;
  while (e) {
    if (strcmp(e->key, key) == 0)
      return (char *)e->value;
    e = (kvlm_entry_t *)e->next;
  }
  return NULL;
}

char **object_resolve(fit_repository_t *repo, const char *name, int *count) {
  char **candidates = NULL;
  int cap = 0;
  *count = 0;

#define ADD(s)                                                                 \
  do {                                                                         \
    if (*count >= cap) {                                                       \
      cap = cap ? cap * 2 : 4;                                                 \
      candidates = realloc(candidates, cap * sizeof(char *));                  \
    }                                                                          \
    candidates[(*count)++] = strdup(s);                                        \
  } while (0)

  if (!name || !name[0])
    return NULL;

  if (strcmp(name, "HEAD") == 0) {
    char *h = ref_resolve(repo, "HEAD");
    if (h) {
      ADD(h);
      free(h);
    }
    return candidates;
  }

  bool is_hex = true;
  size_t nlen = strlen(name);
  if (nlen < 4 || nlen > 40)
    is_hex = false;
  for (size_t i = 0; i < nlen && is_hex; i++) {
    if (!isxdigit((unsigned char)name[i]))
      is_hex = false;
  }

  if (is_hex) {
    char lower[41];
    for (size_t i = 0; i < nlen; i++)
      lower[i] = tolower((unsigned char)name[i]);
    lower[nlen] = '\0';

    char sha0[3];
    strncpy(sha0, lower, 2);
    sha0[2] = '\0';
    char *obj_dir = repo_dir(repo, false, "objects", sha0, NULL);
    if (obj_dir) {
      DIR *d = opendir(obj_dir);
      if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
          if (de->d_name[0] == '.')
            continue;
          if (strncmp(de->d_name, lower + 2, nlen - 2) == 0) {
            char full[41];
            snprintf(full, sizeof(full), "%s%s", sha0, de->d_name);
            ADD(full);
          }
        }
        closedir(d);
      }
      free(obj_dir);
    }
  }

  char *as_tag = ref_resolve(repo, "refs/tags");
  free(as_tag);
  char tag_ref[MAX_PATH];
  snprintf(tag_ref, sizeof(tag_ref), "refs/tags/%s", name);
  char *t = ref_resolve(repo, tag_ref);
  if (t) {
    ADD(t);
    free(t);
  }

  char branch_ref[MAX_PATH];
  snprintf(branch_ref, sizeof(branch_ref), "refs/heads/%s", name);
  char *b = ref_resolve(repo, branch_ref);
  if (b) {
    ADD(b);
    free(b);
  }

  char remote_ref[MAX_PATH];
  snprintf(remote_ref, sizeof(remote_ref), "refs/remotes/%s", name);
  char *r = ref_resolve(repo, remote_ref);
  if (r) {
    ADD(r);
    free(r);
  }

  return candidates;
}

char *object_find(fit_repository_t *repo, const char *name, const char *fmt,
                  bool follow) {
  int count = 0;
  char **shas = object_resolve(repo, name, &count);

  if (!shas || count == 0) {
    fprintf(stderr, "No such reference %s.\n", name);
    return NULL;
  }
  if (count > 1) {
    fprintf(stderr, "Ambiguous reference %s.\n", name);
    for (int i = 0; i < count; i++)
      free(shas[i]);
    free(shas);
    return NULL;
  }

  char *sha = shas[0];
  free(shas);

  if (!fmt)
    return sha;

  obj_type_t want = obj_type_from_str(fmt);

  while (true) {
    fit_object_t *obj = object_read(repo, sha);
    if (!obj) {
      free(sha);
      return NULL;
    }

    if (obj->type == want) {
      object_free(obj);
      return sha;
    }

    if (!follow) {
      object_free(obj);
      free(sha);
      return NULL;
    }

    if (obj->type == OBJ_TAG) {
      kvlm_t *kv = kvlm_parse(obj->raw.data, obj->raw.size);
      const char *target = kvlm_get(kv, "object");
      char *new_sha = target ? strdup(target) : NULL;
      kvlm_free(kv);
      object_free(obj);
      free(sha);
      sha = new_sha;
    } else if (obj->type == OBJ_COMMIT && want == OBJ_TREE) {
      kvlm_t *kv = kvlm_parse(obj->raw.data, obj->raw.size);
      const char *target = kvlm_get(kv, "tree");
      char *new_sha = target ? strdup(target) : NULL;
      kvlm_free(kv);
      object_free(obj);
      free(sha);
      sha = new_sha;
    } else {
      object_free(obj);
      free(sha);
      return NULL;
    }
  }
}
