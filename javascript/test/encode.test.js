import { test } from "node:test";
import assert from "node:assert/strict";
import { writeFileSync, readFileSync, unlinkSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { encode, stringify, parse, Tagged, XunError } from "../src/xun.js";

test("encode empty dict", () => {
  assert.equal(encode({}), "");
  assert.equal(stringify({}), "");
});

test("encode non-dict root throws error", () => {
  assert.throws(() => encode(["item1"]), XunError);
  assert.throws(() => encode("str"), XunError);
  assert.throws(() => encode(123), XunError);
});

test("encode basic types and round-trip", () => {
  const data = {
    str_plain: "hello world",
    str_with_tag: "!not_a_tag",
    str_empty: "",
    number_int: 42,
    number_float: 3.14,
    flag_true: true,
    flag_false: false,
    bytes_val: new Uint8Array([0xde, 0xad, 0xbe, 0xef]),
    tagged_val: new Tagged("ver", "3.10"),
  };

  const text = encode(data);
  const parsed = parse(text);

  assert.equal(parsed.str_plain, "hello world");
  assert.equal(parsed.str_with_tag, "!not_a_tag");
  assert.equal(parsed.str_empty, "");
  assert.equal(parsed.number_int, 42);
  assert.equal(parsed.number_float, 3.14);
  assert.equal(parsed.flag_true, true);
  assert.equal(parsed.flag_false, false);
  assert.deepEqual(parsed.bytes_val, new Uint8Array([0xde, 0xad, 0xbe, 0xef]));
  assert.deepEqual(parsed.tagged_val, new Tagged("ver", "3.10"));
});

test("encode nested objects and arrays", () => {
  const data = {
    server: {
      host: "localhost",
      port: 8080,
      tls: {
        cert: "/etc/ssl/cert.pem",
      },
    },
    empty_obj: {},
    empty_arr: [],
    features: ["auth", "cache", { role: "admin" }],
    banner: "Line1\nLine2\nLine3",
  };

  const text = encode(data);
  const parsed = parse(text);

  assert.equal(parsed.server.host, "localhost");
  assert.equal(parsed.server.port, 8080);
  assert.equal(parsed.server.tls.cert, "/etc/ssl/cert.pem");
  assert.deepEqual(parsed.empty_obj, {});
  assert.deepEqual(parsed.empty_arr, []);
  assert.equal(parsed.features[0], "auth");
  assert.equal(parsed.features[1], "cache");
  assert.deepEqual(parsed.features[2], { role: "admin" });
  assert.equal(parsed.banner, "Line1\nLine2\nLine3");
});

test("file write and read round-trip", () => {
  const data = {
    app: "xun-demo",
    version: new Tagged("ver", "0.1.1"),
    server: {
      host: "0.0.0.0",
      port: 9000,
      ssl: true,
      cert: "/path/to/cert",
    },
    tags: ["prod", "web", "!s-flag"],
    raw_bytes: new Uint8Array([0x01, 0x02, 0xfe, 0xff]),
    intro: "Hello XUN!\nSecond Line.\nThird Line.",
  };

  const tmpFile = join(tmpdir(), `test_xun_${Date.now()}.xun`);
  try {
    const text = encode(data);
    writeFileSync(tmpFile, text, "utf8");

    const readText = readFileSync(tmpFile, "utf8");
    const doc = parse(readText);

    assert.equal(doc.app, "xun-demo");
    assert.deepEqual(doc.version, new Tagged("ver", "0.1.1"));
    assert.equal(doc.server.host, "0.0.0.0");
    assert.equal(doc.server.port, 9000);
    assert.equal(doc.server.ssl, true);
    assert.equal(doc.server.cert, "/path/to/cert");
    assert.deepEqual(doc.tags, ["prod", "web", "!s-flag"]);
    assert.deepEqual(doc.raw_bytes, new Uint8Array([0x01, 0x02, 0xfe, 0xff]));
    assert.equal(doc.intro, "Hello XUN!\nSecond Line.\nThird Line.");
  } finally {
    try {
      unlinkSync(tmpFile);
    } catch {}
  }
});
