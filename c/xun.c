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

static line *peek(parser *p) {
  return p->i < p->nlines ? &p->lines[p->i] : NULL;
}

static void skip_noise(parser *p) {
  while (peek(p)) {
    line *l = peek(p);
    if (l->blank || l->text[0] == '#') p->i++;
    else break;
  }
}

static int is_list_item(line *l) {
  return strcmp(l->text, "-") == 0 || starts_with(l->text, "- ");
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
static xun_value *parse_value(parser *p, const char *raw, int parent_indent, int line_no, int depth);
static xun_value *parse_dict(parser *p, int indent, int depth);
static xun_value *parse_list(parser *p, int indent, int depth, const char *item_tag);

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
    return vint(p, strtoll(s, NULL, 16));
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
    return vint(p, strtoll(glyph, NULL, 8));
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
    if (!glyph[0] || !fullmatch("^([0-9]+d)?([0-9]+h)?([0-9]+m)?([0-9]+(\\.[0-9]+)?s)?$", glyph)) {
      fail(p, n, "invalid duration");
    }
    return vtagged(p, "du", glyph);
  }
  if (strcmp(tag, "sz") == 0) {
    if (!fullmatch("^[0-9]+(\\.[0-9]+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$", glyph)) {
      fail(p, n, "invalid data size");
    }
    return vtagged(p, "sz", glyph);
  }
  if (strcmp(tag, "unix") == 0) return parse_unix(p, glyph, n);
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

static char *match_multiline(parser *p, const char *raw) {
  if (strcmp(raw, "|") == 0) return astrdup(p, "|");
  if (raw[0] == '|' && is_ident(raw + 1)) return astrdup(p, raw + 1);
  return NULL;
}

static xun_value *read_multiline(parser *p, int parent_indent, const char *tag, const char *closer, int line_no) {
  int base = parent_indent + 2;
  char **parts = NULL;
  size_t nparts = 0;
  while (peek(p)) {
    line *l = peek(p);
    char *stripped = rstrip_space_tab(p, l->raw);
    int ind = leading_spaces(l->raw);
    char *content = stripped + ind;
    if (!l->blank && ind == parent_indent && strcmp(content, closer) == 0) {
      p->i++;
      size_t total = 0;
      for (size_t j = 0; j < nparts; j++) total += strlen(parts[j]) + 1;
      char *s = areq(p, total ? total : 1);
      s[0] = 0;
      for (size_t j = 0; j < nparts; j++) {
        strcat(s, parts[j]);
        if (j + 1 < nparts) strcat(s, "\n");
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

static xun_value *parse_empty_or_nested(parser *p, int parent_indent, int line_no, int depth, const char *item_tag) {
  (void)line_no;
  skip_noise(p);
  line *n = peek(p);
  int child = parent_indent + 2;
  if (!n || n->blank || n->indent <= parent_indent) {
    if (item_tag) return vlist(p);
    return vstr(p, "");
  }
  if (n->indent != child) fail(p, n->n, "child indent must be parent + 2");
  if (is_list_item(n)) return parse_list(p, child, depth, item_tag);
  if (item_tag) fail(p, n->n, "!%s[] expected list items", item_tag);
  return parse_dict(p, child, depth);
}

static xun_value *parse_tagged(parser *p, const char *raw, int parent_indent, int line_no, int depth) {
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
    if (!inner[0]) return parse_empty_or_nested(p, parent_indent, line_no, depth, tag);
    xun_value *arr = vlist(p);
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
  char *closer = match_multiline(p, body);
  if (closer) {
    xun_value *res = read_multiline(p, parent_indent, NULL, closer, line_no);
    if (strcmp(tag, "s") == 0) return res;
    return apply_tag(p, tag, res->u.str, line_no);
  }
  if (strcmp(tag, "s") == 0) return vstr(p, body);
  return apply_tag(p, tag, body, line_no);
}

static xun_value *parse_value(parser *p, const char *raw, int parent_indent, int line_no, int depth) {
  if (strcmp(raw, "[]") == 0) return vlist(p);
  if (strcmp(raw, "{}") == 0) return vdict(p);
  char *closer = match_multiline(p, raw);
  if (closer) return read_multiline(p, parent_indent, NULL, closer, line_no);
  if (raw[0] == '!') return parse_tagged(p, raw, parent_indent, line_no, depth);
  if (!raw[0]) return parse_empty_or_nested(p, parent_indent, line_no, depth, NULL);
  return vstr(p, raw);
}

static void split_key(parser *p, const char *text, int n, char **out_k, char **out_v) {
  const char *pos = strstr(text, ": ");
  if (pos && pos != text) {
    *out_k = astrndup(p, text, pos - text);
    *out_v = astrdup(p, pos + 2);
    return;
  }
  size_t len = strlen(text);
  if (len > 1 && text[len - 1] == ':') {
    *out_k = astrndup(p, text, len - 1);
    *out_v = astrdup(p, "");
    return;
  }
  fail(p, n, "expected ': ' or trailing ':'");
}

static xun_value *parse_dict(parser *p, int indent, int depth) {
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
    if (is_list_item(l)) fail(p, l->n, "cannot mix list items into a dictionary");
    char *key = NULL, *rest = NULL;
    split_key(p, l->text, l->n, &key, &rest);
    if (dict_has(obj, key)) fail(p, l->n, "duplicate key '%s'", key);
    p->i++;
    xun_value *val = parse_value(p, rest, indent, l->n, depth + 1);
    dict_put(p, obj, key, val);
  }
  return obj;
}

static xun_value *parse_list(parser *p, int indent, int depth, const char *item_tag) {
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
    if (!is_list_item(l)) fail(p, l->n, "cannot mix dictionary keys into a list");
    const char *rest = strcmp(l->text, "-") == 0 ? "" : l->text + 2;
    p->i++;
    xun_value *val = parse_value(p, rest, indent, l->n, depth + 1);
    if (item_tag) val = apply_tag(p, item_tag, glyph_of(p, val, l->n), l->n);
    list_push(p, arr, val);
  }
  return arr;
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
    line l = { raw, ind, text, n, text[0] == 0 };
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
    return -1;
  }
  if (starts_with(source, "\xef\xbb\xbf")) source += 3;
  p.lines = split_lines(&p, source, &p.nlines);
  skip_noise(&p);
  xun_value *root = NULL;
  if (!peek(&p)) {
    root = vdict(&p);
  } else {
    line *first = peek(&p);
    if (first->indent != 0) fail(&p, first->n, "document must start at indent 0");
    if (is_list_item(first)) fail(&p, first->n, "root must be a dictionary");
    root = parse_dict(&p, 0, 0);
  }
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

static int validate_key_c(const char *key) {
  if (!key || !key[0]) return -1;
  if (strchr(key, '\n') || strchr(key, '\r') || strstr(key, ": ") || (key[0] && key[strlen(key) - 1] == ':')) {
    return -1;
  }
  return 0;
}

static int encode_value_node(const xun_value *v, int depth, str_buf *b);

static int encode_dict_body(const xun_value *dict, int depth, str_buf *b) {
  if (depth > MAX_DEPTH) return -1;
  for (size_t i = 0; i < dict->u.dict.len; i++) {
    const char *key = dict->u.dict.items[i].key;
    const xun_value *val = dict->u.dict.items[i].val;
    if (validate_key_c(key) != 0) return -1;
    buf_append_indent(b, depth);
    buf_append_str(b, key);
    buf_append_str(b, ":");

    if (!val) {
      buf_append_str(b, "\n");
      continue;
    }

    if (val->kind == XUN_DICT) {
      if (val->u.dict.len == 0) {
        buf_append_str(b, " {}\n");
      } else {
        buf_append_str(b, "\n");
        if (encode_dict_body(val, depth + 1, b) != 0) return -1;
      }
    } else if (val->kind == XUN_LIST) {
      if (val->u.list.len == 0) {
        buf_append_str(b, " []\n");
      } else {
        buf_append_str(b, "\n");
        if (encode_value_node(val, depth + 1, b) != 0) return -1;
      }
    } else if (val->kind == XUN_STRING) {
      const char *s = val->u.str;
      if (strchr(s, '\n') || strchr(s, '\r')) {
        buf_append_str(b, " |\n");
        const char *cur = s;
        while (*cur) {
          const char *nl = strchr(cur, '\n');
          size_t line_len = nl ? (size_t)(nl - cur) : strlen(cur);
          if (line_len > 0 && cur[line_len - 1] == '\r') line_len--;
          buf_append_indent(b, depth + 1);
          char *line_buf = malloc(line_len + 1);
          memcpy(line_buf, cur, line_len);
          line_buf[line_len] = 0;
          buf_append_str(b, line_buf);
          buf_append_str(b, "\n");
          free(line_buf);
          if (!nl) break;
          cur = nl + 1;
        }
        buf_append_indent(b, depth);
        buf_append_str(b, "|\n");
      } else if (!s[0]) {
        buf_append_str(b, "\n");
      } else {
        if (s[0] == '!' || strcmp(s, "[]") == 0 || strcmp(s, "{}") == 0 || s[0] == '|') {
          buf_append_str(b, " !s ");
          buf_append_str(b, s);
          buf_append_str(b, "\n");
        } else {
          buf_append_str(b, " ");
          buf_append_str(b, s);
          buf_append_str(b, "\n");
        }
      }
    } else if (val->kind == XUN_INT) {
      char numbuf[64];
      snprintf(numbuf, sizeof numbuf, " !i %lld\n", (long long)val->u.i);
      buf_append_str(b, numbuf);
    } else if (val->kind == XUN_FLOAT) {
      char numbuf[64];
      snprintf(numbuf, sizeof numbuf, "%.17g", val->u.f);
      if (!strchr(numbuf, '.') && !strchr(numbuf, 'e') && !strchr(numbuf, 'E')) {
        strcat(numbuf, ".0");
      }
      buf_append_str(b, " !f ");
      buf_append_str(b, numbuf);
      buf_append_str(b, "\n");
    } else if (val->kind == XUN_BOOL) {
      buf_append_str(b, val->u.b ? " !b true\n" : " !b false\n");
    } else if (val->kind == XUN_BYTES) {
      buf_append_str(b, " !xb ");
      for (size_t j = 0; j < val->u.bytes.len; j++) {
        char h[3];
        snprintf(h, sizeof h, "%02X", val->u.bytes.data[j]);
        buf_append_str(b, h);
      }
      buf_append_str(b, "\n");
    } else if (val->kind == XUN_TAGGED) {
      const char *s = val->u.tagged.value;
      if (strchr(s, '\n') || strchr(s, '\r')) {
        buf_append_str(b, " !");
        buf_append_str(b, val->u.tagged.tag);
        buf_append_str(b, " |\n");
        const char *cur = s;
        while (*cur) {
          const char *nl = strchr(cur, '\n');
          size_t line_len = nl ? (size_t)(nl - cur) : strlen(cur);
          if (line_len > 0 && cur[line_len - 1] == '\r') line_len--;
          buf_append_indent(b, depth + 1);
          char *line_buf = malloc(line_len + 1);
          memcpy(line_buf, cur, line_len);
          line_buf[line_len] = 0;
          buf_append_str(b, line_buf);
          buf_append_str(b, "\n");
          free(line_buf);
          if (!nl) break;
          cur = nl + 1;
        }
        buf_append_indent(b, depth);
        buf_append_str(b, "|\n");
      } else {
        buf_append_str(b, " !");
        buf_append_str(b, val->u.tagged.tag);
        buf_append_str(b, " ");
        buf_append_str(b, val->u.tagged.value);
        buf_append_str(b, "\n");
      }
    } else {
      return -1;
    }
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
      if (!item) {
        buf_append_str(b, "\n");
        continue;
      }
      if (item->kind == XUN_DICT) {
        if (item->u.dict.len == 0) {
          buf_append_str(b, " {}\n");
        } else {
          buf_append_str(b, "\n");
          if (encode_dict_body(item, depth + 1, b) != 0) return -1;
        }
      } else if (item->kind == XUN_LIST) {
        if (item->u.list.len == 0) {
          buf_append_str(b, " []\n");
        } else {
          buf_append_str(b, "\n");
          if (encode_value_node(item, depth + 1, b) != 0) return -1;
        }
      } else if (item->kind == XUN_STRING) {
        const char *s = item->u.str;
        if (strchr(s, '\n') || strchr(s, '\r')) {
          buf_append_str(b, " |\n");
          const char *cur = s;
          while (*cur) {
            const char *nl = strchr(cur, '\n');
            size_t line_len = nl ? (size_t)(nl - cur) : strlen(cur);
            if (line_len > 0 && cur[line_len - 1] == '\r') line_len--;
            buf_append_indent(b, depth + 1);
            char *line_buf = malloc(line_len + 1);
            memcpy(line_buf, cur, line_len);
            line_buf[line_len] = 0;
            buf_append_str(b, line_buf);
            buf_append_str(b, "\n");
            free(line_buf);
            if (!nl) break;
            cur = nl + 1;
          }
          buf_append_indent(b, depth);
          buf_append_str(b, "|\n");
        } else if (!s[0]) {
          buf_append_str(b, "\n");
        } else {
          if (s[0] == '!' || strcmp(s, "[]") == 0 || strcmp(s, "{}") == 0 || s[0] == '|') {
            buf_append_str(b, " !s ");
            buf_append_str(b, s);
            buf_append_str(b, "\n");
          } else {
            buf_append_str(b, " ");
            buf_append_str(b, s);
            buf_append_str(b, "\n");
          }
        }
      } else if (item->kind == XUN_INT) {
        char numbuf[64];
        snprintf(numbuf, sizeof numbuf, " !i %lld\n", (long long)item->u.i);
        buf_append_str(b, numbuf);
      } else if (item->kind == XUN_FLOAT) {
        char numbuf[64];
        snprintf(numbuf, sizeof numbuf, "%.17g", item->u.f);
        if (!strchr(numbuf, '.') && !strchr(numbuf, 'e') && !strchr(numbuf, 'E')) {
          strcat(numbuf, ".0");
        }
        buf_append_str(b, " !f ");
        buf_append_str(b, numbuf);
        buf_append_str(b, "\n");
      } else if (item->kind == XUN_BOOL) {
        buf_append_str(b, item->u.b ? " !b true\n" : " !b false\n");
      } else if (item->kind == XUN_BYTES) {
        buf_append_str(b, " !xb ");
        for (size_t j = 0; j < item->u.bytes.len; j++) {
          char h[3];
          snprintf(h, sizeof h, "%02X", item->u.bytes.data[j]);
          buf_append_str(b, h);
        }
        buf_append_str(b, "\n");
      } else if (item->kind == XUN_TAGGED) {
        const char *s = item->u.tagged.value;
        if (strchr(s, '\n') || strchr(s, '\r')) {
          buf_append_str(b, " !");
          buf_append_str(b, item->u.tagged.tag);
          buf_append_str(b, " |\n");
          const char *cur = s;
          while (*cur) {
            const char *nl = strchr(cur, '\n');
            size_t line_len = nl ? (size_t)(nl - cur) : strlen(cur);
            if (line_len > 0 && cur[line_len - 1] == '\r') line_len--;
            buf_append_indent(b, depth + 1);
            char *line_buf = malloc(line_len + 1);
            memcpy(line_buf, cur, line_len);
            line_buf[line_len] = 0;
            buf_append_str(b, line_buf);
            buf_append_str(b, "\n");
            free(line_buf);
            if (!nl) break;
            cur = nl + 1;
          }
          buf_append_indent(b, depth);
          buf_append_str(b, "|\n");
        } else {
          buf_append_str(b, " !");
          buf_append_str(b, item->u.tagged.tag);
          buf_append_str(b, " ");
          buf_append_str(b, item->u.tagged.value);
          buf_append_str(b, "\n");
        }
      } else {
        return -1;
      }
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

int xun_parse_duration_ms(const char *s, uint64_t *out_ms) {
  if (!s || !*s || !out_ms) return -1;
  uint64_t total = 0;
  const char *p = s;
  int matched = 0;
  while (*p) {
    char *end = NULL;
    double n = strtod(p, &end);
    if (end == p || !end) return -1;
    matched = 1;
    if (*end == 'd') {
      total += (uint64_t)(n * 86400.0 * 1000.0);
      p = end + 1;
    } else if (*end == 'h') {
      total += (uint64_t)(n * 3600.0 * 1000.0);
      p = end + 1;
    } else if (*end == 'm') {
      if (end[1] == 's') {
        total += (uint64_t)n;
        p = end + 2;
      } else {
        total += (uint64_t)(n * 60.0 * 1000.0);
        p = end + 1;
      }
    } else if (*end == 's') {
      total += (uint64_t)(n * 1000.0);
      p = end + 1;
    } else {
      return -1;
    }
  }
  if (!matched) return -1;
  *out_ms = total;
  return 0;
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
