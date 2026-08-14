export class XunError extends Error {
  constructor(message, line = 0) {
    super(line ? `line ${line}: ${message}` : message);
    this.name = "XunError";
    this.line = line;
  }
}

export class Tagged {
  constructor(tag, value) {
    this.tag = tag;
    this.value = value;
  }
}

const IDENT = /^[A-Za-z_][A-Za-z0-9_]*$/;
const MAX_BYTES = 1024 * 1024;
const MAX_DEPTH = 64;
const CORE = new Set([
  "s", "n", "i", "f", "x", "xb", "o", "b", "d", "t", "dt", "tz",
  "du", "sz", "unix", "ver", "uuid", "ip", "b64", "c",
]);

export function parse(source) {
  if (typeof source !== "string") {
    throw new XunError("source must be a string");
  }
  if (source.length > MAX_BYTES) {
    throw new XunError("document exceeds 1MB");
  }
  if (source.includes("\u0000")) {
    throw new XunError("NUL is not allowed");
  }
  if (source.charCodeAt(0) === 0xfeff) source = source.slice(1);
  const parser = new Parser(splitLines(source));
  return parser.parseDocument();
}

function splitLines(source) {
  const lines = [];
  let start = 0;
  let n = 1;
  for (let i = 0; i <= source.length; i++) {
    const c = source.charCodeAt(i);
    const atEnd = i === source.length;
    const isLF = c === 10;
    const isCR = c === 13;
    if (!atEnd && !isLF && !isCR) continue;
    let raw = source.slice(start, i);
    if (isCR && source.charCodeAt(i + 1) === 10) i++;
    lines.push(makeLine(raw, n));
    n++;
    start = i + 1;
  }
  if (source.length === 0) return [];
  return lines;
}

function makeLine(raw, n) {
  let i = 0;
  while (i < raw.length && raw.charCodeAt(i) === 32) i++;
  if (i < raw.length && raw.charCodeAt(i) === 9) {
    throw new XunError("tab is not allowed", n);
  }
  if (i % 2 !== 0) {
    throw new XunError("indent must be a multiple of 2", n);
  }
  const text = raw.slice(i).replace(/[ \t]+$/, "");
  return { raw, indent: i, text, n, blank: text.length === 0 };
}

class Parser {
  constructor(lines) {
    this.lines = lines;
    this.i = 0;
    this.env = new Map();
    this.resolving = new Set();
  }

  peek() {
    return this.i < this.lines.length ? this.lines[this.i] : null;
  }

  skipNoise() {
    while (this.peek()) {
      const l = this.peek();
      if (l.blank || l.text.startsWith("#")) this.i++;
      else break;
    }
  }

  parseDocument() {
    this.parseVars();
    this.skipNoise();
    if (!this.peek()) return {};
    const first = this.peek();
    if (first.indent !== 0) {
      throw new XunError("document must start at indent 0", first.n);
    }
    if (this.isListItem(first)) {
      throw new XunError("root must be a dictionary", first.n);
    }
    return this.parseDict(0, 0);
  }

  parseVars() {
    while (this.peek()) {
      const l = this.peek();
      if (l.blank || l.text.startsWith("#")) {
        this.i++;
        continue;
      }
      if (l.indent !== 0) break;
      if (!l.text.startsWith("$")) break;
      const m = l.text.match(/^\$([A-Za-z_][A-Za-z0-9_]*):(.*)$/);
      if (!m) throw new XunError("invalid variable definition", l.n);
      if (m[2].length && !m[2].startsWith(" ")) {
        throw new XunError("expected ': ' in variable definition", l.n);
      }
      const name = m[1];
      if (this.env.has(name)) throw new XunError(`duplicate variable $${name}`, l.n);
      this.i++;
      const raw = m[2].startsWith(" ") ? m[2].slice(1) : "";
      const value = this.parseValue(raw, 0, l.n, 1, { inVars: true });
      this.env.set(name, value);
    }
  }

  parseDict(indent, depth) {
    if (depth > MAX_DEPTH) throw new XunError("nesting exceeds 64", this.peek()?.n);
    const obj = {};
    while (this.peek()) {
      this.skipNoise();
      const l = this.peek();
      if (!l || l.blank) break;
      if (l.indent < indent) break;
      if (l.indent > indent) {
        throw new XunError("invalid indent jump", l.n);
      }
      if (l.text.startsWith("$") && indent === 0) {
        throw new XunError("variable definitions only allowed at file start", l.n);
      }
      if (this.isListItem(l)) {
        throw new XunError("cannot mix list items into a dictionary", l.n);
      }
      const { key, rest } = splitKey(l.text, l.n);
      if (Object.prototype.hasOwnProperty.call(obj, key)) {
        throw new XunError(`duplicate key '${key}'`, l.n);
      }
      this.i++;
      obj[key] = this.parseValue(rest, indent, l.n, depth + 1, {});
    }
    return obj;
  }

  parseList(indent, depth, itemTag = null) {
    if (depth > MAX_DEPTH) throw new XunError("nesting exceeds 64", this.peek()?.n);
    const arr = [];
    while (this.peek()) {
      this.skipNoise();
      const l = this.peek();
      if (!l || l.blank) break;
      if (l.indent < indent) break;
      if (l.indent > indent) throw new XunError("invalid indent jump", l.n);
      if (!this.isListItem(l)) {
        throw new XunError("cannot mix dictionary keys into a list", l.n);
      }
      const rest = l.text === "-" ? "" : l.text.slice(2);
      this.i++;
      let val = this.parseValue(rest, indent, l.n, depth + 1, {});
      if (itemTag) val = applyTag(itemTag, glyphOf(val), l.n);
      arr.push(val);
    }
    return arr;
  }

  isListItem(l) {
    return l.text === "-" || l.text.startsWith("- ");
  }

  parseValue(raw, parentIndent, lineNo, depth, opts) {
    if (raw === "[]") return [];
    if (raw === "{}") return {};

    const ml = matchMultiline(raw);
    if (ml) return this.readMultiline(parentIndent, ml.tag, ml.closer, lineNo, opts);

    if (raw.startsWith("!")) {
      return this.parseTagged(raw, parentIndent, lineNo, depth, opts);
    }

    if (raw === "") {
      return this.parseEmptyOrNested(parentIndent, lineNo, depth, null);
    }

    if (isWholeRef(raw) && !(opts && opts.literal)) {
      return this.lookup(raw.slice(1), lineNo);
    }
    return interpolate(raw, (name) => this.lookup(name, lineNo));
  }

  parseTagged(raw, parentIndent, lineNo, depth, opts) {
    const m = raw.match(/^!([A-Za-z_][A-Za-z0-9_]*)(.*)$/);
    if (!m) throw new XunError("invalid type tag", lineNo);
    const tag = m[1];
    const rest = m[2];

    if (rest.startsWith("[")) {
      if (tag === "s" && rest !== "[]") {
        throw new XunError("string arrays cannot use compact form", lineNo);
      }
      if (!rest.endsWith("]")) throw new XunError("unclosed compact array", lineNo);
      const inner = rest.slice(1, -1);
      if (inner.length === 0) {
        return this.parseEmptyOrNested(parentIndent, lineNo, depth, tag);
      }
      return splitCompact(inner).map((g) => applyTag(tag, g, lineNo));
    }

    if (rest === "") {
      throw new XunError(`missing value for !${tag}`, lineNo);
    }
    if (!rest.startsWith(" ")) {
      throw new XunError("expected space after type tag", lineNo);
    }
    const body = rest.slice(1);
    const ml = matchMultiline(body);
    if (ml) {
      const text = this.readMultiline(parentIndent, ml.tag, ml.closer, lineNo, opts);
      if (tag === "s") return text;
      return applyTag(tag, text, lineNo);
    }
    if (tag === "s") return body;
    if (isWholeRef(body)) return this.lookup(body.slice(1), lineNo);
    return applyTag(tag, body, lineNo);
  }

  parseEmptyOrNested(parentIndent, lineNo, depth, itemTag) {
    this.skipNoise();
    const n = this.peek();
    const child = parentIndent + 2;
    if (!n || n.blank || n.indent <= parentIndent) {
      if (itemTag) return [];
      return "";
    }
    if (n.indent !== child) throw new XunError("child indent must be parent + 2", n.n);
    if (this.isListItem(n)) return this.parseList(child, depth, itemTag);
    if (itemTag) throw new XunError(`!${itemTag}[] expected list items`, n.n);
    return this.parseDict(child, depth);
  }

  readMultiline(parentIndent, tag, closer, lineNo, opts) {
    const base = parentIndent + 2;
    const parts = [];
    while (this.peek()) {
      const l = this.peek();
      const stripped = l.raw.replace(/[ \t]+$/, "");
      const content = stripped.replace(/^ +/, "");
      const ind = l.raw.match(/^( *)/)[1].length;
      if (!l.blank && ind === parentIndent && content === closer) {
        this.i++;
        let s = parts.join("\n");
        if (opts && opts.inVars) {
          /* variables may still be strings */
        }
        if (tag && tag !== "s") return applyTag(tag, s, lineNo);
        return s;
      }
      if (l.blank) {
        parts.push("");
        this.i++;
        continue;
      }
      if (ind < base && !l.blank) {
        throw new XunError("multiline body must indent +2, or close at opener indent", l.n);
      }
      if (l.raw.includes("\t")) throw new XunError("tab is not allowed", l.n);
      parts.push(l.raw.slice(base));
      this.i++;
    }
    throw new XunError("unclosed multiline block", lineNo);
  }

  lookup(name, lineNo) {
    if (!this.env.has(name)) throw new XunError(`undefined variable $${name}`, lineNo);
    if (this.resolving.has(name)) throw new XunError(`cyclic variable $${name}`, lineNo);
    const v = this.env.get(name);
    if (typeof v === "string" && isWholeRef(v)) {
      this.resolving.add(name);
      try {
        const r = this.lookup(v.slice(1), lineNo);
        this.env.set(name, r);
        return r;
      } finally {
        this.resolving.delete(name);
      }
    }
    return v;
  }
}

function splitKey(text, n) {
  const idx = text.indexOf(": ");
  if (idx > 0) {
    return { key: text.slice(0, idx), rest: text.slice(idx + 2) };
  }
  if (text.endsWith(":") && text.length > 1 && !text.slice(0, -1).includes(":")) {
    return { key: text.slice(0, -1), rest: "" };
  }
  if (text.endsWith(":") && text.length > 1) {
    const i = text.lastIndexOf(":");
    if (i === text.length - 1) {
      const key = text.slice(0, -1);
      if (!key || key.endsWith(" ")) throw new XunError("invalid key", n);
      return { key, rest: "" };
    }
  }
  throw new XunError("expected ': ' or trailing ':'", n);
}

function matchMultiline(raw) {
  if (raw === "|") return { tag: null, closer: "|" };
  const m = raw.match(/^\|([A-Za-z_][A-Za-z0-9_]*)$/);
  if (m) return { tag: null, closer: m[1] };
  return null;
}

function isWholeRef(raw) {
  return /^\$[A-Za-z_][A-Za-z0-9_]*$/.test(raw);
}

function interpolate(s, get) {
  return s.replace(/\$\{([A-Za-z_][A-Za-z0-9_]*)\}/g, (_, name) => {
    const v = get(name);
    return glyphOf(v);
  });
}

function glyphOf(v) {
  if (v instanceof Tagged) return v.value;
  if (v instanceof Uint8Array) {
    return [...v].map((b) => b.toString(16).padStart(2, "0")).join("");
  }
  if (typeof v === "string") return v;
  if (typeof v === "number" || typeof v === "bigint") return String(v);
  if (typeof v === "boolean") return v ? "true" : "false";
  throw new XunError("cannot interpolate a collection");
}

function splitCompact(inner) {
  return inner.split(",").map((s) => s.trim());
}

function applyTag(tag, glyph, n) {
  switch (tag) {
    case "s":
      return glyph;
    case "n":
      return parseN(glyph, n);
    case "i":
      return parseI(glyph, n);
    case "f":
      return parseF(glyph, n);
    case "x":
      return parseX(glyph, n);
    case "xb":
      return parseXb(glyph, n);
    case "o":
      return parseO(glyph, n);
    case "b":
      if (glyph === "true") return true;
      if (glyph === "false") return false;
      throw new XunError("boolean must be true or false", n);
    case "d":
      if (!/^\d{4}-\d{2}-\d{2}$/.test(glyph) || Number.isNaN(Date.parse(glyph + "T00:00:00Z"))) {
        throw new XunError("invalid date", n);
      }
      return new Tagged("d", glyph);
    case "t":
      if (!/^\d{2}:\d{2}(:\d{2}(\.\d+)?)?$/.test(glyph)) throw new XunError("invalid time", n);
      return new Tagged("t", glyph);
    case "dt":
      if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})$/.test(glyph)) {
        throw new XunError("datetime must include a timezone offset", n);
      }
      return new Tagged("dt", glyph);
    case "tz":
      if (glyph !== "Z" && !/^[+-]\d{2}:\d{2}$/.test(glyph) && !/^[A-Za-z_]+(\/[A-Za-z0-9_+-]+)+$/.test(glyph) && glyph !== "UTC") {
        throw new XunError("invalid time zone", n);
      }
      return new Tagged("tz", glyph);
    case "du":
      if (!/^(\d+d)?(\d+h)?(\d+m)?(\d+(\.\d+)?s)?$/.test(glyph) || glyph.length === 0) {
        throw new XunError("invalid duration", n);
      }
      return new Tagged("du", glyph);
    case "sz": {
      const m = glyph.match(/^(\d+(\.\d+)?)(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$/);
      if (!m) throw new XunError("invalid data size", n);
      return new Tagged("sz", glyph);
    }
    case "unix":
      return parseUnix(glyph, n);
    case "ver":
      if (!/^\d+(\.\d+)*$/.test(glyph)) throw new XunError("invalid version", n);
      return new Tagged("ver", glyph);
    case "uuid":
      if (!/^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/.test(glyph)) {
        throw new XunError("invalid uuid", n);
      }
      return new Tagged("uuid", glyph);
    case "ip":
      if (!isIp(glyph)) throw new XunError("invalid ip", n);
      return new Tagged("ip", glyph);
    case "b64":
      return parseB64(glyph, n);
    case "c":
      return parseC(glyph, n);
    default:
      return new Tagged(tag, glyph);
  }
}

function stripUnderscores(s, n) {
  if (s.includes("__") || s.startsWith("_") || s.endsWith("_")) {
    throw new XunError("invalid numeric underscores", n);
  }
  return s.replace(/_/g, "");
}

function parseN(g, n) {
  const s = stripUnderscores(g, n);
  if (/^-?0\d/.test(s)) throw new XunError("leading zeros are not allowed", n);
  if (/^-?\d+$/.test(s)) {
    const v = Number(s);
    if (!Number.isSafeInteger(v)) throw new XunError("integer overflow", n);
    return v;
  }
  if (/^-?\d+\.\d+([eE][+-]?\d+)?$/.test(s) || /^-?\d+[eE][+-]?\d+$/.test(s)) {
    const v = Number(s);
    if (!Number.isFinite(v)) throw new XunError("invalid number", n);
    return v;
  }
  throw new XunError("invalid number", n);
}

function parseI(g, n) {
  const s = stripUnderscores(g, n);
  if (!/^-?\d+$/.test(s)) throw new XunError("invalid integer", n);
  if (/^-?0\d/.test(s)) throw new XunError("leading zeros are not allowed", n);
  const v = Number(s);
  if (!Number.isSafeInteger(v) || v > 9223372036854775807 || v < -9223372036854775808n) {
    /* Number can't hold full i64; check via BigInt */
  }
  const b = BigInt(s);
  if (b > 9223372036854775807n || b < -9223372036854775808n) {
    throw new XunError("integer overflow", n);
  }
  if (b <= BigInt(Number.MAX_SAFE_INTEGER) && b >= BigInt(Number.MIN_SAFE_INTEGER)) {
    return Number(b);
  }
  return b;
}

function parseF(g, n) {
  const s = stripUnderscores(g, n);
  if (!s.includes(".") && !/[eE]/.test(s)) throw new XunError("float must contain '.' or 'e'", n);
  if (/^-?0\d/.test(s.replace(/[eE].*$/, "").replace(/\..*$/, ""))) {
    throw new XunError("leading zeros are not allowed", n);
  }
  const v = Number(s);
  if (!Number.isFinite(v)) throw new XunError("invalid float", n);
  return v;
}

function parseX(g, n) {
  const s = stripUnderscores(g, n);
  if (!/^[0-9A-Fa-f]+$/.test(s)) throw new XunError("invalid hex integer", n);
  const b = BigInt("0x" + s);
  if (b <= BigInt(Number.MAX_SAFE_INTEGER)) return Number(b);
  return b;
}

function parseXb(g, n) {
  const s = g.replace(/_/g, "");
  if (!/^[0-9A-Fa-f]*$/.test(s) || s.length % 2 !== 0 || s.length === 0) {
    throw new XunError("hex bytes must be an even number of digits", n);
  }
  const out = new Uint8Array(s.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(s.slice(i * 2, i * 2 + 2), 16);
  return out;
}

function parseO(g, n) {
  if (!/^[0-7]+$/.test(g)) throw new XunError("invalid octal", n);
  return parseInt(g, 8);
}

function parseUnix(g, n) {
  const s = stripUnderscores(g, n);
  if (/^-?0\d/.test(s)) throw new XunError("leading zeros are not allowed", n);
  if (/^-?\d+(\.\d+)?$/.test(s)) {
    const v = Number(s);
    if (!Number.isFinite(v)) throw new XunError("invalid unix timestamp", n);
    return v;
  }
  throw new XunError("invalid unix timestamp", n);
}

function parseB64(g, n) {
  const s = g.replace(/\s+/g, "");
  if (!/^[A-Za-z0-9+/]*={0,2}$/.test(s) || s.length % 4 !== 0) {
    throw new XunError("invalid base64", n);
  }
  const buf = Buffer.from(s, "base64");
  return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);
}

function parseC(g, n) {
  const u = g.match(/^U\+([0-9A-Fa-f]{4,6})$/);
  if (u) {
    const cp = parseInt(u[1], 16);
    if (cp > 0x10ffff) throw new XunError("invalid code point", n);
    return new Tagged("c", String.fromCodePoint(cp));
  }
  if ([...g].length !== 1) throw new XunError("character must be a single scalar", n);
  return new Tagged("c", g);
}

function isIp(s) {
  if (/^\d{1,3}(\.\d{1,3}){3}$/.test(s)) {
    return s.split(".").every((p) => {
      const n = Number(p);
      return n >= 0 && n <= 255 && String(n) === p;
    });
  }
  if (s.includes(":")) {
    if (s.includes(".")) return false;
    const parts = s.split(":");
    if (parts.length > 8) return false;
    let empty = 0;
    for (const p of parts) {
      if (p === "") empty++;
      else if (!/^[0-9A-Fa-f]{1,4}$/.test(p)) return false;
    }
    return empty <= 2;
  }
  return false;
}
