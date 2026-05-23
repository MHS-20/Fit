#include "remote.h"
#include "objects.h"
#include "refs.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <zlib.h>

typedef struct {
  unsigned char *data;
  size_t size;
  size_t cap;
} buffer_t;

static void buf_append(buffer_t *b, const unsigned char *data, size_t len) {
  while (b->size + len > b->cap) {
    b->cap = b->cap ? b->cap * 2 : 4096;
    b->data = realloc(b->data, b->cap);
  }
  memcpy(b->data + b->size, data, len);
  b->size += len;
}

typedef struct {
  int sock;
  SSL *ssl;
  SSL_CTX *ctx;
  bool use_ssl;
} http_conn_t;

static void http_conn_free(http_conn_t *c) {
  if (c->ssl) {
    SSL_shutdown(c->ssl);
    SSL_free(c->ssl);
  }
  if (c->ctx)
    SSL_CTX_free(c->ctx);
  if (c->sock >= 0)
    close(c->sock);
}

static int http_conn_write(http_conn_t *c, const unsigned char *data,
                           size_t len) {
  if (c->use_ssl)
    return SSL_write(c->ssl, data, (int)len) > 0 ? 0 : -1;
  return send(c->sock, data, len, 0) == (ssize_t)len ? 0 : -1;
}

static int http_conn_read(http_conn_t *c, unsigned char *buf, size_t len) {
  if (c->use_ssl)
    return SSL_read(c->ssl, buf, (int)len);
  return (int)recv(c->sock, buf, len, 0);
}

static buffer_t http_do(const char *url, const char *username,
                        const char *password, const unsigned char *post_data,
                        size_t post_len, const char *content_type) {
  buffer_t result = {NULL, 0, 0};

  char scheme[16], host[256], path[MAX_PATH];
  int port = 80;

  if (sscanf(url, "%15[^:]://%255[^/]%4095s", scheme, host, path) < 2)
    return result;
  if (path[0] == '\0')
    strcpy(path, "/");

  if (strcmp(scheme, "https") == 0)
    port = 443;

  char *colon = strchr(host, ':');
  if (colon) {
    port = atoi(colon + 1);
    *colon = '\0';
  }

  struct addrinfo hints = {0}, *res;
  hints.ai_socktype = SOCK_STREAM;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);
  if (getaddrinfo(host, port_str, &hints, &res) != 0)
    return result;

  http_conn_t conn = {-1, NULL, NULL, strcmp(scheme, "https") == 0};
  conn.sock = socket(res->ai_family, res->ai_socktype, 0);
  if (conn.sock < 0) {
    freeaddrinfo(res);
    return result;
  }

  if (connect(conn.sock, res->ai_addr, res->ai_addrlen) != 0) {
    freeaddrinfo(res);
    http_conn_free(&conn);
    return result;
  }
  freeaddrinfo(res);

  if (conn.use_ssl) {
    SSL_library_init();
    SSL_load_error_strings();
    conn.ctx = SSL_CTX_new(TLS_client_method());
    conn.ssl = SSL_new(conn.ctx);
    SSL_set_fd(conn.ssl, conn.sock);
    SSL_set_tlsext_host_name(conn.ssl, host);
    if (SSL_connect(conn.ssl) != 1) {
      http_conn_free(&conn);
      return result;
    }
  }

  char auth_buf[512];
  char auth_b64[1024] = "";
  if (username && password) {
    snprintf(auth_buf, sizeof(auth_buf), "%s:%s", username, password);
    static const char *b64chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t in_len = strlen(auth_buf);
    size_t out_pos = 0;
    for (size_t i = 0; i < in_len; i += 3) {
      unsigned int val =
          ((unsigned char)auth_buf[i] << 16) |
          (i + 1 < in_len ? (unsigned char)auth_buf[i + 1] << 8 : 0) |
          (i + 2 < in_len ? (unsigned char)auth_buf[i + 2] : 0);
      auth_b64[out_pos++] = b64chars[(val >> 18) & 0x3F];
      auth_b64[out_pos++] = b64chars[(val >> 12) & 0x3F];
      auth_b64[out_pos++] =
          (i + 1 < in_len) ? b64chars[(val >> 6) & 0x3F] : '=';
      auth_b64[out_pos++] = (i + 2 < in_len) ? b64chars[val & 0x3F] : '=';
    }
    auth_b64[out_pos] = '\0';
  }

  char req_hdr[4096];
  int req_len = snprintf(
      req_hdr, sizeof(req_hdr),
      "%s %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Authorization: Basic %s\r\n"
      "%s%s%s"
      "Content-Length: %zu\r\n"
      "Connection: close\r\n"
      "\r\n",
      post_data ? "POST" : "GET", path, host, auth_b64,
      content_type ? "Content-Type: " : "", content_type ? content_type : "",
      content_type ? "\r\n" : "", post_data ? post_len : (size_t)0);

  http_conn_write(&conn, (unsigned char *)req_hdr, req_len);
  if (post_data)
    http_conn_write(&conn, post_data, post_len);

  buffer_t raw = {NULL, 0, 0};
  unsigned char rbuf[4096];
  int n;
  while ((n = http_conn_read(&conn, rbuf, sizeof(rbuf))) > 0) {
    buf_append(&raw, rbuf, n);
  }
  http_conn_free(&conn);

  if (!raw.data)
    return result;

  char *header_end = strstr((char *)raw.data, "\r\n\r\n");
  if (!header_end) {
    free(raw.data);
    return result;
  }

  size_t hdr_len = (header_end - (char *)raw.data) + 4;
  size_t body_len = raw.size - hdr_len;

  result.data = malloc(body_len + 1);
  memcpy(result.data, raw.data + hdr_len, body_len);
  result.data[body_len] = '\0';
  result.size = body_len;
  result.cap = body_len + 1;

  free(raw.data);
  return result;
}

typedef struct {
  char **lines;
  size_t count;
} pkt_lines_t;

static pkt_lines_t pkt_read_lines(const unsigned char *data, size_t len) {
  pkt_lines_t result = {NULL, 0};
  size_t cap = 16;
  result.lines = malloc(cap * sizeof(char *));

  size_t i = 0;
  while (i + 4 <= len) {
    char hex[5];
    memcpy(hex, data + i, 4);
    hex[4] = '\0';

    unsigned int line_len;
    sscanf(hex, "%04x", &line_len);

    if (line_len == 0) {
      if (result.count >= cap) {
        cap *= 2;
        result.lines = realloc(result.lines, cap * sizeof(char *));
      }
      result.lines[result.count++] = strdup("");
      i += 4;
      continue;
    }

    if (line_len < 4 || i + line_len > len)
      break;
    size_t payload = line_len - 4;

    char *line = malloc(payload + 1);
    memcpy(line, data + i + 4, payload);
    line[payload] = '\0';

    if (result.count >= cap) {
      cap *= 2;
      result.lines = realloc(result.lines, cap * sizeof(char *));
    }
    result.lines[result.count++] = line;
    i += line_len;
  }

  return result;
}

static buffer_t pkt_build_lines(char **lines, size_t count) {
  buffer_t out = {NULL, 0, 0};

  for (size_t i = 0; i < count; i++) {
    size_t payload = strlen(lines[i]);
    size_t total = payload + 5;
    char len_hex[5];
    snprintf(len_hex, sizeof(len_hex), "%04zx", total);
    buf_append(&out, (unsigned char *)len_hex, 4);
    buf_append(&out, (unsigned char *)lines[i], payload);
    buf_append(&out, (unsigned char *)"\n", 1);
  }
  buf_append(&out, (unsigned char *)"0000", 4);
  return out;
}

static void pkt_lines_free(pkt_lines_t *pl) {
  for (size_t i = 0; i < pl->count; i++)
    free(pl->lines[i]);
  free(pl->lines);
}

char *get_remote_master_hash(const char *git_url, const char *username,
                             const char *password) {
  char url[MAX_PATH * 2];
  snprintf(url, sizeof(url), "%s/info/refs?service=git-receive-pack", git_url);

  buffer_t resp = http_do(url, username, password, NULL, 0, NULL);
  if (!resp.data)
    return NULL;

  pkt_lines_t lines = pkt_read_lines(resp.data, resp.size);
  free(resp.data);

  if (lines.count < 3) {
    pkt_lines_free(&lines);
    return NULL;
  }

  char *first = lines.lines[2];
  char sha[41] = {0}, ref[256] = {0};

  char *cap_sep = strchr(first, '\0');
  if (cap_sep) {
    char tmp[512];
    size_t tlen = cap_sep - first;
    if (tlen >= sizeof(tmp))
      tlen = sizeof(tmp) - 1;
    strncpy(tmp, first, tlen);
    tmp[tlen] = '\0';
    sscanf(tmp, "%40s %255s", sha, ref);
  } else {
    sscanf(first, "%40s %255s", sha, ref);
  }

  bool is_zero = true;
  for (int i = 0; i < 40; i++)
    if (sha[i] != '0') {
      is_zero = false;
      break;
    }
  if (is_zero) {
    pkt_lines_free(&lines);
    return NULL;
  }

  if (strcmp(ref, "refs/heads/master") == 0 ||
      strcmp(ref, "refs/heads/main") == 0) {
    pkt_lines_free(&lines);
    return strdup(sha);
  }

  for (size_t i = 3; i < lines.count; i++) {
    if (!lines.lines[i][0])
      continue;
    char lsha[41] = {0}, lref[256] = {0};
    sscanf(lines.lines[i], "%40s %255s", lsha, lref);
    if (strcmp(lref, "refs/heads/master") == 0 ||
        strcmp(lref, "refs/heads/main") == 0) {
      pkt_lines_free(&lines);
      return strdup(lsha);
    }
  }

  pkt_lines_free(&lines);
  return NULL;
}

char *get_local_master_hash(fit_repository_t *repo) {
  char *sha = ref_resolve(repo, "refs/heads/master");
  if (!sha)
    sha = ref_resolve(repo, "refs/heads/main");
  return sha;
}

static void collect_tree_objects(fit_repository_t *repo, const char *tree_sha,
                                 char ***seen, size_t *count, size_t *cap) {
  for (size_t i = 0; i < *count; i++)
    if (strcmp((*seen)[i], tree_sha) == 0)
      return;

  if (*count >= *cap) {
    *cap *= 2;
    *seen = realloc(*seen, *cap * sizeof(char *));
  }
  (*seen)[(*count)++] = strdup(tree_sha);

  fit_object_t *tree = object_read(repo, tree_sha);
  if (!tree)
    return;

  for (size_t i = 0; i < tree->tree.count; i++) {
    tree_leaf_t *leaf = &tree->tree.items[i];
    if (strncmp(leaf->mode, "04", 2) == 0) {
      collect_tree_objects(repo, leaf->sha, seen, count, cap);
    } else {
      bool found = false;
      for (size_t j = 0; j < *count; j++)
        if (strcmp((*seen)[j], leaf->sha) == 0) {
          found = true;
          break;
        }
      if (!found) {
        if (*count >= *cap) {
          *cap *= 2;
          *seen = realloc(*seen, *cap * sizeof(char *));
        }
        (*seen)[(*count)++] = strdup(leaf->sha);
      }
    }
  }
  object_free(tree);
}

static void collect_commit_objects(fit_repository_t *repo,
                                   const char *commit_sha, char ***seen,
                                   size_t *count, size_t *cap) {
  for (size_t i = 0; i < *count; i++)
    if (strcmp((*seen)[i], commit_sha) == 0)
      return;

  if (*count >= *cap) {
    *cap *= 2;
    *seen = realloc(*seen, *cap * sizeof(char *));
  }
  (*seen)[(*count)++] = strdup(commit_sha);

  fit_object_t *commit = object_read(repo, commit_sha);
  if (!commit || commit->type != OBJ_COMMIT) {
    if (commit)
      object_free(commit);
    return;
  }

  kvlm_t *kv = kvlm_parse(commit->raw.data, commit->raw.size);

  const char *tree_sha = kvlm_get(kv, "tree");
  if (tree_sha)
    collect_tree_objects(repo, tree_sha, seen, count, cap);

  kvlm_entry_t *e = kv->head;
  while (e) {
    if (strcmp(e->key, "parent") == 0)
      collect_commit_objects(repo, (char *)e->value, seen, count, cap);
    e = (kvlm_entry_t *)e->next;
  }

  kvlm_free(kv);
  object_free(commit);
}

static unsigned char *encode_pack_object(fit_repository_t *repo,
                                         const char *sha, size_t *out_len) {
  fit_object_t *obj = object_read(repo, sha);
  if (!obj) {
    *out_len = 0;
    return NULL;
  }

  unsigned char *raw_data = NULL;
  size_t raw_size = 0;

  if (obj->type == OBJ_BLOB) {
    raw_data = obj->blob.data;
    raw_size = obj->blob.size;
  } else if (obj->type == OBJ_COMMIT || obj->type == OBJ_TAG) {
    raw_data = obj->raw.data;
    raw_size = obj->raw.size;
  } else if (obj->type == OBJ_TREE) {
    size_t sz;
    char *tmp_sha = object_write(obj, NULL);
    (void)tmp_sha;
    free(tmp_sha);
    raw_data = NULL;
    raw_size = 0;
  }

  int type_num;
  switch (obj->type) {
  case OBJ_COMMIT:
    type_num = 1;
    break;
  case OBJ_TREE:
    type_num = 2;
    break;
  case OBJ_BLOB:
    type_num = 3;
    break;
  case OBJ_TAG:
    type_num = 4;
    break;
  default:
    type_num = 3;
  }

  if (!raw_data || raw_size == 0) {
    object_free(obj);
    *out_len = 0;
    return NULL;
  }

  size_t size = raw_size;
  unsigned char hdr[16];
  int hdr_len = 0;
  unsigned char byte = (unsigned char)((type_num << 4) | (size & 0x0F));
  size >>= 4;
  while (size) {
    hdr[hdr_len++] = byte | 0x80;
    byte = size & 0x7F;
    size >>= 7;
  }
  hdr[hdr_len++] = byte;

  uLongf comp_size = compressBound(raw_size);
  unsigned char *comp = malloc(comp_size);
  compress(comp, &comp_size, raw_data, raw_size);

  *out_len = hdr_len + comp_size;
  unsigned char *result = malloc(*out_len);
  memcpy(result, hdr, hdr_len);
  memcpy(result + hdr_len, comp, comp_size);
  free(comp);

  object_free(obj);
  return result;
}

static buffer_t create_pack(fit_repository_t *repo, char **objects,
                            size_t obj_count) {
  buffer_t pack = {NULL, 0, 0};

  unsigned char pack_hdr[12];
  memcpy(pack_hdr, "PACK", 4);
  uint32_t ver = htonl(2);
  uint32_t cnt = htonl((uint32_t)obj_count);
  memcpy(pack_hdr + 4, &ver, 4);
  memcpy(pack_hdr + 8, &cnt, 4);
  buf_append(&pack, pack_hdr, 12);

  for (size_t i = 0; i < obj_count; i++) {
    size_t obj_len;
    unsigned char *obj_data = encode_pack_object(repo, objects[i], &obj_len);
    if (!obj_data || obj_len == 0) {
      free(obj_data);
      continue;
    }
    buf_append(&pack, obj_data, obj_len);
    free(obj_data);
  }

  unsigned char checksum[20];
  SHA1(pack.data, pack.size, checksum);
  buf_append(&pack, checksum, 20);

  return pack;
}

int push(fit_repository_t *repo, const char *git_url, const char *username,
         const char *password) {
  char url_buf[MAX_PATH * 2];
  snprintf(url_buf, sizeof(url_buf), "%s", git_url);
  size_t ulen = strlen(url_buf);
  while (ulen > 0 && url_buf[ulen - 1] == '/')
    url_buf[--ulen] = '\0';

  char *local_sha = get_local_master_hash(repo);
  if (!local_sha) {
    fprintf(stderr, "Nothing to push: no local commits on master/main.\n");
    return FIT_ERROR;
  }

  printf("Fetching remote state from %s ...\n", url_buf);
  char *remote_sha = get_remote_master_hash(url_buf, username, password);

  if (remote_sha && strcmp(remote_sha, local_sha) == 0) {
    printf("Already up to date.\n");
    free(local_sha);
    free(remote_sha);
    return FIT_OK;
  }

  printf("Local  : %s\n", local_sha);
  printf("Remote : %s\n", remote_sha ? remote_sha : "(empty)");

  size_t local_cap = 256;
  char **local_objs = malloc(local_cap * sizeof(char *));
  size_t local_count = 0;
  collect_commit_objects(repo, local_sha, &local_objs, &local_count,
                         &local_cap);

  char **missing = NULL;
  size_t miss_count = 0;

  if (remote_sha) {
    size_t remote_cap = 256;
    char **remote_objs = malloc(remote_cap * sizeof(char *));
    size_t remote_count = 0;
    collect_commit_objects(repo, remote_sha, &remote_objs, &remote_count,
                           &remote_cap);

    size_t miss_cap = 64;
    missing = malloc(miss_cap * sizeof(char *));
    for (size_t i = 0; i < local_count; i++) {
      bool found = false;
      for (size_t j = 0; j < remote_count; j++)
        if (strcmp(local_objs[i], remote_objs[j]) == 0) {
          found = true;
          break;
        }
      if (!found) {
        if (miss_count >= miss_cap) {
          miss_cap *= 2;
          missing = realloc(missing, miss_cap * sizeof(char *));
        }
        missing[miss_count++] = local_objs[i];
      }
    }
    for (size_t i = 0; i < remote_count; i++)
      free(remote_objs[i]);
    free(remote_objs);
  } else {
    missing = local_objs;
    miss_count = local_count;
    local_objs = NULL;
  }

  printf("Packing %zu object(s) ...\n", miss_count);

  char *branch = ref_resolve(repo, "refs/heads/master") ? "master" : "main";
  char remote_ref[256];
  snprintf(remote_ref, sizeof(remote_ref), "refs/heads/%s", branch);

  char old_sha[41];
  if (remote_sha) {
    strncpy(old_sha, remote_sha, 40);
    old_sha[40] = '\0';
  } else {
    memset(old_sha, '0', 40);
    old_sha[40] = '\0';
  }

  char ref_line[512];
  snprintf(ref_line, sizeof(ref_line), "%s %s %s\x00 report-status", old_sha,
           local_sha, remote_ref);

  char *ref_lines[1] = {ref_line};
  buffer_t pkt = pkt_build_lines(ref_lines, 1);
  buffer_t pack = create_pack(repo, missing, miss_count);

  size_t body_len = pkt.size + pack.size;
  unsigned char *body = malloc(body_len);
  memcpy(body, pkt.data, pkt.size);
  memcpy(body + pkt.size, pack.data, pack.size);
  free(pkt.data);
  free(pack.data);

  printf("Pushing ...\n");

  char push_url[MAX_PATH * 2];
  snprintf(push_url, sizeof(push_url), "%s/git-receive-pack", url_buf);

  buffer_t resp = http_do(push_url, username, password, body, body_len,
                          "application/x-git-receive-pack-request");
  free(body);

  if (!resp.data) {
    fprintf(stderr, "Push failed: no response.\n");
    free(local_sha);
    free(remote_sha);
    return FIT_ERROR;
  }

  pkt_lines_t lines = pkt_read_lines(resp.data, resp.size);
  free(resp.data);

  int ret = FIT_ERROR;
  for (size_t i = 0; i < lines.count; i++) {
    if (!lines.lines[i][0])
      continue;
    if (strncmp(lines.lines[i], "unpack ok", 9) == 0) {
      printf("Pushed %.7s -> %s\n", local_sha, remote_ref);
      ret = FIT_OK;
    } else {
      fprintf(stderr, "Push failed: %s\n", lines.lines[i]);
    }
    break;
  }
  pkt_lines_free(&lines);

  if (local_objs) {
    for (size_t i = 0; i < local_count; i++)
      free(local_objs[i]);
    free(local_objs);
  } else {
    for (size_t i = 0; i < miss_count; i++)
      free(missing[i]);
    free(missing);
  }

  free(local_sha);
  free(remote_sha);
  return ret;
}
