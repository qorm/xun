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
  expect_int(xun_dict_get(tls, "mode"), 0755, "mode");
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
  uint64_t ms = 0;
  if (xun_parse_duration_ms("1h30m", &ms) != 0 || ms != 5400000ULL) {
    fail("xun_parse_duration_ms 1h30m");
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
  expect_int(xun_dict_get(doc, "num_oct"), 0755, "core oct");
  const xun_value *bt = xun_dict_get(doc, "flag_t");
  if (!bt || bt->kind != XUN_BOOL || !bt->u.b) fail("flag_t");
  expect_tagged(xun_dict_get(doc, "date_v"), "d", "2026-08-14", "date_v");
  expect_tagged(xun_dict_get(doc, "time_v"), "t", "16:54:00.123", "time_v");
  expect_tagged(xun_dict_get(doc, "dt_v"), "dt", "2026-08-14T16:54:00+08:00", "dt_v");
  expect_tagged(xun_dict_get(doc, "tz_v"), "tz", "Asia/Shanghai", "tz_v");
  expect_tagged(xun_dict_get(doc, "dur_v"), "du", "1d2h30m15s", "dur_v");
  expect_tagged(xun_dict_get(doc, "sz_v"), "sz", "10GiB", "sz_v");
  expect_int(xun_dict_get(doc, "unix_v"), 1700000000, "unix_v");
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

static void test_encode_strips_surrounding_quotes(void) {
  xun_value *doc = NULL;
  xun_error err;
  const char *src = "a: \"hello\"\nb: \"\"\nc: \"x\"\nitems:\n  - \"p\"\n  - \"q\"\n";
  if (xun_parse(src, &doc, &err) != 0) {
    fail("quote strip parse");
    return;
  }
  char *encoded = NULL;
  size_t len = 0;
  if (xun_encode(doc, &encoded, &len) != 0) {
    fail("quote strip encode");
    xun_free(doc);
    return;
  }
  const char *want = "a: hello\nb:\nc: x\nitems:\n  - p\n  - q\n";
  if (strcmp(encoded, want) != 0) {
    fprintf(stderr, "quote strip got: %s\nwant: %s\n", encoded, want);
    fail("quote strip mismatch");
  }
  free(encoded);
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
  test_encode_strips_surrounding_quotes();
  test_extreme_indent_errors();
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
