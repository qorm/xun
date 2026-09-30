#include "xun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed = 0;

static void fail(const char *msg) {
  fprintf(stderr, "FAIL %s\n", msg);
  failed++;
}

static void expect_str(const xun_value *v, const char *want, const char *msg) {
  if (!v || v->kind != XUN_STRING || strcmp(v->u.str, want) != 0) {
    fail(msg);
  }
}

static void expect_int(const xun_value *v, int64_t want, const char *msg) {
  if (!v || v->kind != XUN_INT || v->u.i != want) fail(msg);
}

static void expect_tagged(const xun_value *v, const char *tag, const char *value, const char *msg) {
  if (!v || v->kind != XUN_TAGGED || strcmp(v->u.tagged.tag, tag) || strcmp(v->u.tagged.value, value)) {
    fail(msg);
  }
}

static void expect_err(const char *src, const char *msg) {
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse(src, &doc, &err) == 0) {
    xun_free(doc);
    fail(msg);
  }
}

static void test_example(const char *root) {
  char path[1024];
  snprintf(path, sizeof path, "%s/testdata/example.xun", root);
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse_file(path, &doc, &err) != 0) {
    fprintf(stderr, "parse file: %s\n", err.message);
    fail("example file");
    return;
  }
  const xun_value *server = xun_dict_get(doc, "server");
  expect_str(xun_dict_get(server, "host"), "localhost", "host");
  expect_int(xun_dict_get(server, "port"), 8080, "port");
  expect_tagged(xun_dict_get(server, "bind"), "ip", "::1", "bind");
  const xun_value *tls = xun_dict_get(server, "tls");
  expect_tagged(xun_dict_get(tls, "mode"), "o", "755", "mode");
  int64_t mode_n = 0;
  if (xun_to_int(xun_dict_get(tls, "mode"), &mode_n) != 0 || mode_n != 0755) fail("mode to_int");
  const xun_value *features = xun_dict_get(doc, "features");
  if (!features || features->kind != XUN_LIST || features->u.list.len != 2) fail("features");
  else {
    expect_str(features->u.list.items[0], "auth", "features[0]");
    expect_str(features->u.list.items[1], "cache", "features[1]");
  }
  const xun_value *ports = xun_dict_get(doc, "ports");
  if (!ports || ports->kind != XUN_LIST || ports->u.list.len != 3) fail("ports");
  else {
    expect_int(ports->u.list.items[0], 80, "ports[0]");
    expect_int(ports->u.list.items[1], 443, "ports[1]");
    expect_int(ports->u.list.items[2], 8080, "ports[2]");
  }
  expect_str(xun_dict_get(doc, "endpoint"), "https://api.example.com/v2/orders", "endpoint");
  expect_tagged(xun_dict_get(doc, "tz"), "tz", "Asia/Shanghai", "tz");
  expect_tagged(xun_dict_get(doc, "py"), "ver", "3.10", "py");
  const xun_value *color = xun_dict_get(doc, "color");
  if (!color || color->kind != XUN_BYTES || color->u.bytes.len != 3 ||
      color->u.bytes.data[0] != 0xff || color->u.bytes.data[1] != 0x00 || color->u.bytes.data[2] != 0xaa) {
    fail("color");
  }
  const xun_value *roles = xun_dict_get(doc, "roles");
  if (!roles || roles->kind != XUN_LIST || roles->u.list.len != 2) fail("roles");
  expect_str(xun_dict_get(doc, "banner"), "Welcome\nto XUN", "banner");
  xun_free(doc);
}

static void test_empty(void) {
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse("", &doc, &err) != 0 || !doc || doc->kind != XUN_DICT || doc->u.dict.len != 0) fail("empty");
  xun_free(doc);
  if (xun_parse("# only\n", &doc, &err) != 0 || doc->u.dict.len != 0) fail("comment only");
  xun_free(doc);
}

static void test_untyped(void) {
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse("a: 8080\nb: true\nc: 3.10\n", &doc, &err) != 0) {
    fail("untyped parse");
    return;
  }
  expect_str(xun_dict_get(doc, "a"), "8080", "untyped a");
  expect_str(xun_dict_get(doc, "b"), "true", "untyped b");
  expect_str(xun_dict_get(doc, "c"), "3.10", "untyped c");
  xun_free(doc);
}

static void test_encode_roundtrip(const char *root) {
  char path[1024];
  snprintf(path, sizeof path, "%s/testdata/example.xun", root);
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse_file(path, &doc, &err) != 0) {
    fail("parse file for roundtrip");
    return;
  }
  char *encoded = NULL;
  size_t len = 0;
  if (xun_encode(doc, &encoded, &len) != 0) {
    fail("encode failed");
    xun_free(doc);
    return;
  }
  xun_value *parsed = NULL;
  if (xun_parse(encoded, &parsed, &err) != 0) {
    fail("parse encoded text failed");
    free(encoded);
    xun_free(doc);
    return;
  }
  expect_str(xun_dict_get(parsed, "endpoint"), "https://api.example.com/v2/orders", "roundtrip endpoint");
  expect_str(xun_dict_get(parsed, "banner"), "Welcome\nto XUN", "roundtrip banner");
  const xun_value *server = xun_dict_get(parsed, "server");
  expect_str(xun_dict_get(server, "host"), "localhost", "roundtrip host");
  expect_int(xun_dict_get(server, "port"), 8080, "roundtrip port");

  free(encoded);
  xun_free(parsed);
  xun_free(doc);
}

static void test_file_write_and_read(const char *root) {
  char in_path[1024];
  snprintf(in_path, sizeof in_path, "%s/testdata/example.xun", root);
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse_file(in_path, &doc, &err) != 0) {
    fail("parse file for file write/read");
    return;
  }
  const char *out_path = "/tmp/test_c_roundtrip.xun";
  if (xun_encode_file(doc, out_path) != 0) {
    fail("xun_encode_file failed");
    xun_free(doc);
    return;
  }
  xun_value *parsed = NULL;
  if (xun_parse_file(out_path, &parsed, &err) != 0) {
    fail("xun_parse_file from encoded file failed");
    xun_free(doc);
    return;
  }
  expect_str(xun_dict_get(parsed, "endpoint"), "https://api.example.com/v2/orders", "file endpoint");
  expect_str(xun_dict_get(parsed, "banner"), "Welcome\nto XUN", "file banner");
  const xun_value *server = xun_dict_get(parsed, "server");
  expect_str(xun_dict_get(server, "host"), "localhost", "file host");
  expect_int(xun_dict_get(server, "port"), 8080, "file port");

  remove(out_path);
  xun_free(parsed);
  xun_free(doc);
}

static void test_symmetric_and_unpack(void) {
  uint64_t bytes = 0;
  if (xun_parse_size_bytes("10MiB", &bytes) != 0 || bytes != 10485760ULL) {
    fail("xun_parse_size_bytes 10MiB");
  }
  double secs = 0;
  if (xun_parse_duration_seconds("1h30m", &secs) != 0 || secs != 5400.0) {
    fail("xun_parse_duration_seconds 1h30m");
  }
  if (xun_parse_duration_seconds("500ms", &secs) != 0 || secs != 0.5) {
    fail("xun_parse_duration_seconds 500ms");
  }
  if (xun_parse_duration_seconds("15s500ms", &secs) != 0 || secs != 15.5) {
    fail("xun_parse_duration_seconds 15s500ms");
  }
  int parts[4];
  size_t count = 0;
  if (xun_parse_version_parts("3.10.1", parts, 4, &count) != 0 || count != 3 || parts[0] != 3 || parts[1] != 10 || parts[2] != 1) {
    fail("xun_parse_version_parts 3.10.1");
  }

  xun_value *doc = NULL;
  xun_error err;
  if (xun_decode("server:\n  host: 127.0.0.1\n", &doc, &err) != 0) {
    fail("xun_decode failed");
    return;
  }
  const xun_value *server = xun_dict_get(doc, "server");
  expect_str(xun_dict_get(server, "host"), "127.0.0.1", "decode host");
  xun_free(doc);
}

static void test_unicode_and_chinese(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src = "服务名称: 订单处理系统\n端口: !i 8080\n";
  if (xun_decode(src, &doc, &err) != 0) {
    fail("decode chinese");
    return;
  }
  expect_str(xun_dict_get(doc, "服务名称"), "订单处理系统", "chinese value");
  expect_int(xun_dict_get(doc, "端口"), 8080, "chinese int");
  xun_free(doc);
}

static void test_full_core_tags(void) {
  const char *src =
    "str_plain: hello world\n"
    "str_special: !s !not_a_tag\n"
    "num_int: !i 42\n"
    "num_float: !f 3.14159\n"
    "num_hex: !x DEAD_BEEF\n"
    "num_oct: !o 755\n"
    "flag_t: !b true\n"
    "flag_f: !b false\n"
    "date_v: !d 2026-08-14\n"
    "time_v: !t 16:54:00.123\n"
    "dt_v: !dt 2026-08-14T16:54:00+08:00\n"
    "tz_v: !tz Asia/Shanghai\n"
    "dur_v: !du 1d2h30m15s\n"
    "sz_v: !sz 10GiB\n"
    "unix_v: !unix 1700000000\n"
    "ver_v: !ver 3.10.1\n"
    "uuid_v: !uuid 12345678-1234-5678-1234-567812345678\n"
    "ip4_v: !ip 127.0.0.1\n"
    "ip6_v: !ip ::1\n"
    "bytes_v: !xb FF00AA\n"
    "b64_v: !b64 SGVsbG8=\n"
    "char_v: !c A\n"
    "char_cp: !c U+4E2D\n";
  xun_value *doc = NULL;
  xun_error err;
  if (xun_decode(src, &doc, &err) != 0) {
    fail("decode full core tags");
    return;
  }
  expect_str(xun_dict_get(doc, "str_plain"), "hello world", "core str");
  expect_int(xun_dict_get(doc, "num_int"), 42, "core int");
  expect_tagged(xun_dict_get(doc, "num_hex"), "x", "DEAD_BEEF", "core hex");
  expect_tagged(xun_dict_get(doc, "num_oct"), "o", "755", "core oct");
  int64_t n = 0;
  if (xun_to_int(xun_dict_get(doc, "num_hex"), &n) != 0 || n != 0xDEADBEEFLL) fail("core hex to_int");
  if (xun_to_int(xun_dict_get(doc, "num_oct"), &n) != 0 || n != 0755) fail("core oct to_int");
  const xun_value *bt = xun_dict_get(doc, "flag_t");
  if (!bt || bt->kind != XUN_BOOL || !bt->u.b) fail("flag_t");
  expect_tagged(xun_dict_get(doc, "date_v"), "d", "2026-08-14", "date_v");
  expect_tagged(xun_dict_get(doc, "time_v"), "t", "16:54:00.123", "time_v");
  expect_tagged(xun_dict_get(doc, "dt_v"), "dt", "2026-08-14T16:54:00+08:00", "dt_v");
  expect_tagged(xun_dict_get(doc, "tz_v"), "tz", "Asia/Shanghai", "tz_v");
  expect_tagged(xun_dict_get(doc, "dur_v"), "du", "1d2h30m15s", "dur_v");
  expect_tagged(xun_dict_get(doc, "sz_v"), "sz", "10GiB", "sz_v");
  expect_tagged(xun_dict_get(doc, "unix_v"), "unix", "1700000000", "unix_v");
  if (xun_to_int(xun_dict_get(doc, "unix_v"), &n) != 0 || n != 1700000000LL) fail("unix to_int");
  expect_tagged(xun_dict_get(doc, "ver_v"), "ver", "3.10.1", "ver_v");
  expect_tagged(xun_dict_get(doc, "uuid_v"), "uuid", "12345678-1234-5678-1234-567812345678", "uuid_v");
  expect_tagged(xun_dict_get(doc, "ip4_v"), "ip", "127.0.0.1", "ip4_v");
  expect_tagged(xun_dict_get(doc, "ip6_v"), "ip", "::1", "ip6_v");
  const xun_value *bytes = xun_dict_get(doc, "bytes_v");
  if (!bytes || bytes->kind != XUN_BYTES || bytes->u.bytes.len != 3 || bytes->u.bytes.data[0] != 0xFF) fail("bytes_v");
  expect_tagged(xun_dict_get(doc, "char_v"), "c", "A", "char_v");
  expect_tagged(xun_dict_get(doc, "char_cp"), "c", "中", "char_cp");

  /* Encode round-trip of all tags. */
  char *encoded = NULL;
  size_t len = 0;
  if (xun_encode(doc, &encoded, &len) != 0) {
    fail("encode full core tags");
    xun_free(doc);
    return;
  }
  xun_value *parsed = NULL;
  if (xun_parse(encoded, &parsed, &err) != 0) {
    fail("re-parse encoded core tags");
    free(encoded);
    xun_free(doc);
    return;
  }
  expect_tagged(xun_dict_get(parsed, "uuid_v"), "uuid", "12345678-1234-5678-1234-567812345678", "rt uuid");
  expect_tagged(xun_dict_get(parsed, "ip6_v"), "ip", "::1", "rt ip6");
  expect_tagged(xun_dict_get(parsed, "ver_v"), "ver", "3.10.1", "rt ver");
  expect_tagged(xun_dict_get(parsed, "char_cp"), "c", "中", "rt char_cp");
  expect_tagged(xun_dict_get(parsed, "dt_v"), "dt", "2026-08-14T16:54:00+08:00", "rt dt");
  const xun_value *rt_bytes = xun_dict_get(parsed, "bytes_v");
  if (!rt_bytes || rt_bytes->kind != XUN_BYTES || rt_bytes->u.bytes.len != 3 || rt_bytes->u.bytes.data[0] != 0xFF) fail("rt bytes_v");
  free(encoded);
  xun_free(parsed);
  xun_free(doc);
}

static void test_invalid_glyphs_all_tags(void) {
  expect_err("a: !i 1.5\n", "invalid i");
  expect_err("a: !f 8080\n", "invalid f");
  expect_err("a: !x XYZ\n", "invalid x");
  expect_err("a: !xb F0A\n", "invalid xb");
  expect_err("a: !o 89\n", "invalid o");
  expect_err("a: !b yes\n", "invalid b");
  expect_err("a: !d 2026/08/14\n", "invalid d");
  expect_err("a: !t 4pm\n", "invalid t");
  expect_err("a: !dt 2026-08-14T16:54:00\n", "invalid dt");
  expect_err("a: !tz CST\n", "invalid tz");
  expect_err("a: !du 90 minutes\n", "invalid du");
  expect_err("a: !sz 10m\n", "invalid sz");
  expect_err("a: !unix 01692000000\n", "invalid unix");
  expect_err("a: !ver 3.10.beta\n", "invalid ver");
  expect_err("a: !uuid 12345678-1234-5678-1234-5678123456\n", "invalid uuid");
  expect_err("a: !ip 127.0.0.1:80\n", "invalid ip");
  expect_err("a: !b64 not_base64!!\n", "invalid b64");
  expect_err("a: !c ab\n", "invalid c");
}

static void test_uuid_ip_unpackers(void) {
  uint8_t uuid[16];
  const uint8_t want_uuid[16] = {0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78,
                                 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78};
  if (xun_parse_uuid("12345678-1234-5678-1234-567812345678", uuid) != 0 || memcmp(uuid, want_uuid, 16) != 0) {
    fail("xun_parse_uuid valid");
  }
  if (xun_parse_uuid("12345678-1234-5678-1234-56781234567", uuid) == 0) fail("xun_parse_uuid too short");
  if (xun_parse_uuid("12345678-1234-5678-1234-56781234567X", uuid) == 0) fail("xun_parse_uuid bad hex");

  uint8_t ip[16];
  int is_v6 = -1;
  if (xun_parse_ip("127.0.0.1", ip, &is_v6) != 0 || is_v6 != 0 ||
      ip[0] != 127 || ip[1] != 0 || ip[2] != 0 || ip[3] != 1) {
    fail("xun_parse_ip v4");
  }
  if (xun_parse_ip("127.0.0.256", ip, &is_v6) == 0) fail("xun_parse_ip v4 range");
  if (xun_parse_ip("01.2.3.4", ip, &is_v6) == 0) fail("xun_parse_ip v4 leading zero");
  if (xun_parse_ip("::1", ip, &is_v6) != 0 || is_v6 != 1 || ip[15] != 1) {
    fail("xun_parse_ip v6 ::1");
  }
  if (xun_parse_ip("2001:db8::1", ip, &is_v6) != 0 || is_v6 != 1 ||
      ip[0] != 0x20 || ip[1] != 0x01 || ip[2] != 0x0d || ip[3] != 0xb8 || ip[14] != 0 || ip[15] != 1) {
    fail("xun_parse_ip v6 2001:db8::1");
  }
  if (xun_parse_ip("::", ip, &is_v6) != 0 || is_v6 != 1) fail("xun_parse_ip v6 ::");
  if (xun_parse_ip("1::2::3", ip, &is_v6) == 0) fail("xun_parse_ip double ::");
  if (xun_parse_ip("127.0.0.1:80", ip, &is_v6) == 0) fail("xun_parse_ip with port");
}

static void test_extreme_indent_errors(void) {
  expect_err("a:\n   b: 1\n", "3 spaces");
  expect_err("a:\n\tb: 1\n", "tab");
  expect_err("a:\n    b: 1\n", "indent jump");
  expect_err("server:\n  host: 1\n  - item1\n", "mix dict/list");
}

static void test_encode_keeps_literal_quotes(void) {
  /* Value is the 7-char string "hello" (with quote chars). */
  const char *src = "a: \"\\\"hello\\\"\"\n";
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse(src, &doc, &err) != 0) {
    fail("literal quotes parse");
    return;
  }
  expect_str(xun_dict_get(doc, "a"), "\"hello\"", "literal quote value");
  char *encoded = NULL;
  size_t len = 0;
  if (xun_encode(doc, &encoded, &len) != 0) {
    fail("literal quotes encode");
    xun_free(doc);
    return;
  }
  if (strcmp(encoded, src) != 0) {
    fprintf(stderr, "literal quotes got: %s\nwant: %s\n", encoded, src);
    fail("literal quotes roundtrip");
  }
  free(encoded);
  xun_free(doc);

  /* Two quote chars round-trip as well. */
  const char *src2 = "a: \"\\\"\\\"\"\n";
  if (xun_parse(src2, &doc, &err) != 0) {
    fail("empty quotes parse");
    return;
  }
  expect_str(xun_dict_get(doc, "a"), "\"\"", "empty quotes value");
  if (xun_encode(doc, &encoded, &len) != 0) {
    fail("empty quotes encode");
    xun_free(doc);
    return;
  }
  if (strcmp(encoded, src2) != 0) {
    fprintf(stderr, "empty quotes got: %s\nwant: %s\n", encoded, src2);
    fail("empty quotes roundtrip");
  }
  free(encoded);
  xun_free(doc);
}

static void expect_encode(const char *src, const char *want, const char *label) {
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse(src, &doc, &err) != 0) {
    fail(label);
    return;
  }
  char *encoded = NULL;
  size_t len = 0;
  if (xun_encode(doc, &encoded, &len) != 0) {
    fail(label);
    xun_free(doc);
    return;
  }
  if (strcmp(encoded, want) != 0) {
    fprintf(stderr, "%s got: %s\nwant: %s\n", label, encoded, want);
    fail(label);
  }
  free(encoded);
  xun_free(doc);
}

static void test_encode_numeric_looking_strings(void) {
  expect_encode("a: 123\n", "a: !s 123\n", "str 123");
  expect_encode("a: \"123 \"\n", "a: !s \"123 \"\n", "quoted trailing space");
  {
    xun_value *doc = NULL;
    xun_error err;
    if (xun_parse("a: \"\"\n", &doc, &err) != 0) fail("quoted empty parse");
    else {
      expect_str(xun_dict_get(doc, "a"), "", "quoted empty value");
      xun_free(doc);
    }
  }
  expect_encode("a: 3.10\n", "a: !s 3.10\n", "str 3.10");
  expect_encode("a: -1.5\n", "a: !s -1.5\n", "str -1.5");
  expect_encode("a: 1e-3\n", "a: !s 1e-3\n", "str 1e-3");
  expect_encode("a: 0xFF\n", "a: !s 0xFF\n", "str 0xFF");
  expect_encode("a: 0b10\n", "a: !s 0b10\n", "str 0b10");
  expect_encode("a: 0o755\n", "a: !s 0o755\n", "str 0o755");
  expect_encode("a: Infinity\n", "a: !s Infinity\n", "str Infinity");
  expect_encode("items:\n  - 80\n  - 443\n", "items:\n  - !s 80\n  - !s 443\n", "list numeric strings");
  expect_encode("a: !i 123\n", "a: !i 123\n", "int 123");
  expect_encode("a: hello\n", "a: hello\n", "plain hello");
  expect_encode("a: 123abc\n", "a: 123abc\n", "123abc");
  expect_encode("a: 1.2.3\n", "a: 1.2.3\n", "version-like");
}

static char *nest_source(int levels) {
  size_t cap = (size_t)levels * (size_t)levels + (size_t)levels * 64 + 128;
  char *s = malloc(cap);
  size_t used = 0;
  for (int i = 0; i < levels; i++) {
    for (int sp = 0; sp < i * 2; sp++) s[used++] = ' ';
    used += (size_t)snprintf(s + used, cap - used, "k%d:\n", i);
  }
  for (int sp = 0; sp < levels * 2; sp++) s[used++] = ' ';
  used += (size_t)snprintf(s + used, cap - used, "v: leaf\n");
  s[used] = 0;
  return s;
}

static void expect_string_roundtrip(const char *s, const char *label) {
  char src[128];
  snprintf(src, sizeof src, "a: %s\n", s);
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse(src, &doc, &err) != 0) {
    fail(label);
    return;
  }
  const xun_value *val = xun_dict_get(doc, "a");
  if (!val || val->kind != XUN_STRING || strcmp(val->u.str, s) != 0) {
    fail(label);
    xun_free(doc);
    return;
  }
  char *encoded = NULL;
  size_t len = 0;
  if (xun_encode(doc, &encoded, &len) != 0) {
    fail(label);
    xun_free(doc);
    return;
  }
  xun_value *reparsed = NULL;
  if (xun_parse(encoded, &reparsed, &err) != 0) {
    fail(label);
    free(encoded);
    xun_free(doc);
    return;
  }
  expect_str(xun_dict_get(reparsed, "a"), s, label);
  free(encoded);
  xun_free(doc);
  xun_free(reparsed);
}

static void test_extreme(void) {
  const char *must_tag[] = {
    "0", "00", "012", "08", "8080", "+123", "-123", "-0", "+0",
    "3.10", "3.", ".5", ".0", "0.", "0.0", "00.1", "-.5", "+.5", "+0.0", "-0.0",
    "1e3", "1E-3", "1e+10", "0e0", "1E+0", "1e-0", "+1.5e-10", "5.e2", "+.5e2", "-.5E-1",
    "0xFF", "0Xff", "0x0", "0xabcdef", "0XABCDEF",
    "0b10", "0B10", "0b0", "0b01",
    "0o755", "0O7", "0o0", "0o07",
    "Infinity", "+Infinity", "-Infinity",
    "9007199254740991", "9007199254740993",
    "-0x10", "+0x10", "-0b1", "+0b10", "-0o10",
    " 123",
    NULL
  };
  for (int i = 0; must_tag[i]; i++) {
    expect_string_roundtrip(must_tag[i], must_tag[i]);
  }
  const char *no_tag[] = {
    "hello", "123abc", "1.2.3", "1e", "0x", "0b2", "0o8",
    "infinity", "NaN", "true", "1_000", "127.0.0.1", "::1",
    NULL
  };
  for (int i = 0; no_tag[i]; i++) {
    char src[128];
    snprintf(src, sizeof src, "a: %s\n", no_tag[i]);
    expect_encode(src, src, no_tag[i]);
  }
  expect_encode("a: \"8080\"\n", "a: !s 8080\n", "quoted 8080");
  expect_encode("a: !s !x\n", "a: !s !x\n", "special !x");
  expect_encode("a: !i 0\n", "a: !i 0\n", "int 0");
  expect_encode("a: !s 3.10\n", "a: !s 3.10\n", "explicit !s");
  expect_encode("a: 123\r\nb: x\r\n", "a: !s 123\nb: x\n", "CRLF");

  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse("\xef\xbb\xbf" "a: hello\n", &doc, &err) != 0) fail("BOM");
  else {
    expect_str(xun_dict_get(doc, "a"), "hello", "BOM value");
    xun_free(doc);
  }

  char *huge = malloc(1024 * 1024 + 2);
  memset(huge, 'x', 1024 * 1024 + 1);
  huge[1024 * 1024 + 1] = 0;
  if (xun_parse(huge, &doc, &err) == 0) {
    fail("oversize");
    xun_free(doc);
  }
  free(huge);

  char *d64 = nest_source(64);
  if (xun_parse(d64, &doc, &err) != 0) fail("decode depth 64");
  else {
    char *encoded = NULL;
    size_t len = 0;
    if (xun_encode(doc, &encoded, &len) != 0) fail("encode depth 64");
    else free(encoded);
    xun_free(doc);
  }
  free(d64);

  char *d65 = nest_source(65);
  if (xun_parse(d65, &doc, &err) == 0) {
    fail("decode depth 65");
    xun_free(doc);
  }
  free(d65);
}

/* ============================================================
 * RFC-0001: Optional 'end' block delimiter
 * ============================================================ */

static void test_rfc0001_bare_end_closes_top_level_dict(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "server:\n"
      "  host: localhost\n"
      "  port: !n 8080\n"
      "end\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 bare end top dict"); return; }
  const xun_value *server = xun_dict_get(doc, "server");
  expect_str(xun_dict_get(server, "host"), "localhost", "rfc0001 host");
  expect_int(xun_dict_get(server, "port"), 8080, "rfc0001 port");
  xun_free(doc);
}

static void test_rfc0001_bare_end_closes_nested_dict(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "server:\n"
      "  host: localhost\n"
      "  tls:\n"
      "    cert: /etc/ssl/cert.pem\n"
      "    mode: !o 755\n"
      "  end\n"
      "  port: !n 8080\n"
      "end\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 bare end nested dict"); return; }
  const xun_value *server = xun_dict_get(doc, "server");
  expect_str(xun_dict_get(server, "host"), "localhost", "rfc0001 nested host");
  expect_str(xun_dict_get(xun_dict_get(server, "tls"), "cert"), "/etc/ssl/cert.pem", "rfc0001 tls cert");
  expect_int(xun_dict_get(server, "port"), 8080, "rfc0001 nested port");
  xun_free(doc);
}

static void test_rfc0001_end_with_key_closes_named_nested_dict(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "server:\n"
      "  host: localhost\n"
      "  tls:\n"
      "    cert: /etc/ssl/cert.pem\n"
      "  end tls\n"
      "  port: !n 8080\n"
      "end server\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 end key nested dict"); return; }
  const xun_value *server = xun_dict_get(doc, "server");
  expect_str(xun_dict_get(server, "host"), "localhost", "rfc0001 named host");
  expect_str(xun_dict_get(xun_dict_get(server, "tls"), "cert"), "/etc/ssl/cert.pem", "rfc0001 named cert");
  expect_int(xun_dict_get(server, "port"), 8080, "rfc0001 named port");
  xun_free(doc);
}

static void test_rfc0001_bare_end_closes_list_block(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "servers:\n"
      "  - host: a\n"
      "    port: 80\n"
      "  - host: b\n"
      "    port: 81\n"
      "end\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 bare end list"); return; }
  const xun_value *servers = xun_dict_get(doc, "servers");
  if (!servers || servers->kind != XUN_LIST || servers->u.list.len != 2) fail("rfc0001 list len");
  else {
    expect_str(xun_dict_get(servers->u.list.items[0], "host"), "a", "rfc0001 list[0] host");
    expect_str(xun_dict_get(servers->u.list.items[1], "host"), "b", "rfc0001 list[1] host");
  }
  xun_free(doc);
}

static void test_rfc0001_bare_end_equivalent_to_dedent(void) {
  xun_value *with_end = NULL, *without_end = NULL;
  xun_error err;
  const char *src_with =
      "a: 1\n"
      "b: 2\n"
      "end\n";
  const char *src_without =
      "a: 1\n"
      "b: 2\n";
  if (xun_parse(src_with, &with_end, &err) != 0) { fail("rfc0001 dedent with"); return; }
  if (xun_parse(src_without, &without_end, &err) != 0) { fail("rfc0001 dedent without"); xun_free(with_end); return; }
  expect_str(xun_dict_get(with_end, "a"), "1", "rfc0001 dedent a");
  expect_str(xun_dict_get(with_end, "b"), "2", "rfc0001 dedent b");
  expect_str(xun_dict_get(without_end, "a"), "1", "rfc0001 dedent wo a");
  expect_str(xun_dict_get(without_end, "b"), "2", "rfc0001 dedent wo b");
  xun_free(with_end);
  xun_free(without_end);
}

static void test_rfc0001_end_key_mismatch_throws(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "server:\n"
      "  host: localhost\n"
      "  port: 8080\n"
      "end tls\n";
  if (xun_parse(src, &doc, &err) == 0) {
    fail("rfc0001 end-key mismatch should fail");
    xun_free(doc);
  }
}

static void test_rfc0001_bare_end_on_root_allowed(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "a: 1\n"
      "end\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 bare end root"); return; }
  expect_str(xun_dict_get(doc, "a"), "1", "rfc0001 root end a");
  xun_free(doc);
}

static void test_rfc0001_end_as_dict_key_allowed(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "end: 1\n"
      "end2: 2\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 end as key"); return; }
  expect_str(xun_dict_get(doc, "end"), "1", "rfc0001 end key");
  expect_str(xun_dict_get(doc, "end2"), "2", "rfc0001 end2 key");
  xun_free(doc);
}

static void test_rfc0001_end_inside_multiline_block_is_literal(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "script: |\n"
      "  echo \"end of script\"\n"
      "|\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 multiline end"); return; }
  expect_str(xun_dict_get(doc, "script"), "echo \"end of script\"", "rfc0001 multiline script");
  xun_free(doc);
}

static void test_rfc0001_deeply_nested_end_chains(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "a:\n"
      "  b:\n"
      "    c:\n"
      "      d: !n 1\n"
      "    end c\n"
      "  end b\n"
      "end a\n"
      "e: !n 2\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 deep nested"); return; }
  const xun_value *a = xun_dict_get(doc, "a");
  const xun_value *b = xun_dict_get(a, "b");
  const xun_value *c = xun_dict_get(b, "c");
  expect_int(xun_dict_get(c, "d"), 1, "rfc0001 deep d");
  expect_int(xun_dict_get(doc, "e"), 2, "rfc0001 deep e");
  xun_free(doc);
}

static void test_rfc0001_end_allows_sibling_content_after(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "server:\n"
      "  host: a\n"
      "end server\n"
      "proxy:\n"
      "  host: b\n"
      "end proxy\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 siblings"); return; }
  expect_str(xun_dict_get(xun_dict_get(doc, "server"), "host"), "a", "rfc0001 sib server host");
  expect_str(xun_dict_get(xun_dict_get(doc, "proxy"), "host"), "b", "rfc0001 sib proxy host");
  xun_free(doc);
}

static void test_rfc0001_root_end_with_complex_dict(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "app:\n"
      "  name: myapp\n"
      "  ports: !n[80, 443]\n"
      "  version: !ver 1.2.3\n"
      "end app\n"
      "trailer: hi\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0001 root complex"); return; }
  const xun_value *app = xun_dict_get(doc, "app");
  expect_str(xun_dict_get(app, "name"), "myapp", "rfc0001 root name");
  const xun_value *ports = xun_dict_get(app, "ports");
  if (!ports || ports->kind != XUN_LIST || ports->u.list.len != 2) fail("rfc0001 root ports len");
  else {
    expect_int(ports->u.list.items[0], 80, "rfc0001 root port 0");
    expect_int(ports->u.list.items[1], 443, "rfc0001 root port 1");
  }
  expect_tagged(xun_dict_get(app, "version"), "ver", "1.2.3", "rfc0001 root ver");
  expect_str(xun_dict_get(doc, "trailer"), "hi", "rfc0001 root trailer");
  xun_free(doc);
}

/* ============================================================
 * RFC-0002: Inline object / array literals
 * ============================================================ */

static void test_rfc0002_inline_object_as_list_item(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "servers:\n"
      "  - {host: a, port: 80}\n"
      "  - {host: b, port: 81}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 inline obj list item"); return; }
  const xun_value *servers = xun_dict_get(doc, "servers");
  if (!servers || servers->kind != XUN_LIST || servers->u.list.len != 2) fail("rfc0002 inline obj len");
  else {
    expect_str(xun_dict_get(servers->u.list.items[0], "host"), "a", "rfc0002 inline obj[0] host");
    expect_str(xun_dict_get(servers->u.list.items[0], "port"), "80", "rfc0002 inline obj[0] port");
    expect_str(xun_dict_get(servers->u.list.items[1], "host"), "b", "rfc0002 inline obj[1] host");
    expect_str(xun_dict_get(servers->u.list.items[1], "port"), "81", "rfc0002 inline obj[1] port");
  }
  xun_free(doc);
}

static void test_rfc0002_inline_object_with_quoted_value(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "servers:\n"
      "  - {host: \"my host\", port: 80}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 inline quoted"); return; }
  const xun_value *servers = xun_dict_get(doc, "servers");
  const xun_value *first = servers->u.list.items[0];
  expect_str(xun_dict_get(first, "host"), "my host", "rfc0002 quoted host");
  xun_free(doc);
}

static void test_rfc0002_inline_object_with_tagged_values(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "cfg:\n"
      "  - {port: !n 8080, mode: !o 755, color: !xb FF00AA}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 inline tagged"); return; }
  const xun_value *cfg = xun_dict_get(doc, "cfg");
  const xun_value *first = cfg->u.list.items[0];
  expect_int(xun_dict_get(first, "port"), 8080, "rfc0002 tagged port");
  expect_tagged(xun_dict_get(first, "mode"), "o", "755", "rfc0002 tagged mode");
  const xun_value *color = xun_dict_get(first, "color");
  if (!color || color->kind != XUN_BYTES || color->u.bytes.len != 3 ||
      color->u.bytes.data[0] != 0xff || color->u.bytes.data[1] != 0x00 || color->u.bytes.data[2] != 0xaa) {
    fail("rfc0002 tagged color");
  }
  xun_free(doc);
}

static void test_rfc0002_inline_object_with_empty_value(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "cfg:\n"
      "  - {name: \"\"}\n"
      "  - {empty:}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 inline empty val"); return; }
  const xun_value *cfg = xun_dict_get(doc, "cfg");
  expect_str(xun_dict_get(cfg->u.list.items[0], "name"), "", "rfc0002 empty[0]");
  expect_str(xun_dict_get(cfg->u.list.items[1], "empty"), "", "rfc0002 empty[1]");
  xun_free(doc);
}

static void test_rfc0002_compact_array_of_inline_objects_no_tag(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "peers: [{host: a, port: 80}, {host: b, port: 81}]\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 compact obj no tag"); return; }
  const xun_value *peers = xun_dict_get(doc, "peers");
  if (!peers || peers->kind != XUN_LIST || peers->u.list.len != 2) fail("rfc0002 compact len");
  else {
    expect_str(xun_dict_get(peers->u.list.items[0], "host"), "a", "rfc0002 compact[0] host");
    expect_str(xun_dict_get(peers->u.list.items[1], "host"), "b", "rfc0002 compact[1] host");
  }
  xun_free(doc);
}

static void test_rfc0002_compact_tagged_array_of_inline_objects(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "items: !o[{a: 1, b: 2}, {c: 3}]\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 compact tagged obj"); return; }
  const xun_value *items = xun_dict_get(doc, "items");
  if (!items || items->kind != XUN_LIST || items->u.list.len != 2) fail("rfc0002 compact tagged len");
  else {
    expect_str(xun_dict_get(items->u.list.items[0], "a"), "1", "rfc0002 tagged obj[0].a");
    expect_str(xun_dict_get(items->u.list.items[0], "b"), "2", "rfc0002 tagged obj[0].b");
    expect_str(xun_dict_get(items->u.list.items[1], "c"), "3", "rfc0002 tagged obj[1].c");
  }
  xun_free(doc);
}

static void test_rfc0002_mix_block_and_inline_list_items(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "servers:\n"
      "  - {host: a, port: 80}\n"
      "  - host: b\n"
      "    port: 81\n"
      "    tls: enabled\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 mix list"); return; }
  const xun_value *servers = xun_dict_get(doc, "servers");
  if (!servers || servers->kind != XUN_LIST || servers->u.list.len != 2) fail("rfc0002 mix len");
  else {
    expect_str(xun_dict_get(servers->u.list.items[0], "host"), "a", "rfc0002 mix[0] host");
    expect_str(xun_dict_get(servers->u.list.items[1], "host"), "b", "rfc0002 mix[1] host");
    expect_str(xun_dict_get(servers->u.list.items[1], "tls"), "enabled", "rfc0002 mix[1] tls");
  }
  xun_free(doc);
}

static void test_rfc0002_empty_inline_object(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "cfg:\n"
      "  - {}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 empty inline obj"); return; }
  const xun_value *cfg = xun_dict_get(doc, "cfg");
  const xun_value *first = cfg->u.list.items[0];
  if (!first || first->kind != XUN_DICT || first->u.dict.len != 0) fail("rfc0002 empty obj dict");
  xun_free(doc);
}

static void test_rfc0002_inline_object_in_nested_block(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "data:\n"
      "  items:\n"
      "    - {id: 1, name: alice}\n"
      "    - {id: 2, name: bob}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 nested inline"); return; }
  const xun_value *data = xun_dict_get(doc, "data");
  const xun_value *items = xun_dict_get(data, "items");
  if (!items || items->kind != XUN_LIST || items->u.list.len != 2) fail("rfc0002 nested len");
  else {
    expect_str(xun_dict_get(items->u.list.items[0], "id"), "1", "rfc0002 nested[0] id");
    expect_str(xun_dict_get(items->u.list.items[0], "name"), "alice", "rfc0002 nested[0] name");
    expect_str(xun_dict_get(items->u.list.items[1], "id"), "2", "rfc0002 nested[1] id");
    expect_str(xun_dict_get(items->u.list.items[1], "name"), "bob", "rfc0002 nested[1] name");
  }
  xun_free(doc);
}

static void test_rfc0002_duplicate_keys_in_inline_object_throws(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "cfg:\n"
      "  - {a: 1, a: 2}\n";
  if (xun_parse(src, &doc, &err) == 0) {
    fail("rfc0002 dup keys should fail");
    xun_free(doc);
  }
}

static void test_rfc0002_malformed_inline_object_throws(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "cfg:\n"
      "  - {a, b: 2}\n";
  if (xun_parse(src, &doc, &err) == 0) {
    fail("rfc0002 malformed inline should fail");
    xun_free(doc);
  }
}

static void test_rfc0002_inline_object_preserves_key_order(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "cfg:\n"
      "  - {z: 1, a: 2, k: 3}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 key order"); return; }
  const xun_value *cfg = xun_dict_get(doc, "cfg");
  const xun_value *first = cfg->u.list.items[0];
  if (!first || first->kind != XUN_DICT || first->u.dict.len != 3) fail("rfc0002 key order len");
  else {
    if (strcmp(first->u.dict.items[0].key, "z") != 0) fail("rfc0002 order[0]");
    if (strcmp(first->u.dict.items[1].key, "a") != 0) fail("rfc0002 order[1]");
    if (strcmp(first->u.dict.items[2].key, "k") != 0) fail("rfc0002 order[2]");
  }
  xun_free(doc);
}

static void test_rfc0002_nested_compact_array_in_inline_object(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "cfg:\n"
      "  - {tags: !s[a, b, c], port: !n 80}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 nested compact"); return; }
  const xun_value *cfg = xun_dict_get(doc, "cfg");
  const xun_value *first = cfg->u.list.items[0];
  const xun_value *tags = xun_dict_get(first, "tags");
  if (!tags || tags->kind != XUN_LIST || tags->u.list.len != 3) fail("rfc0002 tags len");
  else {
    expect_str(tags->u.list.items[0], "a", "rfc0002 tags[0]");
    expect_str(tags->u.list.items[1], "b", "rfc0002 tags[1]");
    expect_str(tags->u.list.items[2], "c", "rfc0002 tags[2]");
  }
  expect_int(xun_dict_get(first, "port"), 80, "rfc0002 nested compact port");
  xun_free(doc);
}

static void test_rfc0002_traditional_block_list_regression(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "items:\n"
      "  - one\n"
      "  - two\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 block list reg"); return; }
  const xun_value *items = xun_dict_get(doc, "items");
  if (!items || items->kind != XUN_LIST || items->u.list.len != 2) fail("rfc0002 block list len");
  else {
    expect_str(items->u.list.items[0], "one", "rfc0002 block list[0]");
    expect_str(items->u.list.items[1], "two", "rfc0002 block list[1]");
  }
  xun_free(doc);
}

static void test_rfc0002_traditional_compact_array_regression(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src = "ports: !n[80, 443, 8080]\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 compact reg"); return; }
  const xun_value *ports = xun_dict_get(doc, "ports");
  if (!ports || ports->kind != XUN_LIST || ports->u.list.len != 3) fail("rfc0002 compact len");
  else {
    expect_int(ports->u.list.items[0], 80, "rfc0002 compact reg[0]");
    expect_int(ports->u.list.items[1], 443, "rfc0002 compact reg[1]");
    expect_int(ports->u.list.items[2], 8080, "rfc0002 compact reg[2]");
  }
  xun_free(doc);
}

static void test_rfc0002_end_as_inline_object_value(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "cfg:\n"
      "  - {end: foo, value: 1}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 end key in inline"); return; }
  const xun_value *cfg = xun_dict_get(doc, "cfg");
  const xun_value *first = cfg->u.list.items[0];
  expect_str(xun_dict_get(first, "end"), "foo", "rfc0002 end as key");
  expect_str(xun_dict_get(first, "value"), "1", "rfc0002 end as key value");
  xun_free(doc);
}

static void test_rfc0002_inline_object_with_complex_tagged_values(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src =
      "cfg:\n"
      "  - {host: a, port: !n 8080, tag: !ver 1.2, code: !xb CAFE}\n";
  if (xun_parse(src, &doc, &err) != 0) { fail("rfc0002 complex tagged"); return; }
  const xun_value *cfg = xun_dict_get(doc, "cfg");
  const xun_value *first = cfg->u.list.items[0];
  expect_str(xun_dict_get(first, "host"), "a", "rfc0002 complex host");
  expect_int(xun_dict_get(first, "port"), 8080, "rfc0002 complex port");
  expect_tagged(xun_dict_get(first, "tag"), "ver", "1.2", "rfc0002 complex ver");
  const xun_value *code = xun_dict_get(first, "code");
  if (!code || code->kind != XUN_BYTES || code->u.bytes.len != 2 ||
      code->u.bytes.data[0] != 0xCA || code->u.bytes.data[1] != 0xFE) {
    fail("rfc0002 complex code");
  }
  xun_free(doc);
}

int main(int argc, char **argv) {
  const char *root = argc > 1 ? argv[1] : "..";
  test_example(root);
  test_empty();
  test_untyped();
  test_encode_roundtrip(root);
  test_file_write_and_read(root);
  test_symmetric_and_unpack();
  test_unicode_and_chinese();
  test_full_core_tags();
  test_invalid_glyphs_all_tags();
  test_uuid_ip_unpackers();
  test_encode_keeps_literal_quotes();
  test_encode_numeric_looking_strings();
  test_extreme();
  test_extreme_indent_errors();
  /* RFC-0001 */
  test_rfc0001_bare_end_closes_top_level_dict();
  test_rfc0001_bare_end_closes_nested_dict();
  test_rfc0001_end_with_key_closes_named_nested_dict();
  test_rfc0001_bare_end_closes_list_block();
  test_rfc0001_bare_end_equivalent_to_dedent();
  test_rfc0001_end_key_mismatch_throws();
  test_rfc0001_bare_end_on_root_allowed();
  test_rfc0001_end_as_dict_key_allowed();
  test_rfc0001_end_inside_multiline_block_is_literal();
  test_rfc0001_deeply_nested_end_chains();
  test_rfc0001_end_allows_sibling_content_after();
  test_rfc0001_root_end_with_complex_dict();
  /* RFC-0002 */
  test_rfc0002_inline_object_as_list_item();
  test_rfc0002_inline_object_with_quoted_value();
  test_rfc0002_inline_object_with_tagged_values();
  test_rfc0002_inline_object_with_empty_value();
  test_rfc0002_compact_array_of_inline_objects_no_tag();
  test_rfc0002_compact_tagged_array_of_inline_objects();
  test_rfc0002_mix_block_and_inline_list_items();
  test_rfc0002_empty_inline_object();
  test_rfc0002_inline_object_in_nested_block();
  test_rfc0002_duplicate_keys_in_inline_object_throws();
  test_rfc0002_malformed_inline_object_throws();
  test_rfc0002_inline_object_preserves_key_order();
  test_rfc0002_nested_compact_array_in_inline_object();
  test_rfc0002_traditional_block_list_regression();
  test_rfc0002_traditional_compact_array_regression();
  test_rfc0002_end_as_inline_object_value();
  test_rfc0002_inline_object_with_complex_tagged_values();
  expect_err("a: 1\na: 2\n", "duplicate");
  expect_err("x: !f 8080\n", "float");
  expect_err("a: |\n  hi\n", "multiline");
  expect_err("key:value\n", "no space");
  expect_err("- a\n- b\n", "root list");
  expect_err("v: !s[a, b]\n", "string compact");
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse("a: !s !important\n", &doc, &err) != 0) fail("literal s");
  else {
    expect_str(xun_dict_get(doc, "a"), "!important", "literal s");
    xun_free(doc);
  }
  if (failed) {
    fprintf(stderr, "%d failed\n", failed);
    return 1;
  }
  puts("ok");
  return 0;
}
