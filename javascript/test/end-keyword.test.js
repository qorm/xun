import { test } from "node:test";
import assert from "node:assert/strict";
import { parse, XunError } from "../src/xun.js";

// RFC-0001: Optional 'end' block delimiter

test("RFC-0001: bare 'end' closes a top-level dict", () => {
  const src = `server:
  host: localhost
  port: 8080
end
`;
  const doc = parse(src);
  assert.equal(doc.server.host, "localhost");
  assert.equal(doc.server.port, "8080");
});

test("RFC-0001: bare 'end' closes a nested dict", () => {
  const src = `server:
  host: localhost
  tls:
    cert: /etc/ssl/cert.pem
    mode: 755
  end
  port: 8080
end
`;
  const doc = parse(src);
  assert.equal(doc.server.tls.cert, "/etc/ssl/cert.pem");
  assert.equal(doc.server.port, "8080");
});

test("RFC-0001: 'end <key>' closes a named nested dict", () => {
  const src = `server:
  host: localhost
  tls:
    cert: /etc/ssl/cert.pem
  end tls
  port: 8080
end server
`;
  const doc = parse(src);
  assert.equal(doc.server.host, "localhost");
  assert.equal(doc.server.tls.cert, "/etc/ssl/cert.pem");
  assert.equal(doc.server.port, "8080");
});

test("RFC-0001: bare 'end' closes a list block", () => {
  const src = `servers:
  - host: a
    port: 80
  - host: b
    port: 81
end
`;
  const doc = parse(src);
  assert.equal(doc.servers.length, 2);
  assert.equal(doc.servers[0].host, "a");
  assert.equal(doc.servers[1].host, "b");
});

test("RFC-0001: bare 'end' is equivalent to dedent (backward compat)", () => {
  const withEnd = `a: 1
b: 2
end
`;
  const withoutEnd = `a: 1
b: 2
`;
  const docWith = parse(withEnd);
  const docWithout = parse(withoutEnd);
  assert.deepEqual(docWith, docWithout);
});

test("RFC-0001: 'end <key>' mismatch throws E0042", () => {
  const src = `server:
  host: localhost
  port: 8080
end tls
`;
  assert.throws(() => parse(src), (err) => {
    return err instanceof XunError && /end-key mismatch/.test(err.message);
  });
});

test("RFC-0001: bare 'end' on root is allowed", () => {
  const src = `a: 1
end
`;
  const doc = parse(src);
  assert.equal(doc.a, "1");
});

test("RFC-0001: 'end' as a bare key in a dict works as expected", () => {
  // 'end' as a dict key is just a regular identifier, not a delimiter.
  const src = `end: 1
end2: 2
`;
  const doc = parse(src);
  assert.equal(doc.end, "1");
  assert.equal(doc.end2, "2");
});

test("RFC-0001: 'end' inside a multiline block is literal", () => {
  const src = `script: |
  echo "end of script"
|
`;
  const doc = parse(src);
  assert.equal(doc.script, 'echo "end of script"');
});

test("RFC-0001: 'end' as a key in a dictionary is allowed (not a delimiter)", () => {
  const src = `end: foo
other: bar
`;
  const doc = parse(src);
  assert.equal(doc.end, "foo");
  assert.equal(doc.other, "bar");
});

test("RFC-0001: deeply nested 'end' chains all close correctly", () => {
  const src = `a:
  b:
    c:
      d: 1
    end c
  end b
end a
e: 2
`;
  const doc = parse(src);
  assert.equal(doc.a.b.c.d, "1");
  assert.equal(doc.e, "2");
});

test("RFC-0001: 'end' allows sibling content at same indent after", () => {
  const src = `server:
  host: a
end server
proxy:
  host: b
end proxy
`;
  const doc = parse(src);
  assert.equal(doc.server.host, "a");
  assert.equal(doc.proxy.host, "b");
});