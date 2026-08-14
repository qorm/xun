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

int main(int argc, char **argv) {
  const char *root = argc > 1 ? argv[1] : "..";
  test_example(root);
  test_empty();
  test_untyped();
  expect_err("a: 1\na: 2\n", "duplicate");
  expect_err("x: !f 8080\n", "float");
  expect_err("a: |\n  hi\n", "multiline");
  expect_err("key:value\n", "no space");
  expect_err("- a\n- b\n", "root list");
  expect_err("v: !s[a, b]\n", "string compact");
  xun_value *doc = NULL;
  xun_error err;
  if (xun_parse("$api: https://x.test\na: !s $api\n", &doc, &err) != 0) fail("literal s");
  else {
    expect_str(xun_dict_get(doc, "a"), "$api", "literal $");
    xun_free(doc);
  }
  if (failed) {
    fprintf(stderr, "%d failed\n", failed);
    return 1;
  }
  puts("ok");
  return 0;
}
