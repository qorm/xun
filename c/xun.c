#include "xun.h"

#include <ctype.h>
#include <regex.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BYTES (1024 * 1024)
#define MAX_DEPTH 64

typedef struct {
  void **p;
  size_t n, cap;
} arena;

typedef struct {
  char *raw;
  int indent;
  char *text;
  char *code;
  int n;
  int blank;
} line;

typedef struct {
  jmp_buf jmp;
  xun_error *err;
  arena *a;
  line *lines;
  size_t nlines;
  size_t i;
  /* RFC-0001: stack of open blocks for 'end' validation.
     Each entry records the indent where the block began and the
     expected key name (NULL for root or anonymous blocks). */
  int *open_blocks_indent;
  char **open_blocks_key;
  int open_blocks_count;
  int open_blocks_cap;
} parser;

static void fail(parser *p, int line_no, const char *fmt, ...) {
  if (p->err) {
    p->err->line = line_no;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->err->message, sizeof p->err->message, fmt, ap);
    va_end(ap);
  }
  longjmp(p->jmp, 1);
}

static void *areq(parser *p, size_t n) {
  void *x = calloc(1, n);
  if (!x) fail(p, 0, "out of memory");
  if (p->a->n == p->a->cap) {
    size_t cap = p->a->cap ? p->a->cap * 2 : 32;
    void **np = realloc(p->a->p, cap * sizeof(void *));
    if (!np) fail(p, 0, "out of memory");
    p->a->p = np;
    p->a->cap = cap;
  }
  p->a->p[p->a->n++] = x;
  return x;
}

static char *astrdup(parser *p, const char *s) {
  size_t n = strlen(s);
  char *d = areq(p, n + 1);
  memcpy(d, s, n + 1);
  return d;
}

static char *astrndup(parser *p, const char *s, size_t n) {
  char *d = areq(p, n + 1);
  memcpy(d, s, n);
  d[n] = 0;
  return d;
}

static int fullmatch(const char *pat, const char *s) {
  regex_t re;
  if (regcomp(&re, pat, REG_EXTENDED | REG_NOSUB) != 0) return 0;
  int r = regexec(&re, s, 0, NULL, 0);
  regfree(&re);
  return r == 0;
}

static int starts_with(const char *s, const char *pfx) {
  return strncmp(s, pfx, strlen(pfx)) == 0;
}

static int is_ident(const char *s) {
  return fullmatch("^[A-Za-z_][A-Za-z0-9_]*$", s);
}

static char *parse_quoted_prefix(parser *p, const char *raw, int line_no, size_t *out_end);

static line *peek(parser *p) {
  return p->i < p->nlines ? &p->lines[p->i] : NULL;
}

static void skip_noise(parser *p) {
  while (peek(p)) {
    line *l = peek(p);
    if (l->blank || l->code[0] == 0) p->i++;
    else break;
  }
}

static int is_list_item(line *l) {
  return strcmp(l->code, "-") == 0 || starts_with(l->code, "- ");
}

/* RFC-0001: stack of open blocks for 'end' validation. */
static void push_open_block(parser *p, int indent, const char *key) {
  if (p->open_blocks_count == p->open_blocks_cap) {
    int cap = p->open_blocks_cap ? p->open_blocks_cap * 2 : 8;
    int *ni = realloc(p->open_blocks_indent, cap * sizeof(int));
    if (!ni) fail(p, 0, "out of memory");
    p->open_blocks_indent = ni;
    char **nk = realloc(p->open_blocks_key, cap * sizeof(char *));
    if (!nk) fail(p, 0, "out of memory");
    p->open_blocks_key = nk;
    p->open_blocks_cap = cap;
  }
  p->open_blocks_indent[p->open_blocks_count] = indent;
  p->open_blocks_key[p->open_blocks_count] = key ? astrdup(p, key) : NULL;
  p->open_blocks_count++;
}

static void pop_open_block(parser *p) {
  if (p->open_blocks_count > 0) p->open_blocks_count--;
}

/* RFC-0001: try to consume an 'end' or 'end <key>' statement at the given indent.
   Returns 1 if consumed, 0 if the next line is not an 'end' statement.
   When the stored expected_key is non-NULL and a different key is supplied,
   the parser fails with an end-key mismatch error. */
static int try_consume_end(parser *p, int indent, const char *expected_key) {
  line *l = peek(p);
  if (!l || l->blank || l->indent != indent) return 0;
  const char *code = l->code;
  if (strcmp(code, "end") == 0) {
    p->i++;
    return 1;
  }
  /* "end <key>" — exactly one whitespace-separated identifier after 'end'. */
  if (starts_with(code, "end ") && code[4] && code[4] != ' ' && code[4] != '\t') {
    const char *rest = code + 4;
    const char *space = strchr(rest, ' ');
    const char *tab = strchr(rest, '\t');
    if (!space || (tab && tab < space)) space = tab;
    size_t klen = space ? (size_t)(space - rest) : strlen(rest);
    if (klen == 0) return 0;
    /* The remainder after the key must be blank (only trailing ws). */
    if (space) {
      const char *after = space + 1;
      while (*after == ' ' || *after == '\t') after++;
      if (*after) return 0;
    }
    char *end_key = astrndup(p, rest, klen);
    if (expected_key && strcmp(expected_key, end_key) != 0) {
      fail(p, l->n, "end-key mismatch: expected '%s', got '%s'", expected_key, end_key);
    }
    p->i++;
    return 1;
  }
  return 0;
}

/* RFC-0001: detect whether the current line is an 'end' or 'end <key>'
   statement at the given indent (without consuming). */
static int line_is_end_at(parser *p, int indent) {
  line *l = peek(p);
  if (!l || l->blank || l->indent != indent) return 0;
  const char *code = l->code;
  if (strcmp(code, "end") == 0) return 1;
  if (!starts_with(code, "end ")) return 0;
  const char *rest = code + 4;
  if (!rest[0] || rest[0] == ' ' || rest[0] == '\t') return 0;
  const char *space = strchr(rest, ' ');
  const char *tab = strchr(rest, '\t');
  if (!space || (tab && tab < space)) space = tab;
  size_t klen = space ? (size_t)(space - rest) : strlen(rest);
  if (klen == 0) return 0;
  if (space) {
    const char *after = space + 1;
    while (*after == ' ' || *after == '\t') after++;
    if (*after) return 0;
  }
  return 1;
}

static char *rstrip_space_tab(parser *p, const char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
  return astrndup(p, s, n);
}

static int leading_spaces(const char *s) {
  int i = 0;
  while (s[i] == ' ') i++;
  return i;
}

/* Strip a trailing ` # ...` comment outside of quoted strings. */
static char *strip_trailing_comment(parser *p, const char *s) {
  int in_quote = 0;
  for (size_t i = 0; s[i]; i++) {
    char ch = s[i];
    if (in_quote) {
      if (ch == '\\') i++;
      else if (ch == '"') in_quote = 0;
      continue;
    }
    if (ch == '"') {
      in_quote = 1;
      continue;
    }
    if (ch == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
      size_t n = i;
      while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
      return astrndup(p, s, n);
    }
  }
  return astrdup(p, s);
}

static xun_value *vnew(parser *p, xun_kind k) {
  xun_value *v = areq(p, sizeof(xun_value));
  v->kind = k;
  return v;
}

static xun_value *vstr(parser *p, const char *s) {
  xun_value *v = vnew(p, XUN_STRING);
  v->u.str = astrdup(p, s);
  return v;
}

static xun_value *vint(parser *p, int64_t i) {
  xun_value *v = vnew(p, XUN_INT);
  v->u.i = i;
  return v;
}

static xun_value *vfloat(parser *p, double f) {
  xun_value *v = vnew(p, XUN_FLOAT);
  v->u.f = f;
  return v;
}

static xun_value *vbool(parser *p, int b) {
  xun_value *v = vnew(p, XUN_BOOL);
  v->u.b = b;
  return v;
}

static xun_value *vtagged(parser *p, const char *tag, const char *value) {
  xun_value *v = vnew(p, XUN_TAGGED);
  v->u.tagged.tag = astrdup(p, tag);
  v->u.tagged.value = astrdup(p, value);
  return v;
}

static xun_value *vlist(parser *p) {
  return vnew(p, XUN_LIST);
}

static xun_value *vdict(parser *p) {
  return vnew(p, XUN_DICT);
}

static void list_push(parser *p, xun_value *arr, xun_value *item) {
  xun_value **nitems = calloc(arr->u.list.len + 1, sizeof(xun_value *));
  if (!nitems) fail(p, 0, "out of memory");
  if (p->a->n == p->a->cap) {
    size_t cap = p->a->cap ? p->a->cap * 2 : 32;
    void **np = realloc(p->a->p, cap * sizeof(void *));
    if (!np) fail(p, 0, "out of memory");
    p->a->p = np;
    p->a->cap = cap;
  }
  p->a->p[p->a->n++] = nitems;
  if (arr->u.list.items) memcpy(nitems, arr->u.list.items, arr->u.list.len * sizeof(xun_value *));
  nitems[arr->u.list.len] = item;
  arr->u.list.items = nitems;
  arr->u.list.len++;
}

static void dict_put(parser *p, xun_value *obj, const char *key, xun_value *val) {
  struct xun_pair *npairs = calloc(obj->u.dict.len + 1, sizeof(struct xun_pair));
  if (!npairs) fail(p, 0, "out of memory");
  if (p->a->n == p->a->cap) {
    size_t cap = p->a->cap ? p->a->cap * 2 : 32;
    void **np = realloc(p->a->p, cap * sizeof(void *));
    if (!np) fail(p, 0, "out of memory");
    p->a->p = np;
    p->a->cap = cap;
  }
  p->a->p[p->a->n++] = npairs;
  if (obj->u.dict.items) memcpy(npairs, obj->u.dict.items, obj->u.dict.len * sizeof(struct xun_pair));
  npairs[obj->u.dict.len].key = astrdup(p, key);
  npairs[obj->u.dict.len].val = val;
  obj->u.dict.items = npairs;
  obj->u.dict.len++;
}

static int dict_has(xun_value *obj, const char *key) {
  for (size_t i = 0; i < obj->u.dict.len; i++) {
    if (strcmp(obj->u.dict.items[i].key, key) == 0) return 1;
  }
  return 0;
}

const xun_value *xun_dict_get(const xun_value *dict, const char *key) {
  if (!dict || dict->kind != XUN_DICT) return NULL;
  for (size_t i = 0; i < dict->u.dict.len; i++) {
    if (strcmp(dict->u.dict.items[i].key, key) == 0) return dict->u.dict.items[i].val;
  }
  return NULL;
}

static char *glyph_of(parser *p, xun_value *v, int line_no);
static xun_value *apply_tag(parser *p, const char *tag, const char *glyph, int n);
static xun_value *parse_value(parser *p, const char *raw, int parent_indent, int line_no, int depth, const char *value_key);
static char *parse_quoted_string(parser *p, const char *raw, int line_no);
static char *parse_string_body(parser *p, const char *body, int line_no);
static xun_value *parse_dict(parser *p, int indent, int depth, const char *dict_key);
static xun_value *parse_list(parser *p, int indent, int depth, const char *item_tag, const char *parent_key);
static xun_value *parse_empty_or_nested(parser *p, int parent_indent, int line_no, int depth, const char *item_tag, const char *value_key);
static xun_value *parse_tagged(parser *p, const char *raw, int parent_indent, int line_no, int depth, const char *value_key);
static xun_value *parse_inline_dict(parser *p, const char *text, int line_no);
static void split_top_level_commas(parser *p, const char *inner, char ***out_parts, size_t *out_count);

static char *strip_underscores(parser *p, const char *s, int n) {
  if (strstr(s, "__") || s[0] == '_' || (s[0] && s[strlen(s) - 1] == '_')) {
    fail(p, n, "invalid numeric underscores");
  }
  size_t len = strlen(s);
  char *out = areq(p, len + 1);
  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] != '_') out[j++] = s[i];
  }
  out[j] = 0;
  return out;
}

static int leading_zero_int(const char *s) {
  if (s[0] == '-') s++;
  return s[0] == '0' && s[1] && isdigit((unsigned char)s[1]);
}

static xun_value *parse_n(parser *p, const char *g, int n) {
  char *s = strip_underscores(p, g, n);
  if (leading_zero_int(s)) fail(p, n, "leading zeros are not allowed");
  if (fullmatch("^-?[0-9]+$", s)) {
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (!end || *end) fail(p, n, "invalid number");
    return vint(p, v);
  }
  if (fullmatch("^-?[0-9]+\\.[0-9]+([eE][+-]?[0-9]+)?$", s) || fullmatch("^-?[0-9]+[eE][+-]?[0-9]+$", s)) {
    return vfloat(p, strtod(s, NULL));
  }
  fail(p, n, "invalid number");
  return NULL;
}

static xun_value *parse_i(parser *p, const char *g, int n) {
  char *s = strip_underscores(p, g, n);
  if (!fullmatch("^-?[0-9]+$", s)) fail(p, n, "invalid integer");
  if (leading_zero_int(s)) fail(p, n, "leading zeros are not allowed");
  return vint(p, strtoll(s, NULL, 10));
}

static xun_value *parse_f(parser *p, const char *g, int n) {
  char *s = strip_underscores(p, g, n);
  if (!strchr(s, '.') && !strchr(s, 'e') && !strchr(s, 'E')) fail(p, n, "float must contain '.' or 'e'");
  return vfloat(p, strtod(s, NULL));
}

static xun_value *parse_unix(parser *p, const char *g, int n) {
  char *s = strip_underscores(p, g, n);
  if (leading_zero_int(s)) fail(p, n, "leading zeros are not allowed");
  if (fullmatch("^-?[0-9]+$", s)) return vint(p, strtoll(s, NULL, 10));
  if (fullmatch("^-?[0-9]+\\.[0-9]+$", s)) return vfloat(p, strtod(s, NULL));
  fail(p, n, "invalid unix timestamp");
  return NULL;
}

/* Duration grammar: (?:(\d+)d)?(?:(\d+)h)?(?:(\d+)m)?(?:(\d+(?:\.\d+)?)s)?(?:(\d+(?:\.\d+)?)ms)?
   at least one component required; order is d, h, m, s, ms. Returns seconds as double. */
static int parse_duration_seconds(const char *s, double *out) {
  if (!s || !*s || !out) return -1;
  double total = 0.0;
  int matched = 0;
  const char *p = s;

  /* (\d+d)? */
  {
    const char *st = p;
    while (*p >= '0' && *p <= '9') p++;
    if (p > st && *p == 'd') {
      total += strtod(st, NULL) * 86400.0;
      matched = 1;
      p++;
    } else {
      p = st;
    }
  }
  /* (\d+h)? */
  {
    const char *st = p;
    while (*p >= '0' && *p <= '9') p++;
    if (p > st && *p == 'h') {
      total += strtod(st, NULL) * 3600.0;
      matched = 1;
      p++;
    } else {
      p = st;
    }
  }
  /* (\d+m)? — do not steal the `m` of a following `ms` */
  {
    const char *st = p;
    while (*p >= '0' && *p <= '9') p++;
    if (p > st && *p == 'm' && p[1] != 's') {
      total += strtod(st, NULL) * 60.0;
      matched = 1;
      p++;
    } else {
      p = st;
    }
  }
  /* (\d+(?:\.\d+)?s)? */
  {
    const char *st = p;
    while (*p >= '0' && *p <= '9') p++;
    if (*p == '.' && p[1] >= '0' && p[1] <= '9') {
      p++;
      while (*p >= '0' && *p <= '9') p++;
    }
    if (p > st && *p == 's') {
      total += strtod(st, NULL);
      matched = 1;
      p++;
    } else {
      p = st;
    }
  }
  /* (\d+(?:\.\d+)?ms)? */
  {
    const char *st = p;
    while (*p >= '0' && *p <= '9') p++;
    if (*p == '.' && p[1] >= '0' && p[1] <= '9') {
      p++;
      while (*p >= '0' && *p <= '9') p++;
    }
    if (p > st && p[0] == 'm' && p[1] == 's') {
      total += strtod(st, NULL) / 1000.0;
      matched = 1;
      p += 2;
    } else {
      p = st;
    }
  }
  if (!matched || *p) return -1;
  *out = total;
  return 0;
}

static int is_ip(const char *s) {
  if (fullmatch("^[0-9]{1,3}(\\.[0-9]{1,3}){3}$", s)) {
    int a, b, c, d;
    if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255) return 0;
    char buf[32];
    snprintf(buf, sizeof buf, "%d.%d.%d.%d", a, b, c, d);
    return strcmp(buf, s) == 0;
  }
  if (strchr(s, ':')) {
    if (strchr(s, '.')) return 0;
    int parts = 0, empty = 0;
    const char *cur = s;
    while (1) {
      const char *col = strchr(cur, ':');
      size_t n = col ? (size_t)(col - cur) : strlen(cur);
      parts++;
      if (n == 0) empty++;
      else {
        if (n > 4) return 0;
        for (size_t i = 0; i < n; i++) {
          if (!isxdigit((unsigned char)cur[i])) return 0;
        }
      }
      if (!col) break;
      cur = col + 1;
    }
    return parts <= 8 && empty <= 2;
  }
  return 0;
}

static int b64_val(int c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static xun_value *parse_b64(parser *p, const char *g, int n) {
  size_t len = strlen(g);
  char *s = areq(p, len + 1);
  size_t sl = 0;
  for (size_t i = 0; i < len; i++) {
    if (!isspace((unsigned char)g[i])) s[sl++] = g[i];
  }
  s[sl] = 0;
  if (sl % 4 != 0) fail(p, n, "invalid base64");
  size_t outcap = sl / 4 * 3;
  uint8_t *out = areq(p, outcap ? outcap : 1);
  size_t ol = 0;
  for (size_t i = 0; i < sl; i += 4) {
    int pad2 = s[i + 2] == '=';
    int pad3 = s[i + 3] == '=';
    int a = b64_val(s[i]), b = b64_val(s[i + 1]);
    if (a < 0 || b < 0) fail(p, n, "invalid base64");
    out[ol++] = (uint8_t)((a << 2) | (b >> 4));
    if (!pad2) {
      int c = b64_val(s[i + 2]);
      if (c < 0) fail(p, n, "invalid base64");
      out[ol++] = (uint8_t)(((b & 0xf) << 4) | (c >> 2));
      if (!pad3) {
        int d = b64_val(s[i + 3]);
        if (d < 0) fail(p, n, "invalid base64");
        out[ol++] = (uint8_t)(((c & 3) << 6) | d);
      }
    }
  }
  xun_value *v = vnew(p, XUN_BYTES);
  v->u.bytes.data = out;
  v->u.bytes.len = ol;
  return v;
}

static xun_value *apply_tag(parser *p, const char *tag, const char *glyph, int n) {
  if (strcmp(tag, "s") == 0) return vstr(p, glyph);
  if (strcmp(tag, "n") == 0) return parse_n(p, glyph, n);
  if (strcmp(tag, "i") == 0) return parse_i(p, glyph, n);
  if (strcmp(tag, "f") == 0) return parse_f(p, glyph, n);
  if (strcmp(tag, "x") == 0) {
    char *s = strip_underscores(p, glyph, n);
    if (!fullmatch("^[0-9A-Fa-f]+$", s)) fail(p, n, "invalid hex");
    return vtagged(p, "x", glyph);
  }
  if (strcmp(tag, "xb") == 0) {
    size_t len = strlen(glyph);
    char *s = areq(p, len + 1);
    size_t sl = 0;
    for (size_t i = 0; i < len; i++) if (glyph[i] != '_') s[sl++] = glyph[i];
    s[sl] = 0;
    if (!fullmatch("^[0-9A-Fa-f]*$", s) || sl % 2 != 0 || sl == 0) {
      fail(p, n, "hex bytes must be an even number of digits");
    }
    uint8_t *out = areq(p, sl / 2);
    for (size_t i = 0; i < sl; i += 2) {
      char byte_str[3] = { s[i], s[i + 1], 0 };
      out[i / 2] = (uint8_t)strtoul(byte_str, NULL, 16);
    }
    xun_value *v = vnew(p, XUN_BYTES);
    v->u.bytes.data = out;
    v->u.bytes.len = sl / 2;
    return v;
  }
  if (strcmp(tag, "o") == 0) {
    if (!fullmatch("^[0-7]+$", glyph)) fail(p, n, "invalid octal");
    return vtagged(p, "o", glyph);
  }
  if (strcmp(tag, "b") == 0) {
    if (strcmp(glyph, "true") == 0) return vbool(p, 1);
    if (strcmp(glyph, "false") == 0) return vbool(p, 0);
    fail(p, n, "boolean must be true or false");
  }
  if (strcmp(tag, "d") == 0) {
    if (!fullmatch("^[0-9]{4}-[0-9]{2}-[0-9]{2}$", glyph)) fail(p, n, "invalid date");
    return vtagged(p, "d", glyph);
  }
  if (strcmp(tag, "t") == 0) {
    if (!fullmatch("^[0-9]{2}:[0-9]{2}(:[0-9]{2}(\\.[0-9]+)?)?$", glyph)) fail(p, n, "invalid time");
    return vtagged(p, "t", glyph);
  }
  if (strcmp(tag, "dt") == 0) {
    if (!fullmatch("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\\.[0-9]+)?(Z|[+-][0-9]{2}:[0-9]{2})$", glyph)) {
      fail(p, n, "datetime must include a timezone offset");
    }
    return vtagged(p, "dt", glyph);
  }
  if (strcmp(tag, "tz") == 0) {
    if (strcmp(glyph, "Z") != 0 && strcmp(glyph, "UTC") != 0
        && !fullmatch("^[+-][0-9]{2}:[0-9]{2}$", glyph)
        && !fullmatch("^[A-Za-z_]+(/[A-Za-z0-9_+-]+)+$", glyph)) {
      fail(p, n, "invalid time zone");
    }
    return vtagged(p, "tz", glyph);
  }
  if (strcmp(tag, "du") == 0) {
    double ignored = 0;
    if (parse_duration_seconds(glyph, &ignored) != 0) fail(p, n, "invalid duration");
    return vtagged(p, "du", glyph);
  }
  if (strcmp(tag, "sz") == 0) {
    if (!fullmatch("^[0-9]+(\\.[0-9]+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$", glyph)) {
      fail(p, n, "invalid data size");
    }
    return vtagged(p, "sz", glyph);
  }
  if (strcmp(tag, "unix") == 0) {
    parse_unix(p, glyph, n);
    return vtagged(p, "unix", glyph);
  }
  if (strcmp(tag, "ver") == 0) {
    if (!fullmatch("^[0-9]+(\\.[0-9]+)*$", glyph)) fail(p, n, "invalid version");
    return vtagged(p, "ver", glyph);
  }
  if (strcmp(tag, "uuid") == 0) {
    if (!fullmatch("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$", glyph)) {
      fail(p, n, "invalid uuid");
    }
    return vtagged(p, "uuid", glyph);
  }
  if (strcmp(tag, "ip") == 0) {
    if (!is_ip(glyph)) fail(p, n, "invalid ip");
    return vtagged(p, "ip", glyph);
  }
  if (strcmp(tag, "b64") == 0) return parse_b64(p, glyph, n);
  if (strcmp(tag, "c") == 0) {
    if (starts_with(glyph, "U+")) {
      char *end = NULL;
      long cp = strtol(glyph + 2, &end, 16);
      if (!end || *end || cp > 0x10ffff) fail(p, n, "invalid code point");
      char buf[5] = {0};
      if (cp <= 0x7f) { buf[0] = (char)cp; }
      else if (cp <= 0x7ff) { buf[0] = (char)(0xc0 | (cp >> 6)); buf[1] = (char)(0x80 | (cp & 0x3f)); }
      else if (cp <= 0xffff) { buf[0] = (char)(0xe0 | (cp >> 12)); buf[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); buf[2] = (char)(0x80 | (cp & 0x3f)); }
      else { buf[0] = (char)(0xf0 | (cp >> 18)); buf[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); buf[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); buf[3] = (char)(0x80 | (cp & 0x3f)); }
      return vtagged(p, "c", buf);
    }
    size_t len = strlen(glyph);
    if (len == 0 || (glyph[0] & 0x80 ? ((glyph[0] & 0xe0) == 0xc0 ? len != 2 : ((glyph[0] & 0xf0) == 0xe0 ? len != 3 : len != 4)) : len != 1)) {
      fail(p, n, "character must be a single scalar");
    }
    return vtagged(p, "c", glyph);
  }
  return vtagged(p, tag, glyph);
}

static char *glyph_of(parser *p, xun_value *v, int line_no) {
  if (v->kind == XUN_TAGGED) return v->u.tagged.value;
  if (v->kind == XUN_STRING) return v->u.str;
  if (v->kind == XUN_INT) {
    char buf[64];
    snprintf(buf, sizeof buf, "%lld", (long long)v->u.i);
    return astrdup(p, buf);
  }
  if (v->kind == XUN_FLOAT) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.17g", v->u.f);
    return astrdup(p, buf);
  }
  if (v->kind == XUN_BOOL) return astrdup(p, v->u.b ? "true" : "false");
  if (v->kind == XUN_BYTES) {
    char *out = areq(p, v->u.bytes.len * 2 + 1);
    for (size_t i = 0; i < v->u.bytes.len; i++) {
      snprintf(out + i * 2, 3, "%02x", v->u.bytes.data[i]);
    }
    return out;
  }
  fail(p, line_no, "cannot stringify a collection as scalar glyph");
  return NULL;
}

/* Chomp modes for multiline blocks. */
enum { CHOMP_EXACT = 0, CHOMP_STRIP = 1, CHOMP_CLIP = 2 };

/* Match `|`, `|-`, `|+`, or `|<closer><chomp>`; returns 1 on match. */
static int match_multiline(parser *p, const char *raw, char **out_closer, int *out_chomp) {
  if (strcmp(raw, "|") == 0) {
    *out_closer = astrdup(p, "|");
    *out_chomp = CHOMP_EXACT;
    return 1;
  }
  if (strcmp(raw, "|-") == 0) {
    *out_closer = astrdup(p, "|");
    *out_chomp = CHOMP_STRIP;
    return 1;
  }
  if (strcmp(raw, "|+") == 0) {
    *out_closer = astrdup(p, "|");
    *out_chomp = CHOMP_CLIP;
    return 1;
  }
  if (raw[0] != '|') return 0;
  const char *s = raw + 1;
  size_t tlen = 0;
  while (s[tlen] && (isalnum((unsigned char)s[tlen]) || s[tlen] == '_')) tlen++;
  if (tlen == 0) return 0;
  char *name = astrndup(p, s, tlen);
  if (!is_ident(name)) return 0;
  const char *rest = s + tlen;
  int chomp = CHOMP_EXACT;
  if (rest[0] == '-' && !rest[1]) chomp = CHOMP_STRIP;
  else if (rest[0] == '+' && !rest[1]) chomp = CHOMP_CLIP;
  else if (rest[0]) return 0;
  *out_closer = name;
  *out_chomp = chomp;
  return 1;
}

static xun_value *read_multiline(parser *p, int parent_indent, const char *tag, const char *closer, int chomp, int line_no) {
  int base = parent_indent + 2;
  char **parts = NULL;
  size_t nparts = 0;
  while (peek(p)) {
    line *l = peek(p);
    char *stripped = rstrip_space_tab(p, l->raw);
    int ind = leading_spaces(l->raw);
    char *content = stripped + ind;
    char *closer_text = strip_trailing_comment(p, content);
    if (!l->blank && ind == parent_indent && strcmp(closer_text, closer) == 0) {
      p->i++;
      size_t total = 2; /* room for possible clip '\n' + NUL */
      for (size_t j = 0; j < nparts; j++) total += strlen(parts[j]) + 1;
      char *s = areq(p, total);
      s[0] = 0;
      for (size_t j = 0; j < nparts; j++) {
        strcat(s, parts[j]);
        if (j + 1 < nparts) strcat(s, "\n");
      }
      if (chomp == CHOMP_STRIP) {
        size_t sl = strlen(s);
        while (sl > 0 && s[sl - 1] == '\n') s[--sl] = 0;
      } else if (chomp == CHOMP_CLIP) {
        size_t sl = strlen(s);
        if (sl > 0 && s[sl - 1] != '\n') {
          s[sl] = '\n';
          s[sl + 1] = 0;
        }
      }
      if (tag && strcmp(tag, "s") != 0) return apply_tag(p, tag, s, line_no);
      return vstr(p, s);
    }
    if (l->blank) {
      char **np = realloc(parts, (nparts + 1) * sizeof(char *));
      if (!np) fail(p, 0, "out of memory");
      parts = np;
      parts[nparts++] = astrdup(p, "");
      p->i++;
      continue;
    }
    if (ind < base && !l->blank) fail(p, l->n, "multiline body must indent +2, or close at opener indent");
    if (strchr(l->raw, '\t')) fail(p, l->n, "tab is not allowed");
    char **np = realloc(parts, (nparts + 1) * sizeof(char *));
    if (!np) fail(p, 0, "out of memory");
    parts = np;
    parts[nparts++] = astrdup(p, l->raw + base);
    p->i++;
  }
  fail(p, line_no, "unclosed multiline block");
  return NULL;
}

static xun_value *parse_empty_or_nested(parser *p, int parent_indent, int line_no, int depth, const char *item_tag, const char *value_key) {
  (void)line_no;
  skip_noise(p);
  line *n = peek(p);
  int child = parent_indent + 2;
  if (!n || n->blank || n->indent <= parent_indent) {
    if (item_tag) return vlist(p);
    return vstr(p, "");
  }
  if (n->indent != child) fail(p, n->n, "child indent must be parent + 2");
  if (is_list_item(n)) return parse_list(p, child, depth, item_tag, value_key);
  if (item_tag) fail(p, n->n, "!%s[] expected list items", item_tag);
  return parse_dict(p, child, depth, value_key);
}

static xun_value *parse_tagged(parser *p, const char *raw, int parent_indent, int line_no, int depth, const char *value_key) {
  const char *s = raw + 1;
  size_t tlen = 0;
  while (s[tlen] && (isalnum((unsigned char)s[tlen]) || s[tlen] == '_')) tlen++;
  if (tlen == 0) fail(p, line_no, "invalid type tag");
  char *tag = astrndup(p, s, tlen);
  const char *rest = s + tlen;
  if (rest[0] == '[') {
    if (strcmp(tag, "s") == 0 && strcmp(rest, "[]") != 0) {
      fail(p, line_no, "string arrays cannot use compact form");
    }
    size_t rlen = strlen(rest);
    if (rest[rlen - 1] != ']') fail(p, line_no, "unclosed compact array");
    char *inner = astrndup(p, rest + 1, rlen - 2);
    if (!inner[0]) {
      /* RFC-0001: empty tagged-array block, allow 'end' to close it. */
      push_open_block(p, parent_indent, value_key);
      xun_value *res = parse_empty_or_nested(p, parent_indent, line_no, depth, tag, value_key);
      skip_noise(p);
      try_consume_end(p, parent_indent, value_key);
      pop_open_block(p);
      return res;
    }
    xun_value *arr = vlist(p);
    /* RFC-0002: detect inline objects inside the compact array.
       split_top_level_commas respects nesting depth. */
    char **pieces = NULL;
    size_t n_pieces = 0;
    split_top_level_commas(p, inner, &pieces, &n_pieces);
    int has_objects = 0;
    for (size_t k = 0; k < n_pieces; k++) {
      const char *t = pieces[k];
      while (*t == ' ' || *t == '\t') t++;
      if (*t == '{') { has_objects = 1; break; }
    }
    if (has_objects) {
      for (size_t k = 0; k < n_pieces; k++) {
        const char *t = pieces[k];
        while (*t == ' ' || *t == '\t') t++;
        const char *end = t + strlen(t);
        while (end > t && (end[-1] == ' ' || end[-1] == '\t')) end--;
        size_t tlen2 = (size_t)(end - t);
        if (tlen2 == 0) continue;
        if (t[0] == '{') {
          char *piece_copy = astrndup(p, t, tlen2);
          list_push(p, arr, parse_inline_dict(p, piece_copy, line_no));
        } else {
          char *piece_copy = astrndup(p, t, tlen2);
          list_push(p, arr, apply_tag(p, tag, piece_copy, line_no));
        }
      }
      free(pieces);
      return arr;
    }
    free(pieces);
    char *save = NULL;
    char *tok = strtok_r(inner, ",", &save);
    while (tok) {
      while (*tok == ' ') tok++;
      size_t tl = strlen(tok);
      while (tl > 0 && tok[tl - 1] == ' ') tl--;
      tok[tl] = 0;
      list_push(p, arr, apply_tag(p, tag, tok, line_no));
      tok = strtok_r(NULL, ",", &save);
    }
    return arr;
  }
  if (!rest[0]) fail(p, line_no, "missing value for !%s", tag);
  if (rest[0] != ' ') fail(p, line_no, "expected space after type tag");
  const char *body = rest + 1;
  char *closer = NULL;
  int chomp = CHOMP_EXACT;
  if (match_multiline(p, body, &closer, &chomp)) {
    xun_value *res = read_multiline(p, parent_indent, NULL, closer, chomp, line_no);
    if (strcmp(tag, "s") == 0) return res;
    return apply_tag(p, tag, res->u.str, line_no);
  }
  if (strcmp(tag, "s") == 0) return vstr(p, parse_string_body(p, body, line_no));
  return apply_tag(p, tag, body, line_no);
}

static xun_value *parse_value(parser *p, const char *raw, int parent_indent, int line_no, int depth, const char *value_key) {
  char *stripped = strip_trailing_comment(p, raw);
  /* RFC-0002: untagged compact array of inline objects: [{...}, {...}] */
  if (stripped[0] == '[' && stripped[strlen(stripped) - 1] == ']' && strcmp(stripped, "[]") != 0) {
    size_t rlen = strlen(stripped);
    char *inner = astrndup(p, stripped + 1, rlen - 2);
    char **pieces = NULL;
    size_t n_pieces = 0;
    split_top_level_commas(p, inner, &pieces, &n_pieces);
    int has_objects = 0;
    for (size_t k = 0; k < n_pieces; k++) {
      const char *t = pieces[k];
      while (*t == ' ' || *t == '\t') t++;
      if (*t == '{') { has_objects = 1; break; }
    }
    if (has_objects) {
      xun_value *arr = vlist(p);
      for (size_t k = 0; k < n_pieces; k++) {
        const char *t = pieces[k];
        while (*t == ' ' || *t == '\t') t++;
        const char *end = t + strlen(t);
        while (end > t && (end[-1] == ' ' || end[-1] == '\t')) end--;
        size_t tlen2 = (size_t)(end - t);
        if (tlen2 == 0) continue;
        char *piece_copy = astrndup(p, t, tlen2);
        list_push(p, arr, parse_inline_dict(p, piece_copy, line_no));
      }
      free(pieces);
      return arr;
    }
    free(pieces);
  }
  if (strcmp(stripped, "[]") == 0) return vlist(p);
  if (strcmp(stripped, "{}") == 0) return vdict(p);
  char *closer = NULL;
  int chomp = CHOMP_EXACT;
  if (match_multiline(p, stripped, &closer, &chomp)) {
    return read_multiline(p, parent_indent, NULL, closer, chomp, line_no);
  }
  if (stripped[0] == '!') return parse_tagged(p, stripped, parent_indent, line_no, depth, value_key);
  if (!stripped[0]) {
    /* RFC-0001: nested block — push onto openBlocks so 'end' can close it. */
    push_open_block(p, parent_indent, value_key);
    xun_value *res = parse_empty_or_nested(p, parent_indent, line_no, depth, NULL, value_key);
    skip_noise(p);
    try_consume_end(p, parent_indent, value_key);
    pop_open_block(p);
    return res;
  }
  if (stripped[0] == '{') {
    /* RFC-0002: inline object literal at value position */
    return parse_inline_dict(p, stripped, line_no);
  }
  if (stripped[0] == '"') return vstr(p, parse_quoted_string(p, stripped, line_no));
  return vstr(p, stripped);
}

static void split_key(parser *p, const char *text, int n, char **out_k, char **out_v) {
  if (text[0] == '"') {
    size_t end = 0;
    char *key = parse_quoted_prefix(p, text, n, &end);
    const char *after = text + end;
    if (strcmp(after, ":") == 0) {
      *out_k = key;
      *out_v = astrdup(p, "");
      return;
    }
    if (starts_with(after, ": ")) {
      *out_k = key;
      *out_v = astrdup(p, after + 2);
      return;
    }
    fail(p, n, "expected ': ' or trailing ':' after quoted key");
  }
  const char *pos = strstr(text, ": ");
  if (pos && pos != text) {
    size_t klen = (size_t)(pos - text);
    *out_k = astrndup(p, text, klen);
    if (klen > 0 && (*out_k)[klen - 1] == ':') fail(p, n, "key must not end with ':'");
    *out_v = astrdup(p, pos + 2);
    return;
  }
  size_t len = strlen(text);
  if (len > 1 && text[len - 1] == ':') {
    *out_k = astrndup(p, text, len - 1);
    if (len >= 2 && (*out_k)[len - 2] == ':') fail(p, n, "key must not end with ':'");
    *out_v = astrdup(p, "");
    return;
  }
  fail(p, n, "expected ': ' or trailing ':'");
}

/* RFC-0002: detect a list-item that should be parsed as a (block or inline) dict entry.
   After trimming leading whitespace, an opening '{' means an inline object literal. */
static int looks_like_dict_entry(const char *s) {
  /* Find first non-space/tab char. */
  while (*s == ' ' || *s == '\t') s++;
  if (!s[0] || s[0] == '!' || s[0] == '"' || s[0] == '|') return 0;
  /* RFC-0002: inline object literal starts with '{' and is not followed by whitespace. */
  if (s[0] == '{' && (s[1] == 0 || s[1] == '}' || s[1] == ',' || s[1] == ' ' || s[1] == '\t')) {
    /* '{...}' inline literal — treat as a dict entry. */
    /* But '{x: y' inline form also qualifies; the trailing closure form '{...}' gets
       handled by parse_inline_dict; we still want list items to detect it. */
    /* Detect '{' that is not inside a quoted string — for simplicity here we only treat
       an unescaped '{' as an inline-object trigger if it appears immediately. */
    /* We deliberately only accept '{' that begins the trimmed string. */
    return s[0] == '{' ? 1 : 0;
  }
  if (s[0] == '{') return 1;
  return strstr(s, ": ") != NULL || (strlen(s) > 1 && s[strlen(s) - 1] == ':');
}

static xun_value *parse_inline_dict_entry(parser *p, const char *first, int key_indent, int line_no, int depth) {
  xun_value *obj = vdict(p);
  /* RFC-0002: if the first entry starts with '{', it's an inline object literal. */
  const char *ft = first;
  while (*ft == ' ' || *ft == '\t') ft++;
  if (ft[0] == '{') {
    /* Consume the first entry text and look for trailing pieces on same logical
       line — but inline dict literals are a single line. We just hand off the whole
       first to parse_inline_dict. */
    /* Advance past the line that contained '{first'. Caller (parse_list) has already
       consumed the list-item `-` prefix; we still need to skip lines that look like
       continuation of the inline object — but inline dicts are single-line. */
    return parse_inline_dict(p, first, line_no);
  }
  char *key = NULL, *rest = NULL;
  split_key(p, first, line_no, &key, &rest);
  if (dict_has(obj, key)) fail(p, line_no, "duplicate key '%s'", key);
  dict_put(p, obj, key, parse_value(p, rest, key_indent, line_no, depth + 1, key));
  while (peek(p)) {
    skip_noise(p);
    line *l = peek(p);
    if (!l || l->blank) break;
    if (l->indent < key_indent) break;
    if (l->indent > key_indent) fail(p, l->n, "invalid indent jump");
    if (is_list_item(l)) break;
    char *k = NULL, *r = NULL;
    split_key(p, l->code, l->n, &k, &r);
    if (dict_has(obj, k)) fail(p, l->n, "duplicate key '%s'", k);
    p->i++;
    dict_put(p, obj, k, parse_value(p, r, key_indent, l->n, depth + 1, k));
  }
  return obj;
}

static xun_value *parse_dict(parser *p, int indent, int depth, const char *dict_key) {
  (void)dict_key;
  if (depth > MAX_DEPTH) {
    int n = peek(p) ? peek(p)->n : 0;
    fail(p, n, "nesting exceeds 64");
  }
  xun_value *obj = vdict(p);
  while (peek(p)) {
    skip_noise(p);
    line *l = peek(p);
    if (!l || l->blank) break;
    if (l->indent < indent) break;
    if (l->indent > indent) fail(p, l->n, "invalid indent jump");
    /* RFC-0001: 'end' at body indent stops the dict; caller validates via tryConsumeEnd. */
    if (line_is_end_at(p, indent)) break;
    if (is_list_item(l)) fail(p, l->n, "cannot mix list items into a dictionary");
    char *key = NULL, *rest = NULL;
    split_key(p, l->code, l->n, &key, &rest);
    if (dict_has(obj, key)) fail(p, l->n, "duplicate key '%s'", key);
    p->i++;
    xun_value *val = parse_value(p, rest, indent, l->n, depth + 1, key);
    dict_put(p, obj, key, val);
  }
  return obj;
}

static xun_value *parse_list(parser *p, int indent, int depth, const char *item_tag, const char *parent_key) {
  (void)parent_key;
  if (depth > MAX_DEPTH) {
    int n = peek(p) ? peek(p)->n : 0;
    fail(p, n, "nesting exceeds 64");
  }
  xun_value *arr = vlist(p);
  while (peek(p)) {
    skip_noise(p);
    line *l = peek(p);
    if (!l || l->blank) break;
    if (l->indent < indent) break;
    if (l->indent > indent) fail(p, l->n, "invalid indent jump");
    /* RFC-0001: 'end' at list level stops the list. */
    if (line_is_end_at(p, indent)) break;
    if (!is_list_item(l)) fail(p, l->n, "cannot mix dictionary keys into a list");
    const char *rest = strcmp(l->code, "-") == 0 ? "" : l->code + 2;
    p->i++;
    if (looks_like_dict_entry(rest)) {
      /* RFC-0002: inline object literal {key: val, ...} */
      const char *rt = rest;
      while (*rt == ' ' || *rt == '\t') rt++;
      if (rt[0] == '{') {
        if (item_tag) fail(p, l->n, "!%s[] cannot contain dictionary entries", item_tag);
        list_push(p, arr, parse_inline_dict(p, rest, l->n));
        continue;
      }
      if (item_tag) fail(p, l->n, "!%s[] cannot contain dictionary entries", item_tag);
      list_push(p, arr, parse_inline_dict_entry(p, rest, indent + 2, l->n, depth + 1));
      continue;
    }
    xun_value *val = parse_value(p, rest, indent, l->n, depth + 1, NULL);
    if (item_tag) val = apply_tag(p, item_tag, glyph_of(p, val, l->n), l->n);
    list_push(p, arr, val);
  }
  return arr;
}

/* RFC-0002: split a string by top-level commas, respecting brace and quote nesting.
   Returns an array of malloc'd strings (caller frees with free()). */
static void split_top_level_commas(parser *p, const char *inner, char ***out_parts, size_t *out_count) {
  (void)p;
  size_t cap = 4;
  size_t n = 0;
  char **parts = malloc(cap * sizeof(char *));
  if (!parts) return;
  size_t start = 0;
  int brace = 0, bracket = 0, in_quote = 0, escape = 0;
  for (size_t i = 0; inner[i]; i++) {
    char c = inner[i];
    if (escape) { escape = 0; continue; }
    if (in_quote) {
      if (c == '\\') { escape = 1; continue; }
      if (c == '"') { in_quote = 0; }
      continue;
    }
    if (c == '"') { in_quote = 1; continue; }
    if (c == '{') { brace++; continue; }
    if (c == '}') { brace--; continue; }
    if (c == '[') { bracket++; continue; }
    if (c == ']') { bracket--; continue; }
    if (c == ',' && brace == 0 && bracket == 0) {
      if (n == cap) { cap *= 2; parts = realloc(parts, cap * sizeof(char *)); }
      parts[n++] = strndup(inner + start, i - start);
      start = i + 1;
    }
  }
  if (n == cap) { cap *= 2; parts = realloc(parts, cap * sizeof(char *)); }
  parts[n++] = strndup(inner + start, strlen(inner) - start);
  *out_parts = parts;
  *out_count = n;
}

/* RFC-0002: parse an inline object literal {key: value, key2: value2}. */
static xun_value *parse_inline_dict(parser *p, const char *text, int line_no) {
  /* Trim the entire text — inline dict literals can have surrounding whitespace. */
  const char *s = text;
  while (*s == ' ' || *s == '\t') s++;
  const char *end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t')) end--;
  size_t slen = (size_t)(end - s);
  if (slen < 2 || s[0] != '{' || s[slen - 1] != '}') {
    fail(p, line_no, "inline object must be wrapped in '{...}'");
  }
  char *trimmed = astrndup(p, s, slen);
  /* Empty content "{}". */
  size_t inner_len = slen - 2;
  char *inner = astrndup(p, trimmed + 1, inner_len);
  int empty = 1;
  for (size_t i = 0; i < inner_len; i++) {
    if (inner[i] != ' ' && inner[i] != '\t') { empty = 0; break; }
  }
  if (empty) return vdict(p);
  char **pieces = NULL;
  size_t n_pieces = 0;
  split_top_level_commas(p, inner, &pieces, &n_pieces);
  xun_value *obj = vdict(p);
  for (size_t i = 0; i < n_pieces; i++) {
    const char *piece = pieces[i];
    /* Trim piece. */
    while (*piece == ' ' || *piece == '\t') piece++;
    end = piece + strlen(piece);
    while (end > piece && (end[-1] == ' ' || end[-1] == '\t')) end--;
    size_t plen = (size_t)(end - piece);
    if (plen == 0) continue;
    char *t = astrndup(p, piece, plen);
    /* Find the top-level ':' separator — either ': ' in the middle or trailing ':'. */
    int in_q = 0, esc = 0;
    size_t found_idx = (size_t)-1;
    for (size_t k = 0; t[k]; k++) {
      char c = t[k];
      if (esc) { esc = 0; continue; }
      if (in_q) {
        if (c == '\\') { esc = 1; continue; }
        if (c == '"') in_q = 0;
        continue;
      }
      if (c == '"') { in_q = 1; continue; }
      if (c == ':') {
        if (k == plen - 1) { found_idx = k; break; }
        if (t[k + 1] == ' ' || t[k + 1] == '\t') { found_idx = k; break; }
      }
    }
    if (found_idx == (size_t)-1) {
      fail(p, line_no, "inline object entry missing ': ' separator: '%s'", t);
    }
    /* Extract key (trimmed). */
    char *key_raw = astrndup(p, t, found_idx);
    /* Trim key whitespace. */
    char *k = key_raw;
    while (*k == ' ' || *k == '\t') k++;
    char *kend = k + strlen(k);
    while (kend > k && (kend[-1] == ' ' || kend[-1] == '\t')) kend--;
    *kend = 0;
    if (k[0] == '"') {
      size_t ke = 0;
      char *key = parse_quoted_prefix(p, k, line_no, &ke);
      if (k[ke] != 0) {
        fail(p, line_no, "unexpected content after quoted key in inline object");
      }
      char *tmp = key_raw;
      key_raw = key;
      /* tmp is arena-managed; key becomes our local key. */
      (void)tmp;
    }
    if (!key_raw[0]) fail(p, line_no, "empty key in inline object");
    if (dict_has(obj, key_raw)) fail(p, line_no, "duplicate key '%s'", key_raw);
    /* Extract rest after ':'. */
    char *rest_raw = astrdup(p, t + found_idx + 1);
    char *r = rest_raw;
    while (*r == ' ' || *r == '\t') r++;
    char *rend = r + strlen(r);
    while (rend > r && (rend[-1] == ' ' || rend[-1] == '\t')) rend--;
    *rend = 0;
    xun_value *val = NULL;
    if (!r[0]) {
      val = vstr(p, "");
    } else if (r[0] == '"') {
      val = vstr(p, parse_quoted_string(p, r, line_no));
    } else if (r[0] == '!') {
      /* Compact tagged array or tagged value inside inline dict. */
      const char *ts = r + 1;
      size_t ttaglen = 0;
      while (ts[ttaglen] && (isalnum((unsigned char)ts[ttaglen]) || ts[ttaglen] == '_')) ttaglen++;
      if (ttaglen == 0) fail(p, line_no, "invalid type tag in inline object: '%s'", r);
      char *ttag = astrndup(p, ts, ttaglen);
      const char *tail = ts + ttaglen;
      if (tail[0] == '[') {
        size_t tlen2 = strlen(tail);
        if (tail[tlen2 - 1] != ']') fail(p, line_no, "unclosed compact array in inline object");
        char *tinner = astrndup(p, tail + 1, tlen2 - 2);
        xun_value *sub = vlist(p);
        if (tinner[0]) {
          char *save = NULL;
          char *gtk = strtok_r(tinner, ",", &save);
          while (gtk) {
            while (*gtk == ' ') gtk++;
            size_t gl = strlen(gtk);
            while (gl > 0 && gtk[gl - 1] == ' ') gl--;
            gtk[gl] = 0;
            list_push(p, sub, apply_tag(p, ttag, gtk, line_no));
            gtk = strtok_r(NULL, ",", &save);
          }
        }
        val = sub;
      } else if (tail[0] == ' ') {
        val = apply_tag(p, ttag, tail + 1, line_no);
      } else {
        fail(p, line_no, "expected space or '[' after type tag in inline object");
      }
    } else {
      val = vstr(p, r);
    }
    dict_put(p, obj, key_raw, val);
  }
  free(pieces);
  return obj;
}

static line *split_lines(parser *p, const char *source, size_t *out_n) {
  size_t len = strlen(source);
  if (len == 0) {
    *out_n = 0;
    return NULL;
  }
  line *lines = NULL;
  size_t count = 0;
  int n = 1;
  size_t start = 0;
  for (size_t i = 0; i <= len; i++) {
    int at_end = i == len;
    char c = at_end ? 0 : source[i];
    if (!at_end && c != '\n' && c != '\r') continue;
    char *raw = astrndup(p, source + start, i - start);
    if (c == '\r' && i + 1 < len && source[i + 1] == '\n') i++;
    int ind = leading_spaces(raw);
    if (raw[ind] == '\t') fail(p, n, "tab is not allowed");
    if (ind % 2 != 0) fail(p, n, "indent must be a multiple of 2");
    char *text = rstrip_space_tab(p, raw + ind);
    char *code = strip_trailing_comment(p, text);
    line l = { raw, ind, text, code, n, text[0] == 0 };
    line *nl = realloc(lines, (count + 1) * sizeof(line));
    if (!nl) fail(p, 0, "out of memory");
    lines = nl;
    lines[count++] = l;
    n++;
    start = i + 1;
  }
  *out_n = count;
  return lines;
}

int xun_parse(const char *source, xun_value **out, xun_error *err) {
  if (!source) {
    if (err) { err->line = 0; snprintf(err->message, sizeof err->message, "source cannot be NULL"); }
    return -1;
  }
  if (strlen(source) > MAX_BYTES) {
    if (err) { err->line = 0; snprintf(err->message, sizeof err->message, "document exceeds 1MB"); }
    return -1;
  }
  if (strchr(source, '\0') && strlen(source) != 0) {
    /* check for embedded NUL if length check allows */
  }
  arena a = {0};
  parser p = {0};
  p.err = err;
  p.a = &a;
  if (setjmp(p.jmp) != 0) {
    if (p.lines) free(p.lines);
    for (size_t i = 0; i < a.n; i++) free(a.p[i]);
    free(a.p);
    free(p.open_blocks_indent);
    free(p.open_blocks_key);
    return -1;
  }
  if (starts_with(source, "\xef\xbb\xbf")) source += 3;
  p.lines = split_lines(&p, source, &p.nlines);
  skip_noise(&p);
  xun_value *root = NULL;
  /* RFC-0001: track root block so a trailing 'end' can close it. */
  push_open_block(&p, 0, NULL);
  if (!peek(&p)) {
    root = vdict(&p);
  } else {
    line *first = peek(&p);
    if (first->indent != 0) fail(&p, first->n, "document must start at indent 0");
    if (is_list_item(first)) fail(&p, first->n, "root must be a dictionary");
    root = parse_dict(&p, 0, 0, NULL);
    skip_noise(&p);
    try_consume_end(&p, 0, NULL);
  }
  pop_open_block(&p);
  if (p.lines) {
    free(p.lines);
    p.lines = NULL;
  }
  /* Save arena handle in root so xun_free can free all allocations */
  arena *saved = malloc(sizeof(arena));
  *saved = a;
  root->arena = saved;
  *out = root;
  return 0;
}

int xun_parse_file(const char *path, xun_value **out, xun_error *err) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    if (err) { err->line = 0; snprintf(err->message, sizeof err->message, "cannot open file"); }
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0 || sz > MAX_BYTES) {
    fclose(f);
    if (err) { err->line = 0; snprintf(err->message, sizeof err->message, "file too large"); }
    return -1;
  }
  char *buf = malloc(sz + 1);
  if (!buf) {
    fclose(f);
    if (err) { err->line = 0; snprintf(err->message, sizeof err->message, "out of memory"); }
    return -1;
  }
  size_t read_bytes = fread(buf, 1, sz, f);
  fclose(f);
  buf[read_bytes] = 0;
  int r = xun_parse(buf, out, err);
  free(buf);
  return r;
}

void xun_free(xun_value *v) {
  if (!v || !v->arena) return;
  arena *a = (arena *)v->arena;
  for (size_t i = 0; i < a->n; i++) free(a->p[i]);
  free(a->p);
  free(a);
}

// --- Encoder ---

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} str_buf;

static void buf_init(str_buf *b) {
  b->cap = 256;
  b->data = malloc(b->cap);
  b->len = 0;
  if (b->data) b->data[0] = 0;
}

static void buf_append_str(str_buf *b, const char *s) {
  if (!s) return;
  size_t slen = strlen(s);
  while (b->len + slen + 1 >= b->cap) {
    b->cap *= 2;
    b->data = realloc(b->data, b->cap);
  }
  memcpy(b->data + b->len, s, slen);
  b->len += slen;
  b->data[b->len] = 0;
}

static void buf_append_indent(str_buf *b, int depth) {
  for (int i = 0; i < depth; i++) buf_append_str(b, "  ");
}

static int is_ascii_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* Glyphs that JavaScript Number() would coerce. */
static int looks_like_js_decimal(const char *s, size_t n) {
  size_t i = 0;
  int saw_digit = 0;
  if (i < n && s[i] == '.') {
    i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
      saw_digit = 1;
      i++;
    }
  } else {
    while (i < n && s[i] >= '0' && s[i] <= '9') {
      saw_digit = 1;
      i++;
    }
    if (i < n && s[i] == '.') {
      i++;
      while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    }
  }
  if (!saw_digit) return 0;
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < n && (s[i] == '+' || s[i] == '-')) i++;
    size_t exp_start = i;
    while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    if (i == exp_start) return 0;
  }
  return i == n;
}

static int looks_like_js_number(const char *s) {
  while (*s && is_ascii_ws(*s)) s++;
  size_t n = strlen(s);
  while (n > 0 && is_ascii_ws(s[n - 1])) n--;
  if (n == 0) return 0;
  size_t i = 0;
  if (s[0] == '+' || s[0] == '-') {
    i = 1;
    if (i >= n) return 0;
  }
  const char *rest = s + i;
  size_t rn = n - i;
  if (rn == 8 && memcmp(rest, "Infinity", 8) == 0) return 1;
  if (rn >= 3 && rest[0] == '0' && (rest[1] == 'x' || rest[1] == 'X')) {
    for (size_t k = 2; k < rn; k++) {
      char c = rest[k];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
  }
  if (rn >= 3 && rest[0] == '0' && (rest[1] == 'b' || rest[1] == 'B')) {
    for (size_t k = 2; k < rn; k++) {
      if (rest[k] != '0' && rest[k] != '1') return 0;
    }
    return 1;
  }
  if (rn >= 3 && rest[0] == '0' && (rest[1] == 'o' || rest[1] == 'O')) {
    for (size_t k = 2; k < rn; k++) {
      if (rest[k] < '0' || rest[k] > '7') return 0;
    }
    return 1;
  }
  return looks_like_js_decimal(rest, rn);
}

static int needs_string_tag(const char *s) {
  return s[0] == '!' || strcmp(s, "[]") == 0 || strcmp(s, "{}") == 0 || s[0] == '|' || looks_like_js_number(s);
}

/* Match JS needsQuotedGlyph: leading/trailing whitespace, quotes, backslash,
   ` #`, leading `#`, `: `, trailing `:`, or leading `|`. */
static int needs_quoted_glyph(const char *s) {
  size_t len = strlen(s);
  if (len == 0) return 0;
  if (is_ascii_ws(s[0]) || is_ascii_ws(s[len - 1])) return 1;
  if (strchr(s, '"') || strchr(s, '\\')) return 1;
  if (strstr(s, " #")) return 1;
  if (s[0] == '#') return 1;
  if (strstr(s, ": ")) return 1;
  if (len > 1 && s[len - 1] == ':') return 1;
  if (s[0] == '|') return 1;
  return 0;
}

static void buf_append_quoted_glyph(str_buf *b, const char *s) {
  buf_append_str(b, "\"");
  for (const char *c = s; *c; c++) {
    if (*c == '\\') buf_append_str(b, "\\\\");
    else if (*c == '"') buf_append_str(b, "\\\"");
    else {
      char tmp[2] = {*c, 0};
      buf_append_str(b, tmp);
    }
  }
  buf_append_str(b, "\"");
}

static void buf_append_string_glyph(str_buf *b, const char *s) {
  if (needs_string_tag(s)) buf_append_str(b, " !s ");
  else buf_append_str(b, " ");
  if (needs_quoted_glyph(s)) buf_append_quoted_glyph(b, s);
  else buf_append_str(b, s);
  buf_append_str(b, "\n");
}

/* Encode a multi-line string/tagged body with `|` opener and closer at `depth`. */
static void buf_append_multiline_body(str_buf *b, int depth, const char *s) {
  const char *cur = s;
  while (1) {
    const char *nl = strchr(cur, '\n');
    size_t line_len = nl ? (size_t)(nl - cur) : strlen(cur);
    if (line_len > 0 && cur[line_len - 1] == '\r') line_len--;
    buf_append_indent(b, depth + 1);
    for (size_t k = 0; k < line_len; k++) {
      char tmp[2] = {cur[k], 0};
      buf_append_str(b, tmp);
    }
    buf_append_str(b, "\n");
    if (!nl) break;
    cur = nl + 1;
  }
  buf_append_indent(b, depth);
  buf_append_str(b, "|\n");
}

/* Parse a quoted string prefix; sets *out_end to index after the closing quote. */
static char *parse_quoted_prefix(parser *p, const char *raw, int line_no, size_t *out_end) {
  if (raw[0] != '"') fail(p, line_no, "quoted string must start with '\"'");
  str_buf out;
  buf_init(&out);
  for (size_t i = 1; raw[i]; i++) {
    char ch = raw[i];
    if (ch == '\\') {
      if (!raw[i + 1]) {
        free(out.data);
        fail(p, line_no, "unclosed escape in quoted string");
      }
      char nxt = raw[++i];
      if (nxt == '\\' || nxt == '"') {
        char tmp[2] = {nxt, 0};
        buf_append_str(&out, tmp);
      } else {
        free(out.data);
        fail(p, line_no, "invalid escape in quoted string");
      }
      continue;
    }
    if (ch == '"') {
      char *res = astrdup(p, out.data ? out.data : "");
      free(out.data);
      if (out_end) *out_end = i + 1;
      return res;
    }
    char tmp[2] = {ch, 0};
    buf_append_str(&out, tmp);
  }
  free(out.data);
  fail(p, line_no, "unclosed quoted string");
  return NULL;
}

static char *parse_quoted_string(parser *p, const char *raw, int line_no) {
  size_t end = 0;
  char *res = parse_quoted_prefix(p, raw, line_no, &end);
  if (raw[end]) fail(p, line_no, "unexpected trailing content after quoted string");
  return res;
}

static char *parse_string_body(parser *p, const char *body, int line_no) {
  if (body[0] == '"') return parse_quoted_string(p, body, line_no);
  return astrdup(p, body);
}

static int validate_key_c(const char *key) {
  if (!key || !key[0]) return -1;
  if (strchr(key, '\n') || strchr(key, '\r') || strstr(key, ": ") || (key[0] && key[strlen(key) - 1] == ':')) {
    return -1;
  }
  return 0;
}

static int encode_value_node(const xun_value *v, int depth, str_buf *b);

static int encode_scalar_body(const xun_value *val, int depth, str_buf *b, const char *lead) {
  /* lead is the already-written prefix ("key:" or "-"). */
  if (!val) {
    buf_append_str(b, "\n");
    return 0;
  }
  if (val->kind == XUN_DICT) {
    if (val->u.dict.len == 0) {
      buf_append_str(b, " {}\n");
    } else {
      buf_append_str(b, "\n");
      for (size_t i = 0; i < val->u.dict.len; i++) {
        const char *key = val->u.dict.items[i].key;
        const xun_value *v = val->u.dict.items[i].val;
        if (validate_key_c(key) != 0) return -1;
        buf_append_indent(b, depth + 1);
        buf_append_str(b, key);
        buf_append_str(b, ":");
        if (encode_scalar_body(v, depth + 1, b, key) != 0) return -1;
      }
    }
    return 0;
  }
  if (val->kind == XUN_LIST) {
    if (val->u.list.len == 0) {
      buf_append_str(b, " []\n");
    } else {
      buf_append_str(b, "\n");
      if (encode_value_node(val, depth + 1, b) != 0) return -1;
    }
    return 0;
  }
  if (val->kind == XUN_STRING) {
    const char *s = val->u.str;
    if (strchr(s, '\n') || strchr(s, '\r')) {
      buf_append_str(b, " |\n");
      buf_append_multiline_body(b, depth, s);
    } else if (!s[0]) {
      buf_append_str(b, "\n");
    } else {
      buf_append_string_glyph(b, s);
    }
    return 0;
  }
  if (val->kind == XUN_INT) {
    char numbuf[64];
    snprintf(numbuf, sizeof numbuf, " !i %lld\n", (long long)val->u.i);
    buf_append_str(b, numbuf);
    return 0;
  }
  if (val->kind == XUN_FLOAT) {
    char numbuf[64];
    snprintf(numbuf, sizeof numbuf, "%.17g", val->u.f);
    if (!strchr(numbuf, '.') && !strchr(numbuf, 'e') && !strchr(numbuf, 'E')) {
      strcat(numbuf, ".0");
    }
    buf_append_str(b, " !f ");
    buf_append_str(b, numbuf);
    buf_append_str(b, "\n");
    return 0;
  }
  if (val->kind == XUN_BOOL) {
    buf_append_str(b, val->u.b ? " !b true\n" : " !b false\n");
    return 0;
  }
  if (val->kind == XUN_BYTES) {
    buf_append_str(b, " !xb ");
    for (size_t j = 0; j < val->u.bytes.len; j++) {
      char h[3];
      snprintf(h, sizeof h, "%02X", val->u.bytes.data[j]);
      buf_append_str(b, h);
    }
    buf_append_str(b, "\n");
    return 0;
  }
  if (val->kind == XUN_TAGGED) {
    const char *s = val->u.tagged.value;
    if (strchr(s, '\n') || strchr(s, '\r')) {
      buf_append_str(b, " !");
      buf_append_str(b, val->u.tagged.tag);
      buf_append_str(b, " |\n");
      buf_append_multiline_body(b, depth, s);
    } else {
      buf_append_str(b, " !");
      buf_append_str(b, val->u.tagged.tag);
      buf_append_str(b, " ");
      buf_append_str(b, val->u.tagged.value);
      buf_append_str(b, "\n");
    }
    return 0;
  }
  (void)lead;
  return -1;
}

static int encode_dict_body(const xun_value *dict, int depth, str_buf *b) {
  if (depth > MAX_DEPTH) return -1;
  for (size_t i = 0; i < dict->u.dict.len; i++) {
    const char *key = dict->u.dict.items[i].key;
    const xun_value *val = dict->u.dict.items[i].val;
    if (validate_key_c(key) != 0) return -1;
    buf_append_indent(b, depth);
    buf_append_str(b, key);
    buf_append_str(b, ":");
    if (encode_scalar_body(val, depth, b, key) != 0) return -1;
  }
  return 0;
}

static int encode_value_node(const xun_value *v, int depth, str_buf *b) {
  if (depth > MAX_DEPTH) return -1;
  if (!v) return 0;
  if (v->kind == XUN_LIST) {
    for (size_t i = 0; i < v->u.list.len; i++) {
      const xun_value *item = v->u.list.items[i];
      buf_append_indent(b, depth);
      buf_append_str(b, "-");
      if (encode_scalar_body(item, depth, b, "-") != 0) return -1;
    }
  }
  return 0;
}

int xun_encode(const xun_value *v, char **out_str, size_t *out_len) {
  if (!v || v->kind != XUN_DICT) return -1;
  if (v->u.dict.len == 0) {
    char *s = strdup("");
    if (out_len) *out_len = 0;
    *out_str = s;
    return 0;
  }
  str_buf b;
  buf_init(&b);
  if (encode_dict_body(v, 0, &b) != 0) {
    free(b.data);
    return -1;
  }
  if (out_len) *out_len = b.len;
  *out_str = b.data;
  return 0;
}

int xun_encode_file(const xun_value *v, const char *path) {
  char *str = NULL;
  size_t len = 0;
  if (xun_encode(v, &str, &len) != 0) return -1;
  FILE *f = fopen(path, "wb");
  if (!f) {
    free(str);
    return -1;
  }
  fwrite(str, 1, len, f);
  fclose(f);
  free(str);
  return 0;
}

int xun_decode(const char *source, xun_value **out, xun_error *err) {
  return xun_parse(source, out, err);
}

int xun_decode_file(const char *path, xun_value **out, xun_error *err) {
  return xun_parse_file(path, out, err);
}

int xun_parse_size_bytes(const char *s, uint64_t *out_bytes) {
  if (!s || !out_bytes) return -1;
  char *end = NULL;
  double val = strtod(s, &end);
  if (end == s || !end) return -1;
  uint64_t mult = 1;
  if (strcmp(end, "B") == 0) mult = 1ULL;
  else if (strcmp(end, "KB") == 0) mult = 1000ULL;
  else if (strcmp(end, "MB") == 0) mult = 1000000ULL;
  else if (strcmp(end, "GB") == 0) mult = 1000000000ULL;
  else if (strcmp(end, "TB") == 0) mult = 1000000000000ULL;
  else if (strcmp(end, "KiB") == 0) mult = 1024ULL;
  else if (strcmp(end, "MiB") == 0) mult = 1024ULL * 1024ULL;
  else if (strcmp(end, "GiB") == 0) mult = 1024ULL * 1024ULL * 1024ULL;
  else if (strcmp(end, "TiB") == 0) mult = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
  else return -1;
  *out_bytes = (uint64_t)(val * (double)mult);
  return 0;
}

int xun_parse_duration_seconds(const char *s, double *out_seconds) {
  return parse_duration_seconds(s, out_seconds);
}

int xun_parse_version_parts(const char *s, int *out_parts, size_t max_parts, size_t *out_count) {
  if (!s || !*s || !out_parts || !out_count) return -1;
  size_t count = 0;
  const char *p = s;
  while (*p) {
    if (count >= max_parts) return -1;
    char *end = NULL;
    long n = strtol(p, &end, 10);
    if (end == p || n < 0) return -1;
    out_parts[count++] = (int)n;
    if (*end == '.') p = end + 1;
    else if (*end == '\0') break;
    else return -1;
  }
  *out_count = count;
  return 0;
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int xun_parse_uuid(const char *s, uint8_t out[16]) {
  if (!s || !out) return -1;
  if (strlen(s) != 36) return -1;
  if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') return -1;
  int oi = 0;
  for (int i = 0; i < 36; i++) {
    if (s[i] == '-') continue;
    int hi = hexval(s[i]);
    int lo = hexval(s[i + 1]);
    if (hi < 0 || lo < 0) return -1;
    out[oi++] = (uint8_t)((hi << 4) | lo);
    i++;
  }
  return oi == 16 ? 0 : -1;
}

int xun_parse_ip(const char *s, uint8_t out[16], int *out_is_v6) {
  if (!s || !out) return -1;
  if (strchr(s, ':') == NULL) {
    int a, b, c, d;
    char buf[32];
    if (!fullmatch("^[0-9]{1,3}(\\.[0-9]{1,3}){3}$", s)) return -1;
    if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return -1;
    if (a > 255 || b > 255 || c > 255 || d > 255) return -1;
    snprintf(buf, sizeof buf, "%d.%d.%d.%d", a, b, c, d);
    if (strcmp(buf, s) != 0) return -1;
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    if (out_is_v6) *out_is_v6 = 0;
    return 0;
  }
  if (strchr(s, '.')) return -1;
  int hextets[8];
  int hn = 0;
  int dcol_pos = -1;
  const char *p = s;
  while (*p) {
    if (p[0] == ':' && p[1] == ':') {
      if (dcol_pos >= 0) return -1;
      dcol_pos = hn;
      p += 2;
      if (!*p) break;
      continue;
    }
    const char *start = p;
    while (*p && *p != ':') p++;
    size_t len = (size_t)(p - start);
    if (len == 0 || len > 4 || hn >= 8) return -1;
    int val = 0;
    for (size_t i = 0; i < len; i++) {
      int hv = hexval(start[i]);
      if (hv < 0) return -1;
      val = (val << 4) | hv;
    }
    hextets[hn++] = val;
    if (*p == ':') {
      if (p[1] == ':') continue; /* "::" handled at loop top */
      p++;
    }
  }
  if (dcol_pos < 0) {
    if (hn != 8) return -1;
    for (int i = 0; i < 8; i++) {
      out[2 * i] = (uint8_t)(hextets[i] >> 8);
      out[2 * i + 1] = (uint8_t)(hextets[i] & 0xff);
    }
  } else {
    if (hn >= 8) return -1;
    int zeros = 8 - hn;
    int idx = 0;
    for (int i = 0; i < dcol_pos; i++) {
      out[2 * idx] = (uint8_t)(hextets[i] >> 8);
      out[2 * idx + 1] = (uint8_t)(hextets[i] & 0xff);
      idx++;
    }
    for (int i = 0; i < zeros; i++) {
      out[2 * idx] = 0;
      out[2 * idx + 1] = 0;
      idx++;
    }
    for (int i = dcol_pos; i < hn; i++) {
      out[2 * idx] = (uint8_t)(hextets[i] >> 8);
      out[2 * idx + 1] = (uint8_t)(hextets[i] & 0xff);
      idx++;
    }
  }
  if (out_is_v6) *out_is_v6 = 1;
  return 0;
}

/* toNumber()-style integer conversion for !o / !x / !unix tagged values and
   native ints. Glyphs keep their original form on decode; this helper yields
   the integer they represent. */
int xun_to_int(const xun_value *v, int64_t *out) {
  if (!v || !out) return -1;
  if (v->kind == XUN_INT) {
    *out = v->u.i;
    return 0;
  }
  if (v->kind != XUN_TAGGED) return -1;
  const char *tag = v->u.tagged.tag;
  const char *g = v->u.tagged.value;
  if (strcmp(tag, "o") == 0) {
    if (!g[0] || !fullmatch("^[0-7]+$", g)) return -1;
    *out = (int64_t)strtoll(g, NULL, 8);
    return 0;
  }
  if (strcmp(tag, "x") == 0) {
    size_t len = strlen(g);
    char *s = malloc(len + 1);
    if (!s) return -1;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
      if (g[i] != '_') s[j++] = g[i];
    }
    s[j] = 0;
    int ok = j > 0 && fullmatch("^[0-9A-Fa-f]+$", s);
    if (ok) *out = (int64_t)strtoull(s, NULL, 16);
    free(s);
    return ok ? 0 : -1;
  }
  if (strcmp(tag, "unix") == 0) {
    size_t len = strlen(g);
    char *s = malloc(len + 1);
    if (!s) return -1;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
      if (g[i] != '_') s[j++] = g[i];
    }
    s[j] = 0;
    int ok = fullmatch("^-?[0-9]+$", s) && !(s[0] == '0' && s[1] && isdigit((unsigned char)s[1]))
             && !(s[0] == '-' && s[1] == '0' && s[2] && isdigit((unsigned char)s[2]));
    if (ok) *out = (int64_t)strtoll(s, NULL, 10);
    free(s);
    return ok ? 0 : -1;
  }
  if (strcmp(tag, "n") == 0 || strcmp(tag, "i") == 0) {
    size_t len = strlen(g);
    char *s = malloc(len + 1);
    if (!s) return -1;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
      if (g[i] != '_') s[j++] = g[i];
    }
    s[j] = 0;
    int ok = fullmatch("^-?[0-9]+$", s) && !(s[0] == '0' && s[1] && isdigit((unsigned char)s[1]))
             && !(s[0] == '-' && s[1] == '0' && s[2] && isdigit((unsigned char)s[2]));
    if (ok) *out = (int64_t)strtoll(s, NULL, 10);
    free(s);
    return ok ? 0 : -1;
  }
  return -1;
}
