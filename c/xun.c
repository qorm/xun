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

typedef struct env_entry {
  char *name;
  xun_value *val;
  struct env_entry *next;
} env_entry;

typedef struct {
  jmp_buf jmp;
  xun_error *err;
  arena *a;
  line *lines;
  size_t nlines;
  size_t i;
  env_entry *env;
  env_entry *resolving;
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

static int is_whole_ref(const char *raw) {
  return fullmatch("^\\$[A-Za-z_][A-Za-z0-9_]*$", raw);
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
    char tmp[256];
    if (strlen(s) >= sizeof tmp) return 0;
    memcpy(tmp, s, strlen(s) + 1);
    int parts = 0, empty = 0;
    char *save = NULL;
    /* split keeping empties: manual */
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
    (void)save;
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
        out[ol++] = (uint8_t)(((c & 0x3) << 6) | d);
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
    if (!fullmatch("^[0-9A-Fa-f]+$", s)) fail(p, n, "invalid hex integer");
    return vint(p, (int64_t)strtoull(s, NULL, 16));
  }
  if (strcmp(tag, "xb") == 0) {
    size_t glen = strlen(glyph);
    char *s = areq(p, glen + 1);
    size_t sl = 0;
    for (size_t i = 0; i < glen; i++) if (glyph[i] != '_') s[sl++] = glyph[i];
    s[sl] = 0;
    if (sl == 0 || sl % 2 || !fullmatch("^[0-9A-Fa-f]+$", s)) {
      fail(p, n, "hex bytes must be an even number of digits");
    }
    uint8_t *out = areq(p, sl / 2);
    for (size_t i = 0; i < sl / 2; i++) {
      char buf[3] = {s[i * 2], s[i * 2 + 1], 0};
      out[i] = (uint8_t)strtoul(buf, NULL, 16);
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
    if (strcmp(glyph, "Z") && strcmp(glyph, "UTC") &&
        !fullmatch("^[+-][0-9]{2}:[0-9]{2}$", glyph) &&
        !fullmatch("^[A-Za-z_]+(/[A-Za-z0-9_+-]+)+$", glyph)) {
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
    if (starts_with(glyph, "U+") && fullmatch("^U\\+[0-9A-Fa-f]{4,6}$", glyph)) {
      unsigned long cp = strtoul(glyph + 2, NULL, 16);
      if (cp > 0x10FFFF) fail(p, n, "invalid code point");
      char utf8[5] = {0};
      if (cp < 0x80) {
        utf8[0] = (char)cp;
      } else if (cp < 0x800) {
        utf8[0] = (char)(0xC0 | (cp >> 6));
        utf8[1] = (char)(0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        utf8[0] = (char)(0xE0 | (cp >> 12));
        utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (cp & 0x3F));
      } else {
        utf8[0] = (char)(0xF0 | (cp >> 18));
        utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (cp & 0x3F));
      }
      return vtagged(p, "c", utf8);
    }
    /* single UTF-8 scalar: count code points */
    const unsigned char *u = (const unsigned char *)glyph;
    if (!u[0]) fail(p, n, "character must be a single scalar");
    int more = 0;
    if (u[0] < 0x80) more = 0;
    else if ((u[0] & 0xE0) == 0xC0) more = 1;
    else if ((u[0] & 0xF0) == 0xE0) more = 2;
    else if ((u[0] & 0xF8) == 0xF0) more = 3;
    else fail(p, n, "character must be a single scalar");
    if ((int)strlen(glyph) != more + 1) fail(p, n, "character must be a single scalar");
    return vtagged(p, "c", glyph);
  }
  return vtagged(p, tag, glyph);
}

static char *glyph_of(parser *p, xun_value *v, int line_no) {
  char buf[64];
  if (v->kind == XUN_TAGGED) return v->u.tagged.value;
  if (v->kind == XUN_BYTES) {
    char *hex = areq(p, v->u.bytes.len * 2 + 1);
    for (size_t i = 0; i < v->u.bytes.len; i++) sprintf(hex + i * 2, "%02x", v->u.bytes.data[i]);
    return hex;
  }
  if (v->kind == XUN_STRING) return v->u.str;
  if (v->kind == XUN_BOOL) return v->u.b ? "true" : "false";
  if (v->kind == XUN_INT) {
    snprintf(buf, sizeof buf, "%lld", (long long)v->u.i);
    return astrdup(p, buf);
  }
  if (v->kind == XUN_FLOAT) {
    snprintf(buf, sizeof buf, "%g", v->u.f);
    return astrdup(p, buf);
  }
  fail(p, line_no, "cannot interpolate a collection");
  return NULL;
}

static xun_value *env_get(parser *p, const char *name) {
  for (env_entry *e = p->env; e; e = e->next) {
    if (strcmp(e->name, name) == 0) return e->val;
  }
  return NULL;
}

static int resolving_has(parser *p, const char *name) {
  for (env_entry *e = p->resolving; e; e = e->next) {
    if (strcmp(e->name, name) == 0) return 1;
  }
  return 0;
}

static xun_value *lookup(parser *p, const char *name, int line_no) {
  xun_value *v = env_get(p, name);
  if (!v) fail(p, line_no, "undefined variable $%s", name);
  if (resolving_has(p, name)) fail(p, line_no, "cyclic variable $%s", name);
  if (v->kind == XUN_STRING && is_whole_ref(v->u.str)) {
    env_entry *e = areq(p, sizeof(env_entry));
    e->name = astrdup(p, name);
    e->next = p->resolving;
    p->resolving = e;
    xun_value *r = lookup(p, v->u.str + 1, line_no);
    p->resolving = e->next;
    for (env_entry *it = p->env; it; it = it->next) {
      if (strcmp(it->name, name) == 0) {
        it->val = r;
        break;
      }
    }
    return r;
  }
  return v;
}

static xun_value *interpolate(parser *p, const char *s, int line_no) {
  size_t cap = strlen(s) + 1;
  char *out = areq(p, cap);
  size_t ol = 0;
  const char *cur = s;
  while (*cur) {
    const char *d = strstr(cur, "${");
    if (!d) {
      size_t rest = strlen(cur);
      while (ol + rest + 1 > cap) {
        cap *= 2;
        char *n = areq(p, cap);
        memcpy(n, out, ol);
        out = n;
      }
      memcpy(out + ol, cur, rest);
      ol += rest;
      break;
    }
    size_t pre = (size_t)(d - cur);
    while (ol + pre + 1 > cap) {
      cap *= 2;
      char *n = areq(p, cap);
      memcpy(n, out, ol);
      out = n;
    }
    memcpy(out + ol, cur, pre);
    ol += pre;
    const char *end = strchr(d + 2, '}');
    if (!end) {
      /* literal ${ */
      while (ol + 3 > cap) {
        cap *= 2;
        char *n = areq(p, cap);
        memcpy(n, out, ol);
        out = n;
      }
      memcpy(out + ol, "${", 2);
      ol += 2;
      cur = d + 2;
      continue;
    }
    size_t nlen = (size_t)(end - (d + 2));
    char name[64];
    if (nlen == 0 || nlen >= sizeof name) fail(p, line_no, "invalid interpolation");
    memcpy(name, d + 2, nlen);
    name[nlen] = 0;
    if (!is_ident(name)) {
      /* not a valid name; keep literal — JS regex only replaces valid idents */
      while (ol + 2 > cap) {
        cap *= 2;
        char *n = areq(p, cap);
        memcpy(n, out, ol);
        out = n;
      }
      memcpy(out + ol, "${", 2);
      ol += 2;
      cur = d + 2;
      continue;
    }
    char *g = glyph_of(p, lookup(p, name, line_no), line_no);
    size_t gl = strlen(g);
    while (ol + gl + 1 > cap) {
      cap *= 2;
      char *n = areq(p, cap);
      memcpy(n, out, ol);
      out = n;
    }
    memcpy(out + ol, g, gl);
    ol += gl;
    cur = end + 1;
  }
  out[ol] = 0;
  return vstr(p, out);
}

static const char *match_multiline(const char *raw) {
  if (strcmp(raw, "|") == 0) return "|";
  if (raw[0] == '|' && is_ident(raw + 1)) return raw + 1;
  return NULL;
}

static void split_key(parser *p, const char *text, int n, char **key, char **rest) {
  const char *idx = strstr(text, ": ");
  if (idx && idx > text) {
    *key = astrndup(p, text, (size_t)(idx - text));
    *rest = astrdup(p, idx + 2);
    return;
  }
  size_t len = strlen(text);
  if (len > 1 && text[len - 1] == ':') {
    *key = astrndup(p, text, len - 1);
    *rest = astrdup(p, "");
    return;
  }
  fail(p, n, "expected ': ' or trailing ':'");
}

static xun_value *read_multiline(parser *p, int parent_indent, const char *closer, int line_no) {
  int base = parent_indent + 2;
  char **parts = NULL;
  size_t nparts = 0, cap = 0;
  while (peek(p)) {
    line *l = peek(p);
    char *stripped = rstrip_space_tab(p, l->raw);
    const char *content = stripped;
    while (*content == ' ') content++;
    int ind = leading_spaces(l->raw);
    if (!l->blank && ind == parent_indent && strcmp(content, closer) == 0) {
      p->i++;
      size_t total = nparts ? nparts - 1 : 0;
      for (size_t i = 0; i < nparts; i++) total += strlen(parts[i]);
      char *s = areq(p, total + 1);
      s[0] = 0;
      for (size_t i = 0; i < nparts; i++) {
        if (i) strcat(s, "\n");
        strcat(s, parts[i]);
      }
      return vstr(p, s);
    }
    if (l->blank) {
      if (nparts == cap) {
        cap = cap ? cap * 2 : 8;
        char **np = areq(p, cap * sizeof(char *));
        if (parts) memcpy(np, parts, nparts * sizeof(char *));
        parts = np;
      }
      parts[nparts++] = astrdup(p, "");
      p->i++;
      continue;
    }
    if (ind < base) fail(p, l->n, "multiline body must indent +2, or close at opener indent");
    if (strchr(l->raw, '\t')) fail(p, l->n, "tab is not allowed");
    if (nparts == cap) {
      cap = cap ? cap * 2 : 8;
      char **np = areq(p, cap * sizeof(char *));
      if (parts) memcpy(np, parts, nparts * sizeof(char *));
      parts = np;
    }
    size_t rlen = strlen(l->raw);
    parts[nparts++] = astrdup(p, rlen >= (size_t)base ? l->raw + base : "");
    p->i++;
  }
  fail(p, line_no, "unclosed multiline block");
  return NULL;
}

static xun_value *parse_empty_or_nested(parser *p, int parent_indent, int depth, const char *item_tag) {
  skip_noise(p);
  line *n = peek(p);
  int child = parent_indent + 2;
  if (!n || n->blank || n->indent <= parent_indent) {
    return item_tag ? vlist(p) : vstr(p, "");
  }
  if (n->indent != child) fail(p, n->n, "child indent must be parent + 2");
  if (is_list_item(n)) return parse_list(p, child, depth, item_tag);
  if (item_tag) fail(p, n->n, "!%s[] expected list items", item_tag);
  return parse_dict(p, child, depth);
}

static xun_value *parse_tagged(parser *p, const char *raw, int parent_indent, int line_no, int depth) {
  if (raw[0] != '!') fail(p, line_no, "invalid type tag");
  const char *s = raw + 1;
  size_t i = 0;
  if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) fail(p, line_no, "invalid type tag");
  i = 1;
  while (isalnum((unsigned char)s[i]) || s[i] == '_') i++;
  char *tag = astrndup(p, s, i);
  const char *rest = s + i;
  if (rest[0] == '[') {
    size_t rlen = strlen(rest);
    if (strcmp(tag, "s") == 0 && strcmp(rest, "[]") != 0) fail(p, line_no, "string arrays cannot use compact form");
    if (rlen < 2 || rest[rlen - 1] != ']') fail(p, line_no, "unclosed compact array");
    char *inner = astrndup(p, rest + 1, rlen - 2);
    if (inner[0] == 0) return parse_empty_or_nested(p, parent_indent, depth, tag);
    xun_value *arr = vlist(p);
    char *save = inner;
    while (*save) {
      char *comma = strchr(save, ',');
      char *item;
      if (comma) {
        *comma = 0;
        item = save;
        save = comma + 1;
      } else {
        item = save;
        save += strlen(save);
      }
      while (*item == ' ' || *item == '\t') item++;
      char *end = item + strlen(item);
      while (end > item && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
      list_push(p, arr, apply_tag(p, tag, item, line_no));
    }
    return arr;
  }
  if (rest[0] == 0) fail(p, line_no, "missing value for !%s", tag);
  if (rest[0] != ' ') fail(p, line_no, "expected space after type tag");
  const char *body = rest + 1;
  const char *ml = match_multiline(body);
  if (ml) {
    xun_value *text = read_multiline(p, parent_indent, ml, line_no);
    if (strcmp(tag, "s") == 0) return text;
    return apply_tag(p, tag, text->u.str, line_no);
  }
  if (strcmp(tag, "s") == 0) return vstr(p, body);
  if (is_whole_ref(body)) return lookup(p, body + 1, line_no);
  return apply_tag(p, tag, body, line_no);
}

static xun_value *parse_value(parser *p, const char *raw, int parent_indent, int line_no, int depth) {
  if (strcmp(raw, "[]") == 0) return vlist(p);
  if (strcmp(raw, "{}") == 0) return vdict(p);
  const char *ml = match_multiline(raw);
  if (ml) return read_multiline(p, parent_indent, ml, line_no);
  if (raw[0] == '!') return parse_tagged(p, raw, parent_indent, line_no, depth);
  if (raw[0] == 0) return parse_empty_or_nested(p, parent_indent, depth, NULL);
  if (is_whole_ref(raw)) return lookup(p, raw + 1, line_no);
  return interpolate(p, raw, line_no);
}

static xun_value *parse_dict(parser *p, int indent, int depth) {
  if (depth > MAX_DEPTH) fail(p, peek(p) ? peek(p)->n : 0, "nesting exceeds 64");
  xun_value *obj = vdict(p);
  while (peek(p)) {
    skip_noise(p);
    line *l = peek(p);
    if (!l || l->blank) break;
    if (l->indent < indent) break;
    if (l->indent > indent) fail(p, l->n, "invalid indent jump");
    if (l->text[0] == '$' && indent == 0) fail(p, l->n, "variable definitions only allowed at file start");
    if (is_list_item(l)) fail(p, l->n, "cannot mix list items into a dictionary");
    char *key = NULL, *rest = NULL;
    split_key(p, l->text, l->n, &key, &rest);
    if (dict_has(obj, key)) fail(p, l->n, "duplicate key '%s'", key);
    p->i++;
    dict_put(p, obj, key, parse_value(p, rest, indent, l->n, depth + 1));
  }
  return obj;
}

static xun_value *parse_list(parser *p, int indent, int depth, const char *item_tag) {
  if (depth > MAX_DEPTH) fail(p, peek(p) ? peek(p)->n : 0, "nesting exceeds 64");
  xun_value *arr = vlist(p);
  while (peek(p)) {
    skip_noise(p);
    line *l = peek(p);
    if (!l || l->blank) break;
    if (l->indent < indent) break;
    if (l->indent > indent) fail(p, l->n, "invalid indent jump");
    if (!is_list_item(l)) fail(p, l->n, "cannot mix dictionary keys into a list");
    const char *rest = strcmp(l->text, "-") == 0 ? "" : l->text + 2;
    int ln = l->n;
    p->i++;
    xun_value *val = parse_value(p, rest, indent, ln, depth + 1);
    if (item_tag) val = apply_tag(p, item_tag, glyph_of(p, val, ln), ln);
    list_push(p, arr, val);
  }
  return arr;
}

static void parse_vars(parser *p) {
  while (peek(p)) {
    line *l = peek(p);
    if (l->blank || l->text[0] == '#') {
      p->i++;
      continue;
    }
    if (l->indent != 0 || l->text[0] != '$') break;
    if (l->text[1] == 0) fail(p, l->n, "invalid variable definition");
    const char *s = l->text + 1;
    size_t i = 0;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) fail(p, l->n, "invalid variable definition");
    i = 1;
    while (isalnum((unsigned char)s[i]) || s[i] == '_') i++;
    if (s[i] != ':') fail(p, l->n, "invalid variable definition");
    char *name = astrndup(p, s, i);
    const char *after = s + i + 1;
    if (after[0] && after[0] != ' ') fail(p, l->n, "expected ': ' in variable definition");
    if (env_get(p, name)) fail(p, l->n, "duplicate variable $%s", name);
    p->i++;
    const char *raw = after[0] == ' ' ? after + 1 : "";
    env_entry *e = areq(p, sizeof(env_entry));
    e->name = name;
    e->val = parse_value(p, raw, 0, l->n, 1);
    e->next = p->env;
    p->env = e;
  }
}

static xun_value *parse_document(parser *p) {
  parse_vars(p);
  skip_noise(p);
  if (!peek(p)) return vdict(p);
  line *first = peek(p);
  if (first->indent != 0) fail(p, first->n, "document must start at indent 0");
  if (is_list_item(first)) fail(p, first->n, "root must be a dictionary");
  return parse_dict(p, 0, 0);
}

static line make_line(parser *p, const char *raw, int n) {
  int i = 0;
  while (raw[i] == ' ') i++;
  if (raw[i] == '\t') fail(p, n, "tab is not allowed");
  if (i % 2 != 0) fail(p, n, "indent must be a multiple of 2");
  char *text = rstrip_space_tab(p, raw + i);
  line l;
  l.raw = astrdup(p, raw);
  l.indent = i;
  l.text = text;
  l.n = n;
  l.blank = text[0] == 0;
  return l;
}

static void split_lines(parser *p, const char *source) {
  size_t len = strlen(source);
  if (len == 0) {
    p->lines = NULL;
    p->nlines = 0;
    return;
  }
  size_t cap = 16;
  p->lines = areq(p, cap * sizeof(line));
  p->nlines = 0;
  int n = 1;
  size_t start = 0;
  size_t i = 0;
  while (i <= len) {
    int at_end = i == len;
    char c = at_end ? 0 : source[i];
    if (!at_end && c != '\n' && c != '\r') {
      i++;
      continue;
    }
    if (p->nlines == cap) {
      cap *= 2;
      line *nl = areq(p, cap * sizeof(line));
      memcpy(nl, p->lines, p->nlines * sizeof(line));
      p->lines = nl;
    }
    char *raw = astrndup(p, source + start, i - start);
    if (c == '\r' && i + 1 < len && source[i + 1] == '\n') i++;
    p->lines[p->nlines++] = make_line(p, raw, n);
    n++;
    i++;
    start = i;
  }
}

static void arena_release(arena *a) {
  if (!a) return;
  for (size_t i = 0; i < a->n; i++) free(a->p[i]);
  free(a->p);
  free(a);
}

int xun_parse(const char *source, xun_value **out, xun_error *err) {
  if (out) *out = NULL;
  parser p;
  memset(&p, 0, sizeof p);
  p.err = err;
  if (err) {
    err->line = 0;
    err->message[0] = 0;
  }
  p.a = calloc(1, sizeof(arena));
  if (!p.a) {
    if (err) snprintf(err->message, sizeof err->message, "out of memory");
    return -1;
  }
  if (setjmp(p.jmp)) {
    arena_release(p.a);
    return -1;
  }
  if (!source) fail(&p, 0, "source must be a string");
  if (strlen(source) > MAX_BYTES) fail(&p, 0, "document exceeds 1MB");
  if (strchr(source, '\0') != source + strlen(source)) {
    /* strchr can't find interior NUL if we used strlen; check bytes */
  }
  for (const char *c = source; *c; c++) {
    /* walk */
  }
  /* reject interior NUL: caller may pass length-unknown C string so NUL already terminates.
     Spec: NUL in file is illegal; C strings cannot contain it. */
  if (source[0] == '\xEF' && source[1] == '\xBB' && source[2] == '\xBF') source += 3;
  /* UTF-8 BOM as bytes; also U+FEFF in UTF-8 is EF BB BF. */
  split_lines(&p, source);
  xun_value *doc = parse_document(&p);
  doc->arena = p.a;
  if (out) *out = doc;
  return 0;
}

int xun_parse_file(const char *path, xun_value **out, xun_error *err) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    if (err) {
      err->line = 0;
      snprintf(err->message, sizeof err->message, "cannot open file");
    }
    if (out) *out = NULL;
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    if (err) snprintf(err->message, sizeof err->message, "cannot read file");
    return -1;
  }
  long sz = ftell(f);
  if (sz < 0 || sz > MAX_BYTES) {
    fclose(f);
    if (err) snprintf(err->message, sizeof err->message, "document exceeds 1MB");
    return -1;
  }
  rewind(f);
  char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    if (err) snprintf(err->message, sizeof err->message, "out of memory");
    return -1;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    if (err) snprintf(err->message, sizeof err->message, "cannot read file");
    return -1;
  }
  fclose(f);
  for (long i = 0; i < sz; i++) {
    if (buf[i] == 0) {
      free(buf);
      if (err) {
        err->line = 0;
        snprintf(err->message, sizeof err->message, "NUL is not allowed");
      }
      return -1;
    }
  }
  buf[sz] = 0;
  int rc = xun_parse(buf, out, err);
  free(buf);
  return rc;
}

void xun_free(xun_value *v) {
  if (!v) return;
  arena_release((arena *)v->arena);
}
