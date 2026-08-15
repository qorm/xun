import { test } from "node:test";
import assert from "node:assert/strict";
import {
  decode,
  encode,
  parse,
  stringify,
  unpack,
  parseSize,
  parseDuration,
  parseVersion,
  Tagged,
  XunError,
} from "../src/xun.js";

test("Unicode and Chinese keys and values", () => {
  const data = {
    服务名称: "订单处理系统",
    版本号: new Tagged("ver", "2.1.0"),
    端口: 8080,
    配置项: {
      超时时间: new Tagged("du", "30s"),
      允许跨域: true,
      白名单IP: [new Tagged("ip", "127.0.0.1"), new Tagged("ip", "192.168.1.1")],
    },
  };

  const text = encode(data);
  const doc = decode(text);

  assert.equal(doc["服务名称"], "订单处理系统");
  assert.deepEqual(doc["版本号"], new Tagged("ver", "2.1.0"));
  assert.equal(doc["端口"], 8080);
  assert.deepEqual(doc["配置项"]["超时时间"], new Tagged("du", "30s"));
  assert.equal(doc["配置项"]["允许跨域"], true);
  assert.deepEqual(doc["配置项"]["白名单IP"][0], new Tagged("ip", "127.0.0.1"));
});

test("Extreme indentation and error reporting with line/col", () => {
  // Odd spaces indent (3 spaces)
  assert.throws(() => decode("a:\n   b: 1\n"), (err) => {
    return err instanceof XunError && err.line === 2 && err.message.includes("multiple of 2");
  });

  // Tab character indent
  assert.throws(() => decode("a:\n\tb: 1\n"), (err) => {
    return err instanceof XunError && err.line === 2 && err.message.includes("tab");
  });

  // Indent jump (0 to 4)
  assert.throws(() => decode("a:\n    b: 1\n"), (err) => {
    return err instanceof XunError && err.line === 2 && err.message.includes("indent");
  });

  // Mixing dictionary and list at the same level
  assert.throws(() => decode("server:\n  host: localhost\n  - item1\n"), (err) => {
    return err instanceof XunError && err.line === 3 && err.message.includes("mix");
  });

  assert.throws(() => decode("items:\n  - item1\n  key: val\n"), (err) => {
    return err instanceof XunError && err.line === 3 && err.message.includes("mix");
  });
});

test("Full 20 core tags verification", () => {
  const raw = `
str_plain: hello world
str_special: !s !not_a_tag
str_empty:
num_int: !i 42
num_float: !f 3.14159
num_hex: !x DEAD_BEEF
num_oct: !o 755
flag_t: !b true
flag_f: !b false
date_v: !d 2026-08-14
time_v: !t 16:54:00.123
dt_v: !dt 2026-08-14T16:54:00+08:00
tz_v: !tz Asia/Shanghai
dur_v: !du 1d2h30m15s
sz_v: !sz 10GiB
unix_v: !unix 1700000000
ver_v: !ver 3.10.1
uuid_v: !uuid 12345678-1234-5678-1234-567812345678
ip4_v: !ip 127.0.0.1
ip6_v: !ip ::1
bytes_v: !xb FF00AA
b64_v: !b64 SGVsbG8=
char_v: !c A
char_cp: !c U+4E2D
custom_v: !sql SELECT * FROM users
`;

  const doc = decode(raw);
  assert.equal(doc.str_plain, "hello world");
  assert.equal(doc.str_special, "!not_a_tag");
  assert.equal(doc.str_empty, "");
  assert.equal(doc.num_int, 42);
  assert.equal(doc.num_float, 3.14159);
  assert.equal(doc.num_hex, 0xdeadbeef);
  assert.equal(doc.num_oct, 0o755);
  assert.equal(doc.flag_t, true);
  assert.equal(doc.flag_f, false);
  assert.deepEqual(doc.date_v, new Tagged("d", "2026-08-14"));
  assert.deepEqual(doc.time_v, new Tagged("t", "16:54:00.123"));
  assert.deepEqual(doc.dt_v, new Tagged("dt", "2026-08-14T16:54:00+08:00"));
  assert.deepEqual(doc.tz_v, new Tagged("tz", "Asia/Shanghai"));
  assert.deepEqual(doc.dur_v, new Tagged("du", "1d2h30m15s"));
  assert.deepEqual(doc.sz_v, new Tagged("sz", "10GiB"));
  assert.equal(doc.unix_v, 1700000000);
  assert.deepEqual(doc.ver_v, new Tagged("ver", "3.10.1"));
  assert.deepEqual(doc.uuid_v, new Tagged("uuid", "12345678-1234-5678-1234-567812345678"));
  assert.deepEqual(doc.ip4_v, new Tagged("ip", "127.0.0.1"));
  assert.deepEqual(doc.ip6_v, new Tagged("ip", "::1"));
  assert.deepEqual(doc.bytes_v, new Uint8Array([0xff, 0x00, 0xaa]));
  assert.deepEqual(doc.b64_v, new Uint8Array([72, 101, 108, 108, 111]));
  assert.deepEqual(doc.char_v, new Tagged("c", "A"));
  assert.deepEqual(doc.char_cp, new Tagged("c", "中"));
  assert.deepEqual(doc.custom_v, new Tagged("sql", "SELECT * FROM users"));
});

test("Compact array variations and error validation", () => {
  const src = `
numbers: !n[1, 2, 3, 4]
floats: !f[1.1, 2.2, 3.3]
chars: !c[a, b, c]
ips: !ip[10.0.0.1, 10.0.0.2]
versions: !ver[1.0, 2.0, 3.10]
`;
  const doc = decode(src);
  assert.deepEqual(doc.numbers, [1, 2, 3, 4]);
  assert.deepEqual(doc.floats, [1.1, 2.2, 3.3]);
  assert.deepEqual(doc.chars, [new Tagged("c", "a"), new Tagged("c", "b"), new Tagged("c", "c")]);
  assert.deepEqual(doc.ips, [new Tagged("ip", "10.0.0.1"), new Tagged("ip", "10.0.0.2")]);
  assert.deepEqual(doc.versions, [new Tagged("ver", "1.0"), new Tagged("ver", "2.0"), new Tagged("ver", "3.10")]);

  // Invalid compact array: missing closing bracket
  assert.throws(() => decode("ports: !n[80, 443\n"), XunError);
});

test("Multiline blocks with custom delimiters and inner special chars", () => {
  const src = `
sql_query: |SQL
  SELECT id, name, email
  FROM users
  WHERE status = 'active'
  # This is not a comment line
  AND age > 18;
SQL
empty_block: |
|
`;
  const doc = decode(src);
  assert.equal(
    doc.sql_query,
    "SELECT id, name, email\nFROM users\nWHERE status = 'active'\n# This is not a comment line\nAND age > 18;"
  );
  assert.equal(doc.empty_block, "");

  // Unclosed multiline block
  assert.throws(() => decode("a: |\n  Line 1\n  Line 2\n"), XunError);
});

test("Empty containers edge cases", () => {
  const src = `
empty_root_dict: {}
empty_root_list: []
nested:
  empty_d: {}
  empty_l: []
list_of_empties:
  - {}
  - []
  - simple
`;
  const doc = decode(src);
  assert.deepEqual(doc.empty_root_dict, {});
  assert.deepEqual(doc.empty_root_list, []);
  assert.deepEqual(doc.nested.empty_d, {});
  assert.deepEqual(doc.nested.empty_l, []);
  assert.deepEqual(doc.list_of_empties, [{}, [], "simple"]);
});

test("Deep nesting and boundary recursion", () => {
  let nestedObj = { value: "deepest" };
  for (let i = 0; i < 20; i++) {
    nestedObj = { [`level_${i}`]: nestedObj };
  }
  const text = encode(nestedObj);
  const doc = decode(text);

  let cur = doc;
  for (let i = 19; i >= 0; i--) {
    cur = cur[`level_${i}`];
  }
  assert.equal(cur.value, "deepest");
});
