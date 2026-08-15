import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { parse, Tagged, XunError } from "../src/xun.js";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");

test("README example", () => {
  const src = readFileSync(join(root, "testdata/example.xun"), "utf8");
  const doc = parse(src);
  assert.equal(doc.server.host, "localhost");
  assert.equal(doc.server.port, 8080);
  assert.deepEqual(doc.server.bind, new Tagged("ip", "::1"));
  assert.equal(doc.server.tls.cert, "/etc/ssl/cert.pem");
  assert.equal(doc.server.tls.mode, 0o755);
  assert.deepEqual(doc.features, ["auth", "cache"]);
  assert.deepEqual(doc.ports, [80, 443, 8080]);
  assert.equal(doc.endpoint, "https://api.example.com/v2/orders");
  assert.deepEqual(doc.tz, new Tagged("tz", "Asia/Shanghai"));
  assert.deepEqual(doc.py, new Tagged("ver", "3.10"));
  assert.deepEqual(doc.limit, new Tagged("sz", "10MiB"));
  assert.deepEqual(doc.when, new Tagged("dt", "2026-08-14T16:54:00+08:00"));
  assert.deepEqual(doc.color, Uint8Array.from([0xff, 0x00, 0xaa]));
  assert.deepEqual(doc.roles, ["admin", "ops"]);
  assert.equal(doc.banner, "Welcome\nto XUN");
});

test("empty file is empty dict", () => {
  assert.deepEqual(parse(""), {});
  assert.deepEqual(parse("# only comment\n"), {});
});

test("untyped values stay strings", () => {
  const doc = parse("a: 8080\nb: true\nc: 3.10\n");
  assert.equal(doc.a, "8080");
  assert.equal(doc.b, "true");
  assert.equal(doc.c, "3.10");
});

test("duplicate keys fail", () => {
  assert.throws(() => parse("a: 1\na: 2\n"), XunError);
});

test("3.10 as version is not a float", () => {
  const doc = parse("py: !ver 3.10\n");
  assert.deepEqual(doc.py, new Tagged("ver", "3.10"));
});

test("implicit float tag rejected without dot or e", () => {
  assert.throws(() => parse("x: !f 8080\n"), XunError);
});

test("multiline closer required", () => {
  assert.throws(() => parse("a: |\n  hi\n"), XunError);
});

test("key:value without space is illegal", () => {
  assert.throws(() => parse("key:value\n"), XunError);
});

test("root list is illegal", () => {
  assert.throws(() => parse("- a\n- b\n"), XunError);
});

test("string with special char and !s", () => {
  const doc = parse("a: !s !important\n");
  assert.equal(doc.a, "!important");
});

test("typed compact character array", () => {
  const doc = parse("v: !c[a, e, i]\n");
  assert.deepEqual(doc.v, [
    new Tagged("c", "a"),
    new Tagged("c", "e"),
    new Tagged("c", "i"),
  ]);
});

test("string compact array is illegal", () => {
  assert.throws(() => parse("v: !s[a, b]\n"), XunError);
});
