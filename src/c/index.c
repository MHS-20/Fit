#include "index.h"
#include <arpa/inet.h>

static uint32_t read_u32(const unsigned char *buf) {
  uint32_t v;
  memcpy(&v, buf, 4);
  return ntohl(v);
}

static uint16_t read_u16(const unsigned char *buf) {
  uint16_t v;
  memcpy(&v, buf, 2);
  return ntohs(v);
}

static void write_u32(unsigned char *buf, uint32_t v) {
  uint32_t n = htonl(v);
  memcpy(buf, &n, 4);
}

static void write_u16(unsigned char *buf, uint16_t v) {
  uint16_t n = htons(v);
  memcpy(buf, &n, 2);
}

fit_index_t *index_read(fit_repository_t *repo) {
  char *index_path = repo_file(repo, false, "index", NULL);

  fit_index_t *idx = calloc(1, sizeof(fit_index_t));
  idx->version = 2;

  if (!index_path)
    return idx;

  struct stat st;
  if (stat(index_path, &st) != 0) {
    free(index_path);
    return idx;
  }

  FILE *fp = fopen(index_path, "rb");
  free(index_path);
  if (!fp)
    return idx;

  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  unsigned char *raw = malloc(fsize);
  fread(raw, 1, fsize, fp);
  fclose(fp);

  if (memcmp(raw, "DIRC", 4) != 0) {
    free(raw);
    return idx;
  }

  idx->version = read_u32(raw + 4);
  uint32_t count = read_u32(raw + 8);

  idx->entries = calloc(count, sizeof(fit_index_entry_t));

  size_t pos = 12;
  for (uint32_t i = 0; i < count && pos + 62 <= (size_t)fsize; i++) {
    fit_index_entry_t *e = &idx->entries[idx->count];

    e->ctime_s = read_u32(raw + pos);
    e->ctime_ns = read_u32(raw + pos + 4);
    e->mtime_s = read_u32(raw + pos + 8);
    e->mtime_ns = read_u32(raw + pos + 12);
    e->dev = read_u32(raw + pos + 16);
    e->ino = read_u32(raw + pos + 20);

    uint32_t mode = read_u32(raw + pos + 24);
    e->mode_type = (uint16_t)(mode >> 12);
    e->mode_perms = (uint16_t)(mode & 0x1FF);

    e->uid = read_u32(raw + pos + 28);
    e->gid = read_u32(raw + pos + 32);
    e->fsize = read_u32(raw + pos + 36);

    for (int j = 0; j < 20; j++)
      snprintf(e->sha + j * 2, 3, "%02x", raw[pos + 40 + j]);
    e->sha[40] = '\0';

    uint16_t flags = read_u16(raw + pos + 60);
    e->flag_assume_valid = (flags & 0x8000) != 0;
    e->flag_stage = flags & 0x3000;
    uint16_t name_length = flags & 0x0FFF;

    pos += 62;

    const unsigned char *name_start = raw + pos;
    size_t actual_len;
    if (name_length < 0xFFF) {
      actual_len = name_length;
    } else {
      const unsigned char *null_pos =
          memchr(raw + pos + 0xFFF, 0, fsize - pos - 0xFFF);
      actual_len = null_pos ? (size_t)(null_pos - (raw + pos)) : 0;
    }

    e->name = malloc(actual_len + 1);
    memcpy(e->name, name_start, actual_len);
    e->name[actual_len] = '\0';

    pos += actual_len + 1;
    pos = ((pos + 7) / 8) * 8;

    idx->count++;
  }

  free(raw);
  return idx;
}

int index_write(fit_repository_t *repo, fit_index_t *index) {
  char *index_path = repo_file(repo, false, "index", NULL);
  if (!index_path)
    return FIT_ERROR;

  FILE *fp = fopen(index_path, "wb");
  free(index_path);
  if (!fp)
    return FIT_ERROR;

  unsigned char hdr[12];
  memcpy(hdr, "DIRC", 4);
  write_u32(hdr + 4, index->version);
  write_u32(hdr + 8, (uint32_t)index->count);
  fwrite(hdr, 1, 12, fp);

  for (size_t i = 0; i < index->count; i++) {
    fit_index_entry_t *e = &index->entries[i];
    unsigned char buf[62];

    write_u32(buf, e->ctime_s);
    write_u32(buf + 4, e->ctime_ns);
    write_u32(buf + 8, e->mtime_s);
    write_u32(buf + 12, e->mtime_ns);
    write_u32(buf + 16, e->dev);
    write_u32(buf + 20, e->ino);

    uint32_t mode = ((uint32_t)e->mode_type << 12) | e->mode_perms;
    write_u32(buf + 24, mode);

    write_u32(buf + 28, e->uid);
    write_u32(buf + 32, e->gid);
    write_u32(buf + 36, e->fsize);

    for (int j = 0; j < 20; j++) {
      unsigned int byte;
      sscanf(e->sha + j * 2, "%02x", &byte);
      buf[40 + j] = (unsigned char)byte;
    }

    size_t name_len = strlen(e->name);
    uint16_t flag_name_len = (name_len >= 0xFFF) ? 0xFFF : (uint16_t)name_len;
    uint16_t flags =
        (e->flag_assume_valid ? 0x8000 : 0) | e->flag_stage | flag_name_len;
    write_u16(buf + 60, flags);

    fwrite(buf, 1, 62, fp);
    fwrite(e->name, 1, name_len, fp);

    unsigned char null_byte = 0;
    fwrite(&null_byte, 1, 1, fp);

    size_t entry_len = 62 + name_len + 1;
    size_t pad = (8 - (entry_len % 8)) % 8;
    for (size_t p = 0; p < pad; p++)
      fwrite(&null_byte, 1, 1, fp);
  }

  fclose(fp);
  return FIT_OK;
}

void index_free(fit_index_t *index) {
  if (!index)
    return;
  for (size_t i = 0; i < index->count; i++)
    free(index->entries[i].name);
  free(index->entries);
  free(index);
}
