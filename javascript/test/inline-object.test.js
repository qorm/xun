import { test } from "node:test";
import assert from "node:assert/strict";
import { parse, Tagged, XunError } from "../src/xun.js";

// RFC-0002: Inline object / array literals

test("RFC-0002: inline object literal in list item", () => {
  const src = `servers:
  - {host: a, port: 80}
  - {host: b, port: 81}
`;
  const doc = parse(src);
  assert.equal(doc.servers.length, 2);
  assert.equal(doc.servers[0].host, "a");
  assert.equal(doc.servers[0].port, "80");
  assert.equal(doc.servers[1].host, "b");
  assert.equal(doc.servers[1].port, "81");
});

test("RFC-0002: inline object with quoted value", () => {
  const src = `servers:
  - {host: "my host", port: 80}
`;
  const doc = parse(src);
  assert.equal(doc.servers[0].host, "my host");
});

test("RFC-0002: inline object with tagged values", () => {
  const src = `cfg:
  - {port: !n 8080, mode: !o 755, color: !xb FF00AA}
`;
  const doc = parse(src);
  assert.equal(doc.cfg[0].port, 8080);
  assert.deepEqual(doc.cfg[0].mode, new Tagged("o", "755"));
  assert.deepEqual(doc.cfg[0].color, Uint8Array.from([0xff, 0x00, 0xaa]));
});

test("RFC-0002: inline object with empty value", () => {
  const src = `cfg:
  - {name: ""}
  - {empty:}
`;
  const doc = parse(src);
  // First has explicit empty string via ""; second uses trailing colon -> empty string
  assert.equal(doc.cfg[0].name, "");
  assert.equal(doc.cfg[1].empty, "");
});

test("RFC-0002: compact array of inline objects (no tag)", () => {
  const src = `peers: [{host: a, port: 80}, {host: b, port: 81}]
`;
  const doc = parse(src);
  assert.equal(doc.peers.length, 2);
  assert.equal(doc.peers[0].host, "a");
  assert.equal(doc.peers[1].host, "b");
});

test("RFC-0002: compact tagged array of inline objects", () => {
  // Mixed: one inline object + one scalar in a numeric array.
  // Inline objects in compact arrays must have a tag prefix? Let's check.
  const src = `items: !o[{a: 1, b: 2}, {c: 3}]
`;
  const doc = parse(src);
  assert.equal(doc.items.length, 2);
  assert.equal(doc.items[0].a, "1");
  assert.equal(doc.items[0].b, "2");
  assert.equal(doc.items[1].c, "3");
});

test("RFC-0002: mix block-form and inline form in same list", () => {
  const src = `servers:
  - {host: a, port: 80}
  - host: b
    port: 81
    tls: enabled
`;
  const doc = parse(src);
  assert.equal(doc.servers.length, 2);
  assert.equal(doc.servers[0].host, "a");
  assert.equal(doc.servers[1].host, "b");
  assert.equal(doc.servers[1].tls, "enabled");
});

test("RFC-0002: empty inline object {}", () => {
  const src = `cfg:
  - {}
`;
  const doc = parse(src);
  assert.deepEqual(doc.cfg[0], {});
});

test("RFC-0002: inline object inside nested block", () => {
  const src = `data:
  items:
    - {id: 1, name: alice}
    - {id: 2, name: bob}
`;
  const doc = parse(src);
  assert.equal(doc.data.items.length, 2);
  assert.equal(doc.data.items[0].id, "1");
  assert.equal(doc.data.items[0].name, "alice");
});

test("RFC-0002: duplicate keys in inline object throw", () => {
  const src = `cfg:
  - {a: 1, a: 2}
`;
  assert.throws(() => parse(src), (err) => {
    return err instanceof XunError && /duplicate key 'a'/.test(err.message);
  });
});

test("RFC-0002: malformed inline object (missing :) throws", () => {
  const src = `cfg:
  - {a, b: 2}
`;
  assert.throws(() => parse(src), XunError);
});

test("RFC-0002: inline object with whitespace tolerance", () => {
  const src = `cfg:
  - { host:a, port:80 }
`;
  // The key-value separator must be ': ' or trailing ':'
  // { host:a } - 'host:a' is not valid since 'a' is not separated by space
  // Actually our parser requires ': ' OR trailing ':'. So 'host:a' fails.
  assert.throws(() => parse(src), XunError);
});

test("RFC-0002: inline object preserves order of keys", () => {
  const src = `cfg:
  - {z: 1, a: 2, m: 3}
`;
  const doc = parse(src);
  assert.deepEqual(Object.keys(doc.cfg[0]), ["z", "a", "m"]);
});

test("RFC-0002: nested compact array inside inline object value", () => {
  const src = `cfg:
  - {tags: !s[a, b, c], port: !n 80}
`;
  const doc = parse(src);
  assert.equal(doc.cfg[0].tags.length, 3);
  // !n returns plain number (not Tagged), unlike !o / !xb etc.
  assert.equal(doc.cfg[0].port, 80);
});

test("RFC-0002: traditional block list still works (regression)", () => {
  const src = `items:
  - one
  - two
`;
  const doc = parse(src);
  assert.deepEqual(doc.items, ["one", "two"]);
});

test("RFC-0002: traditional !n[] compact array still works (regression)", () => {
  const src = `ports: !n[80, 443, 8080]
`;
  const doc = parse(src);
  assert.equal(doc.ports.length, 3);
  assert.equal(doc.ports[0], 80);
  assert.equal(doc.ports[1], 443);
  assert.equal(doc.ports[2], 8080);
});

test("RFC-0002: list with inline object that has 'end' as a key", () => {
  // 'end' as a key inside { ... } must not be confused with the block-close keyword
  const src = `cfg:
  - {end: foo, value: 1}
`;
  const doc = parse(src);
  assert.equal(doc.cfg[0].end, "foo");
  assert.equal(doc.cfg[0].value, "1");
});