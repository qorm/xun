#ifndef XUN_H
#define XUN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  XUN_STRING = 1,
  XUN_INT,
  XUN_FLOAT,
  XUN_BOOL,
  XUN_BYTES,
  XUN_TAGGED,
  XUN_LIST,
  XUN_DICT
} xun_kind;

typedef struct xun_value xun_value;

struct xun_pair {
  char *key;
  xun_value *val;
};

struct xun_value {
  xun_kind kind;
  void *arena; /* non-NULL only on the root returned by xun_parse */
  union {
    char *str;
    int64_t i;
    double f;
    int b;
    struct {
      uint8_t *data;
      size_t len;
    } bytes;
    struct {
      char *tag;
      char *value;
    } tagged;
    struct {
      xun_value **items;
      size_t len;
    } list;
    struct {
      struct xun_pair *items;
      size_t len;
    } dict;
  } u;
};

typedef struct {
  int line;
  char message[256];
} xun_error;

/* 0 on success, -1 on error. Root is always a dict. Caller must xun_free. */
int xun_parse(const char *source, xun_value **out, xun_error *err);
int xun_parse_file(const char *path, xun_value **out, xun_error *err);
void xun_free(xun_value *v);
const xun_value *xun_dict_get(const xun_value *dict, const char *key);

#ifdef __cplusplus
}
#endif

#endif
