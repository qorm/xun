import { test } from "node:test";
import assert from "node:assert/strict";
import { decode, encode, Tagged, XunError } from "../src/xun.js";

const MUST_TAG = [
  "0",
  "00",
  "012",
  "08",
  "8080",
  "+123",
  "-123",
  "-0",
  "+0",
  "3.10",
  "3.",
  ".5",
  ".0",
  "0.",
  "0.0",
  "00.1",
  "-.5",
  "+.5",
  "+0.0",
  "-0.0",
  "1e3",
  "1E-3",
  "1e+10",
  "0e0",
  "1E+0",
  "1e-0",
  "+1.5e-10",
  "5.e2",
  "+.5e2",
  "-.5E-1",
  "0xFF",
  "0Xff",
  "0x0",
  "0xabcdef",
  "0XABCDEF",
  "0b10",
  "0B10",
  "0b0",
  "0b01",
  "0o755",
  "0O7",
  "0o0",
  "0o07",
  "Infinity",
  "+Infinity",
  "-Infinity",
  "9007199254740991",
  "9007199254740993",
  "-0x10",
  "+0x10",
  "-0b1",
  "+0b10",
  "-0o10",
  " 123",
];

const MUST_NOT_TAG = [
  "hello",
  "123abc",
  "abc123",
  "1.2.3",
  "3.1.0",
  "1e",
  "1e+",
  "e3",
  "e10",
  "5.e",
  ".",
  "+",
  "-",
  "0x",
  "0b",
  "0o",
  "0xg",
  "0xG",
  "0b2",
  "0o8",
  "0x10n",
  "123n",
  "infinity",
  "INFINITY",
  "Inf",
  "NaN",
  "true",
  "false",
  "null",
  "1_000",
  "1_2",
  "0xFF_AA",
  "127.0.0.1",
  "2026-08-14",
  "::1",
];

function nestEncode(levels) {
  let o = { v: "leaf" };
  for (let i = 0; i < levels; i++) o = { c: o };
  return o;
}

function nestSource(levels) {
  let s = "";
  for (let i = 0; i < levels; i++) s += `${"  ".repeat(i)}k${i}:\n`;
  s += `${"  ".repeat(levels)}v: leaf\n`;
  return s;
}

function glyphExpect(s) {
  const quoted = s.trim() !== s || s.includes('"') || s.includes("\\");
  let body = s;
  if (quoted) {
    body = `"${s.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
  }
  const tagged = s.startsWith("!") || s === "[]" || s === "{}" || s.startsWith("|") ||
    /^[ \t\n\r\f\v]*[+-]?(?:Infinity|0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)[ \t\n\r\f\v]*$/.test(s);
  return tagged ? `!s ${body}` : body;
}

test("extreme: numeric-looking strings must encode as !s and round-trip as string", () => {
  for (const s of MUST_TAG) {
    const text = encode({ a: s });
    assert.equal(text, `a: ${glyphExpect(s)}\n`, `encode ${JSON.stringify(s)}`);
    const doc = decode(text);
    assert.equal(typeof doc.a, "string", `typeof ${JSON.stringify(s)}`);
    assert.equal(doc.a, s, `round-trip ${JSON.stringify(s)}`);
  }
});

test("extreme: quoted strings preserve leading and trailing spaces", () => {
  assert.equal(encode({ a: "123 " }), 'a: !s "123 "\n');
  assert.equal(encode({ a: "  3.14  " }), 'a: !s "  3.14  "\n');
  assert.equal(encode({ a: " hello" }), 'a: " hello"\n');
  assert.equal(decode('a: "123 "\n').a, "123 ");
  assert.equal(decode('a: !s "8080 "\n').a, "8080 ");
  assert.equal(decode('a: ""\n').a, "");
  assert.equal(decode('a: "Alice"\n').a, "Alice");
  assert.equal(encode(decode('a: "123 "\n')), 'a: !s "123 "\n');
});

test("extreme: non-numeric strings stay untagged", () => {
  for (const s of MUST_NOT_TAG) {
    assert.equal(encode({ a: s }), `a: ${s}\n`, `encode ${JSON.stringify(s)}`);
    assert.equal(decode(`a: ${s}\n`).a, s);
  }
});

test("extreme: syntactic specials still need !s", () => {
  assert.equal(encode({ a: "!x" }), "a: !s !x\n");
  assert.equal(encode({ a: "[]" }), "a: !s []\n");
  assert.equal(encode({ a: "{}" }), "a: !s {}\n");
  assert.equal(encode({ a: "|foo" }), "a: !s |foo\n");
  assert.equal(encode({ a: "|" }), "a: !s |\n");
});

test("extreme: quote stripping then numeric glyph still gets !s", () => {
  assert.equal(encode({ a: '"8080"' }), "a: !s 8080\n");
  assert.equal(encode({ a: '""3.10""' }), "a: !s 3.10\n");
  assert.equal(encode({ a: '"0xFF"' }), "a: !s 0xFF\n");
  assert.equal(encode({ a: '"Infinity"' }), "a: !s Infinity\n");
  assert.equal(encode({ items: ['"80"', '"443"'] }), "items:\n  - !s 80\n  - !s 443\n");
});

test("extreme: list and nested containers preserve string type", () => {
  const data = {
    ports: ["80", "443", "8080"],
    mixed: ["1", 1, "x"],
    deep: { inner: { code: "007" } },
  };
  const text = encode(data);
  assert.equal(text, "ports:\n  - !s 80\n  - !s 443\n  - !s 8080\nmixed:\n  - !s 1\n  - !i 1\n  - x\ndeep:\n  inner:\n    code: !s 007\n");
  const doc = decode(text);
  assert.deepEqual(doc.ports, ["80", "443", "8080"]);
  assert.equal(typeof doc.ports[0], "string");
  assert.equal(doc.mixed[0], "1");
  assert.equal(doc.mixed[1], 1);
  assert.equal(doc.deep.inner.code, "007");
});

test("extreme: untagged decode stays string; re-encode becomes explicit !s", () => {
  const src = "a: 123\nb: 3.10\nc: 0xFF\nd: Infinity\ne: true\n";
  const doc = decode(src);
  assert.equal(doc.a, "123");
  assert.equal(doc.b, "3.10");
  assert.equal(doc.c, "0xFF");
  assert.equal(doc.d, "Infinity");
  assert.equal(doc.e, "true");
  assert.equal(encode(doc), "a: !s 123\nb: !s 3.10\nc: !s 0xFF\nd: !s Infinity\ne: true\n");
});

test("extreme: real numbers keep numeric tags", () => {
  assert.equal(encode({ a: 0 }), "a: !i 0\n");
  assert.equal(encode({ a: 123 }), "a: !i 123\n");
  assert.equal(encode({ a: -7 }), "a: !i -7\n");
  assert.equal(encode({ a: 3.14 }), "a: !f 3.14\n");
  assert.equal(encode({ a: true }), "a: !b true\n");
  const doc = decode(encode({ a: 123, b: "123" }));
  assert.equal(doc.a, 123);
  assert.equal(doc.b, "123");
});

test("extreme: empty / blank / comments-only documents", () => {
  assert.deepEqual(decode(""), {});
  assert.deepEqual(decode("\n\n"), {});
  assert.deepEqual(decode("# only\n# comments\n"), {});
  assert.equal(encode({}), "");
  assert.equal(encode({ a: "" }), "a:\n");
  assert.equal(decode("a:\n").a, "");
});

test("extreme: BOM is ignored; NUL and oversize documents fail", () => {
  assert.equal(decode("\uFEFFa: hello\n").a, "hello");
  assert.throws(() => decode("a: ok\0no\n"), XunError);
  assert.throws(() => decode("x".repeat(1024 * 1024 + 1)), XunError);
  assert.deepEqual(decode("a: " + "x".repeat(100) + "\n").a, "x".repeat(100));
});

test("extreme: decode nesting at the 64-level boundary", () => {
  decode(nestSource(64));
  assert.throws(() => decode(nestSource(65)), (err) => err instanceof XunError && /nesting/.test(err.message));
});

test("extreme: encode nesting at the 64-level boundary", () => {
  const text = encode(nestEncode(64));
  const doc = decode(text);
  let cur = doc;
  for (let i = 0; i < 64; i++) cur = cur.c;
  assert.equal(cur.v, "leaf");
  assert.throws(() => encode(nestEncode(65)), (err) => err instanceof XunError && /nesting/.test(err.message));
});

test("extreme: encoder rejects empty and illegal keys", () => {
  assert.throws(() => encode({ "": "x" }), XunError);
  assert.throws(() => encode({ "a: b": "x" }), XunError);
  assert.throws(() => encode({ "a:": "x" }), XunError);
  assert.throws(() => encode({ "a\nb": "x" }), XunError);
});

test("extreme: numeric-looking keys stay keys; values still tagged", () => {
  const text = encode({ 8080: "8080", "3.10": "3.10" });
  const doc = decode(text);
  assert.equal(doc["8080"], "8080");
  assert.equal(doc["3.10"], "3.10");
  assert.match(text, /8080: !s 8080/);
  assert.match(text, /3\.10: !s 3\.10/);
});

test("extreme: multiline numeric text is a string block, not a number", () => {
  const text = encode({ a: "123\n456" });
  assert.equal(text, "a: |\n  123\n  456\n|\n");
  assert.equal(decode(text).a, "123\n456");
});

test("extreme: CRLF / CR source and already-tagged !s", () => {
  assert.equal(decode("a: 123\r\nb: x\r\n").a, "123");
  assert.equal(decode("a: 123\rb: x\r").a, "123");
  assert.equal(decode("a: !s 123\n").a, "123");
  assert.equal(decode("a: !s 3.10\n").a, "3.10");
  assert.deepEqual(decode("v: !ver 3.10\n").v, new Tagged("ver", "3.10"));
});
