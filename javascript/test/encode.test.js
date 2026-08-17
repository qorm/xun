import { test } from "node:test";
import assert from "node:assert/strict";
import { writeFileSync, readFileSync, unlinkSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import {
  encode,
  decode,
  stringify,
  parse,
  unpack,
  parseSize,
  parseDuration,
  parseVersion,
  Tagged,
  XunError,
} from "../src/xun.js";

test("encode empty dict", () => {
  assert.equal(encode({}), "");
  assert.equal(stringify({}), "");
  assert.deepEqual(decode(""), {});
  assert.deepEqual(parse(""), {});
});

test("encode non-dict root throws error", () => {
  assert.throws(() => encode(["item1"]), XunError);
  assert.throws(() => encode("str"), XunError);
  assert.throws(() => encode(123), XunError);
});

test("encode basic types and decode round-trip", () => {
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
  const parsed = decode(text);

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

test("encode native Date and unpack helpers", () => {
  const d = new Date("2026-08-14T16:54:00.000Z");
  const data = {
    time: d,
    size: new Tagged("sz", "10MiB"),
    duration: new Tagged("du", "1h30m"),
    version: new Tagged("ver", "3.10.1"),
  };

  const text = encode(data);
  const doc = decode(text);

  assert.equal(doc.time.toDate().toISOString(), d.toISOString());
  assert.equal(doc.size.toBytesSize(), 10485760);
  assert.equal(doc.duration.toDurationSeconds(), 5400);
  assert.deepEqual(doc.version.toVersionParts(), [3, 10, 1]);

  assert.equal(parseSize("3KB"), 3000);
  assert.equal(parseDuration("1d2h"), 86400 + 7200);
  assert.deepEqual(parseVersion("1.0.0"), [1, 0, 0]);

  const unpacked = unpack(doc);
  assert.equal(unpacked.size, 10485760);
  assert.equal(unpacked.duration, 5400);
  assert.deepEqual(unpacked.version, [3, 10, 1]);
});

test("detect circular references in encoder", () => {
  const a = {};
  const b = { a };
  a.b = b;
  assert.throws(() => encode(a), (err) => {
    return err instanceof XunError && err.message.includes("circular reference");
  });
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
  const parsed = decode(text);

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

test("encode strips surrounding double-quote pairs", () => {
  assert.equal(encode({ a: '"hello"' }), "a: hello\n");
  assert.equal(encode({ a: '""hello world""' }), "a: hello world\n");
  assert.equal(encode({ a: '""' }), "a:\n");
  assert.equal(encode({ a: '"!x"' }), "a: !s !x\n");
  assert.equal(encode({ a: '"' }), 'a: "\\""\n');
  assert.equal(encode({ a: '"unclosed' }), 'a: "\\"unclosed"\n');
  assert.equal(encode({ items: ['"a"', '"b"'] }), "items:\n  - a\n  - b\n");
  // Tagged values are never stripped.
  assert.equal(encode({ a: new Tagged("s", '"keep"') }), "a: !s \"keep\"\n");
});

test("encode numeric-looking strings with !s", () => {
  assert.equal(encode({ a: "123" }), "a: !s 123\n");
  assert.equal(encode({ a: "3.10" }), "a: !s 3.10\n");
  assert.equal(encode({ a: "-1.5" }), "a: !s -1.5\n");
  assert.equal(encode({ a: "1e-3" }), "a: !s 1e-3\n");
  assert.equal(encode({ a: "0xFF" }), "a: !s 0xFF\n");
  assert.equal(encode({ a: "0b10" }), "a: !s 0b10\n");
  assert.equal(encode({ a: "0o755" }), "a: !s 0o755\n");
  assert.equal(encode({ a: "Infinity" }), "a: !s Infinity\n");
  assert.equal(encode({ a: '"8080"' }), "a: !s 8080\n");
  assert.equal(encode({ a: "123 " }), 'a: !s "123 "\n');
  assert.equal(encode({ items: ["80", "443"] }), "items:\n  - !s 80\n  - !s 443\n");
  // Actual numbers still use numeric tags.
  assert.equal(encode({ a: 123 }), "a: !i 123\n");
  // Non-numeric strings stay untagged.
  assert.equal(encode({ a: "hello" }), "a: hello\n");
  assert.equal(encode({ a: "123abc" }), "a: 123abc\n");
  assert.equal(encode({ a: "1.2.3" }), "a: 1.2.3\n");
  // Decode still treats untagged numbers as strings.
  assert.equal(decode("a: 123\n").a, "123");
  assert.equal(encode(decode("a: 123\n")), "a: !s 123\n");
  assert.equal(decode('a: "Alice"\n').a, "Alice");
  assert.equal(decode('a: !s "3.10 "\n').a, "3.10 ");
});

test("encode quoted escape sequences", () => {
  assert.equal(encode({ a: 'say "hi"' }), 'a: "say \\"hi\\""\n');
  assert.equal(decode('a: "say \\"hi\\""\n').a, 'say "hi"');
  assert.equal(encode({ a: "path\\to" }), 'a: "path\\\\to"\n');
});

test("file write and read round-trip", () => {
  const data = {
    app: "xun-demo",
    version: new Tagged("ver", "0.1.5"),
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
    const doc = decode(readText);

    assert.equal(doc.app, "xun-demo");
    assert.deepEqual(doc.version, new Tagged("ver", "0.1.5"));
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
