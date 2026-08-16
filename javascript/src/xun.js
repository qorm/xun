export class XunError extends Error {
  constructor(message, line = 0, column = 0, sourceLine = "") {
    let prefix = "";
    if (line && column) prefix = `[line ${line}: col ${column}] `;
    else if (line) prefix = `[line ${line}] `;
    const detail = sourceLine ? `\n  --> ${sourceLine.trim()}` : "";
    super(`${prefix}${message}${detail}`);
    this.name = "XunError";
    this.line = line;
    this.column = column;
    this.sourceLine = sourceLine;
  }
}

export class Tagged {
  constructor(tag, value) {
    this.tag = tag;
    this.value = value;
  }

  toDate() {
    if (this.tag !== "dt" && this.tag !== "d") {
      throw new XunError(`cannot convert !${this.tag} to Date`);
    }
    return new Date(this.value);
  }

  toBytes() {
    if (this.tag === "xb") {
      const s = this.value.replace(/_/g, "");
      const arr = new Uint8Array(s.length / 2);
      for (let i = 0; i < s.length; i += 2) {
        arr[i / 2] = parseInt(s.slice(i, i + 2), 16);
      }
      return arr;
    }
    if (this.tag === "b64") {
      const s = this.value.replace(/\s+/g, "");
      if (typeof Buffer !== "undefined") {
        return new Uint8Array(Buffer.from(s, "base64"));
      }
      const bin = atob(s);
      const arr = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) arr[i] = bin.charCodeAt(i);
      return arr;
    }
    throw new XunError(`cannot convert !${this.tag} to raw bytes`);
  }

  toBytesSize() {
    if (this.tag !== "sz") {
      throw new XunError(`cannot convert !${this.tag} to size bytes`);
    }
    return parseSize(this.value);
  }

  toDurationSeconds() {
    if (this.tag !== "du") {
      throw new XunError(`cannot convert !${this.tag} to duration seconds`);
    }
    return parseDuration(this.value);
  }

  toVersionParts() {
    if (this.tag !== "ver") {
      throw new XunError(`cannot convert !${this.tag} to version parts`);
    }
    return parseVersion(this.value);
  }

  toUUID() {
    if (this.tag !== "uuid") {
      throw new XunError(`cannot convert !${this.tag} to UUID`);
    }
    if (!/^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/.test(this.value)) {
      throw new XunError(`invalid UUID: ${this.value}`);
    }
    return this.value.toLowerCase();
  }

  toIP() {
    if (this.tag !== "ip") {
      throw new XunError(`cannot convert !${this.tag} to IP address`);
    }
    if (!isIPv4(this.value) && !isIPv6(this.value)) {
      throw new XunError(`invalid IP address: ${this.value}`);
    }
    return this.value;
  }

  toChar() {
    if (this.tag !== "c") {
      throw new XunError(`cannot convert !${this.tag} to char`);
    }
    const m = this.value.match(/^U\+([0-9A-Fa-f]{4,6})$/);
    if (m) {
      const cp = parseInt(m[1], 16);
      if (cp > 0x10ffff) throw new XunError("invalid code point");
      return String.fromCodePoint(cp);
    }
    if ([...this.value].length !== 1) {
      throw new XunError(`value '${this.value}' is not a single character`);
    }
    return this.value;
  }
}

export function parseSize(s) {
  const units = {
    B: 1,
    KB: 1000,
    MB: 1000 ** 2,
    GB: 1000 ** 3,
    TB: 1000 ** 4,
    PB: 1000 ** 5,
    KiB: 1024,
    MiB: 1024 ** 2,
    GiB: 1024 ** 3,
    TiB: 1024 ** 4,
    PiB: 1024 ** 5,
  };
  const m = s.match(/^(\d+(?:\.\d+)?)(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$/);
  if (!m) throw new XunError(`invalid size format: "${s}"`);
  return Math.floor(parseFloat(m[1]) * units[m[2]]);
}

export function parseDuration(s) {
  if (!s) throw new XunError("empty duration string");
  const m = s.match(/^(?:(\d+)d)?(?:(\d+)h)?(?:(\d+)m)?(?:(\d+(?:\.\d+)?)s)?$/);
  if (!m || (!m[1] && !m[2] && !m[3] && !m[4])) {
    throw new XunError(`invalid duration format: "${s}"`);
  }
  const days = parseInt(m[1] || "0", 10);
  const hours = parseInt(m[2] || "0", 10);
  const minutes = parseInt(m[3] || "0", 10);
  const seconds = parseFloat(m[4] || "0");
  return days * 86400 + hours * 3600 + minutes * 60 + seconds;
}

export function parseVersion(s) {
  if (!/^\d+(?:\.\d+)*$/.test(s)) throw new XunError(`invalid version format: "${s}"`);
  return s.split(".").map((n) => parseInt(n, 10));
}

export function unpack(v) {
  if (v instanceof Tagged) {
    if (v.tag === "dt" || v.tag === "d") return v.toDate();
    if (v.tag === "ver") return v.toVersionParts();
    if (v.tag === "sz") return v.toBytesSize();
    if (v.tag === "du") return v.toDurationSeconds();
    if (v.tag === "xb" || v.tag === "b64") return v.toBytes();
    if (v.tag === "ip") return v.toIP();
    if (v.tag === "uuid") return v.toUUID();
    if (v.tag === "c") return v.toChar();
    return v.value;
  }
  if (Array.isArray(v)) {
    return v.map(unpack);
  }
  if (v !== null && typeof v === "object" && !(v instanceof Uint8Array)) {
    const out = {};
    for (const [k, val] of Object.entries(v)) {
      out[k] = unpack(val);
    }
    return out;
  }
  return v;
}

const IDENT = /^[A-Za-z_][A-Za-z0-9_]*$/;
const MAX_BYTES = 1024 * 1024;
const MAX_DEPTH = 64;

export function decode(source) {
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

export const parse = decode;

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
    throw new XunError("tab is not allowed for indentation", n, i + 1, raw);
  }
  if (i % 2 !== 0) {
    throw new XunError(`indent must be a multiple of 2, got ${i} spaces`, n, i + 1, raw);
  }
  const text = raw.slice(i).replace(/[ \t]+$/, "");
  return { raw, indent: i, text, n, blank: text.length === 0 };
}

class Parser {
  constructor(lines) {
    this.lines = lines;
    this.i = 0;
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
    this.skipNoise();
    if (!this.peek()) return {};
    const first = this.peek();
    if (first.indent !== 0) {
      throw new XunError("document must start at indent 0", first.n, 1, first.raw);
    }
    if (this.isListItem(first)) {
      throw new XunError("root must be a dictionary", first.n, 1, first.raw);
    }
    return this.parseDict(0, 0);
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
        throw new XunError(`invalid indent jump from ${indent} to ${l.indent}`, l.n, l.indent + 1, l.raw);
      }
      if (this.isListItem(l)) {
        throw new XunError("cannot mix list items into a dictionary", l.n, 1, l.raw);
      }
      const { key, rest } = splitKey(l.text, l.n, l.raw);
      if (Object.prototype.hasOwnProperty.call(obj, key)) {
        throw new XunError(`duplicate key '${key}'`, l.n, 1, l.raw);
      }
      this.i++;
      obj[key] = this.parseValue(rest, indent, l.n, l.raw, depth + 1);
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
      if (l.indent > indent) throw new XunError(`invalid indent jump from ${indent} to ${l.indent}`, l.n, l.indent + 1, l.raw);
      if (!this.isListItem(l)) {
        throw new XunError("cannot mix dictionary keys into a list", l.n, 1, l.raw);
      }
      const rest = l.text === "-" ? "" : l.text.slice(2);
      this.i++;
      let val = this.parseValue(rest, indent, l.n, l.raw, depth + 1);
      if (itemTag) val = applyTag(itemTag, glyphOf(val), l.n, l.raw);
      arr.push(val);
    }
    return arr;
  }

  isListItem(l) {
    return l.text === "-" || l.text.startsWith("- ");
  }

  parseValue(raw, parentIndent, lineNo, sourceLine, depth) {
    if (raw === "[]") return [];
    if (raw === "{}") return {};

    const ml = matchMultiline(raw);
    if (ml) return this.readMultiline(parentIndent, ml.tag, ml.closer, lineNo, sourceLine);

    if (raw.startsWith("!")) {
      return this.parseTagged(raw, parentIndent, lineNo, sourceLine, depth);
    }

    if (raw === "") {
      return this.parseEmptyOrNested(parentIndent, lineNo, sourceLine, depth, null);
    }

    return raw;
  }

  parseTagged(raw, parentIndent, lineNo, sourceLine, depth) {
    const m = raw.match(/^!([A-Za-z_][A-Za-z0-9_]*)(.*)$/);
    if (!m) throw new XunError("invalid type tag", lineNo, 1, sourceLine);
    const tag = m[1];
    const rest = m[2];

    if (rest.startsWith("[")) {
      if (tag === "s" && rest !== "[]") {
        throw new XunError("string arrays cannot use compact form", lineNo, 1, sourceLine);
      }
      if (!rest.endsWith("]")) throw new XunError("unclosed compact array", lineNo, 1, sourceLine);
      const inner = rest.slice(1, -1);
      if (inner.length === 0) {
        return this.parseEmptyOrNested(parentIndent, lineNo, sourceLine, depth, tag);
      }
      return splitCompact(inner).map((g) => applyTag(tag, g, lineNo, sourceLine));
    }

    if (rest === "") {
      throw new XunError(`missing value for !${tag}`, lineNo, 1, sourceLine);
    }
    if (!rest.startsWith(" ")) {
      throw new XunError("expected space after type tag", lineNo, 1, sourceLine);
    }
    const body = rest.slice(1);
    const ml = matchMultiline(body);
    if (ml) {
      const text = this.readMultiline(parentIndent, ml.tag, ml.closer, lineNo, sourceLine);
      if (tag === "s") return text;
      return applyTag(tag, text, lineNo, sourceLine);
    }
    if (tag === "s") return body;
    return applyTag(tag, body, lineNo, sourceLine);
  }

  parseEmptyOrNested(parentIndent, lineNo, sourceLine, depth, itemTag) {
    this.skipNoise();
    const n = this.peek();
    const child = parentIndent + 2;
    if (!n || n.blank || n.indent <= parentIndent) {
      if (itemTag) return [];
      return "";
    }
    if (n.indent !== child) throw new XunError(`child indent must be parent + 2 (${child}), got ${n.indent}`, n.n, 1, n.raw);
    if (this.isListItem(n)) return this.parseList(child, depth, itemTag);
    if (itemTag) throw new XunError(`!${itemTag}[] expected list items`, n.n, 1, n.raw);
    return this.parseDict(child, depth);
  }

  readMultiline(parentIndent, tag, closer, lineNo, sourceLine) {
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
        if (tag && tag !== "s") return applyTag(tag, s, lineNo, sourceLine);
        return s;
      }
      if (l.blank) {
        parts.push("");
        this.i++;
        continue;
      }
      if (ind < base && !l.blank) {
        throw new XunError("multiline body must indent +2, or close at opener indent", l.n, 1, l.raw);
      }
      if (l.raw.includes("\t")) throw new XunError("tab is not allowed in multiline body", l.n, 1, l.raw);
      parts.push(l.raw.slice(base));
      this.i++;
    }
    throw new XunError(`unclosed multiline block (expected '${closer}' at indent ${parentIndent})`, lineNo, 1, sourceLine);
  }
}

function splitKey(text, n, sourceLine) {
  const idx = text.indexOf(": ");
  if (idx > 0) return { key: text.slice(0, idx), rest: text.slice(idx + 2) };
  if (text.endsWith(":") && text.length > 1) {
    return { key: text.slice(0, -1), rest: "" };
  }
  throw new XunError("expected ': ' or trailing ':'", n, 1, sourceLine);
}

function matchMultiline(raw) {
  if (raw === "|") return { tag: null, closer: "|" };
  const m = raw.match(/^\|([A-Za-z_][A-Za-z0-9_]*)$/);
  if (m) return { tag: null, closer: m[1] };
  return null;
}

function glyphOf(v) {
  if (v instanceof Tagged) return v.value;
  if (v instanceof Uint8Array) {
    return Array.from(v, (b) => b.toString(16).padStart(2, "0")).join("");
  }
  if (typeof v === "boolean") return v ? "true" : "false";
  if (typeof v === "string" || typeof v === "number") return String(v);
  throw new XunError("cannot stringify a collection as scalar glyph");
}

function splitCompact(inner) {
  return inner.split(",").map((s) => s.trim());
}

function stripUnderscores(s, n, sourceLine) {
  if (s.includes("__") || s.startsWith("_") || s.endsWith("_")) {
    throw new XunError("invalid numeric underscores", n, 1, sourceLine);
  }
  return s.replace(/_/g, "");
}

function applyTag(tag, glyph, n, sourceLine = "") {
  if (tag === "s") return glyph;
  if (tag === "n") return parseN(glyph, n, sourceLine);
  if (tag === "i") return parseI(glyph, n, sourceLine);
  if (tag === "f") return parseF(glyph, n, sourceLine);
  if (tag === "x") {
    const s = stripUnderscores(glyph, n, sourceLine);
    if (!/^[0-9A-Fa-f]+$/.test(s)) throw new XunError("invalid hex", n, 1, sourceLine);
    return parseInt(s, 16);
  }
  if (tag === "xb") {
    const s = glyph.replace(/_/g, "");
    if (!/^[0-9A-Fa-f]*$/.test(s) || s.length % 2 !== 0 || s.length === 0) {
      throw new XunError("hex bytes must be an even number of digits", n, 1, sourceLine);
    }
    const arr = new Uint8Array(s.length / 2);
    for (let i = 0; i < s.length; i += 2) {
      arr[i / 2] = parseInt(s.slice(i, i + 2), 16);
    }
    return arr;
  }
  if (tag === "o") {
    if (!/^[0-7]+$/.test(glyph)) throw new XunError("invalid octal", n, 1, sourceLine);
    return parseInt(glyph, 8);
  }
  if (tag === "b") {
    if (glyph === "true") return true;
    if (glyph === "false") return false;
    throw new XunError("boolean must be true or false", n, 1, sourceLine);
  }
  if (tag === "d") {
    if (!/^\d{4}-\d{2}-\d{2}$/.test(glyph)) throw new XunError("invalid date", n, 1, sourceLine);
    return new Tagged("d", glyph);
  }
  if (tag === "t") {
    if (!/^\d{2}:\d{2}(:\d{2}(\.\d+)?)?$/.test(glyph)) throw new XunError("invalid time", n, 1, sourceLine);
    return new Tagged("t", glyph);
  }
  if (tag === "dt") {
    if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})$/.test(glyph)) {
      throw new XunError("datetime must include a timezone offset", n, 1, sourceLine);
    }
    return new Tagged("dt", glyph);
  }
  if (tag === "tz") {
    if (glyph !== "Z" && glyph !== "UTC" && !/^[+-]\d{2}:\d{2}$/.test(glyph) && !/^[A-Za-z_]+(\/[A-Za-z0-9_+-]+)+$/.test(glyph)) {
      throw new XunError("invalid time zone", n, 1, sourceLine);
    }
    return new Tagged("tz", glyph);
  }
  if (tag === "du") {
    if (!glyph || !/^(\d+d)?(\d+h)?(\d+m)?(\d+(\.\d+)?s)?$/.test(glyph)) {
      throw new XunError("invalid duration", n, 1, sourceLine);
    }
    return new Tagged("du", glyph);
  }
  if (tag === "sz") {
    if (!/^\d+(\.\d+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$/.test(glyph)) {
      throw new XunError("invalid data size", n, 1, sourceLine);
    }
    return new Tagged("sz", glyph);
  }
  if (tag === "unix") return parseUnix(glyph, n, sourceLine);
  if (tag === "ver") {
    if (!/^\d+(\.\d+)*$/.test(glyph)) throw new XunError("invalid version", n, 1, sourceLine);
    return new Tagged("ver", glyph);
  }
  if (tag === "uuid") {
    if (!/^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/.test(glyph)) {
      throw new XunError("invalid uuid", n, 1, sourceLine);
    }
    return new Tagged("uuid", glyph);
  }
  if (tag === "ip") {
    if (!isIPv4(glyph) && !isIPv6(glyph)) throw new XunError("invalid ip", n, 1, sourceLine);
    return new Tagged("ip", glyph);
  }
  if (tag === "b64") {
    const s = glyph.replace(/\s+/g, "");
    try {
      if (typeof Buffer !== "undefined") {
        return new Uint8Array(Buffer.from(s, "base64"));
      }
      const bin = atob(s);
      const arr = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) arr[i] = bin.charCodeAt(i);
      return arr;
    } catch {
      throw new XunError("invalid base64", n, 1, sourceLine);
    }
  }
  if (tag === "c") {
    const u = glyph.match(/^U\+([0-9A-Fa-f]{4,6})$/);
    if (u) {
      const cp = parseInt(u[1], 16);
      if (cp > 0x10ffff) throw new XunError("invalid code point", n, 1, sourceLine);
      return new Tagged("c", String.fromCodePoint(cp));
    }
    if ([...glyph].length !== 1) throw new XunError("character must be a single scalar", n, 1, sourceLine);
    return new Tagged("c", glyph);
  }
  return new Tagged(tag, glyph);
}

function parseN(g, n, sourceLine) {
  const s = stripUnderscores(g, n, sourceLine);
  if (/^-?0\d/.test(s)) throw new XunError("leading zeros are not allowed", n, 1, sourceLine);
  if (/^-?\d+$/.test(s)) {
    const v = Number(s);
    return v;
  }
  if (/^-?\d+\.\d+([eE][+-]?\d+)?$/.test(s) || /^-?\d+[eE][+-]?\d+$/.test(s)) {
    return Number(s);
  }
  throw new XunError("invalid number", n, 1, sourceLine);
}

function parseI(g, n, sourceLine) {
  const s = stripUnderscores(g, n, sourceLine);
  if (!/^-?\d+$/.test(s)) throw new XunError("invalid integer", n, 1, sourceLine);
  if (/^-?0\d/.test(s)) throw new XunError("leading zeros are not allowed", n, 1, sourceLine);
  return Number(s);
}

function parseF(g, n, sourceLine) {
  const s = stripUnderscores(g, n, sourceLine);
  if (!s.includes(".") && !/[eE]/.test(s)) {
    throw new XunError("float must contain '.' or 'e'", n, 1, sourceLine);
  }
  return Number(s);
}

function parseUnix(g, n, sourceLine) {
  const s = stripUnderscores(g, n, sourceLine);
  if (/^-?0\d/.test(s)) throw new XunError("leading zeros are not allowed", n, 1, sourceLine);
  if (/^-?\d+$/.test(s)) return parseInt(s, 10);
  if (/^-?\d+\.\d+$/.test(s)) return parseFloat(s);
  throw new XunError("invalid unix timestamp", n, 1, sourceLine);
}

function isIPv4(s) {
  const parts = s.split(".");
  if (parts.length !== 4) return false;
  return parts.every((p) => {
    if (!/^\d+$/.test(p)) return false;
    const n = Number(p);
    return n >= 0 && n <= 255 && String(n) === p;
  });
}

function isIPv6(s) {
  return s.includes(":") && !s.includes(":::") && /^[0-9a-fA-F:]+$/.test(s);
}

// --- Encoder ---

export function encode(value) {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new XunError(`root must be a dictionary, got: ${typeof value}`);
  }
  const keys = Object.keys(value);
  if (keys.length === 0) return "";
  const lines = [];
  const seen = new Set();
  encodeDictItems(value, 0, lines, seen, "root");
  return lines.join("\n") + "\n";
}

export const stringify = encode;

function validateKey(key, path) {
  if (typeof key !== "string" || key.length === 0) {
    throw new XunError(`key at path '${path}' must be a non-empty string, got: ${key}`);
  }
  if (key.includes("\n") || key.includes("\r") || key.includes(": ") || key.endsWith(":")) {
    throw new XunError(`invalid key format '${key}' at path '${path}'`);
  }
  return key;
}

function encodeDictItems(d, depth, out, seen, path) {
  if (depth > MAX_DEPTH) throw new XunError(`nesting depth exceeds limit at path '${path}'`);
  if (seen.has(d)) throw new XunError(`circular reference detected at path '${path}'`);
  seen.add(d);
  try {
    const indent = "  ".repeat(depth);
    for (const [k, v] of Object.entries(d)) {
      const currentPath = `${path}.${k}`;
      const key = validateKey(k, currentPath);
      if (v !== null && typeof v === "object" && !Array.isArray(v) && !(v instanceof Uint8Array) && !(v instanceof Tagged) && !(v instanceof Date)) {
        if (Object.keys(v).length === 0) {
          out.push(`${indent}${key}: {}`);
        } else {
          out.push(`${indent}${key}:`);
          encodeDictItems(v, depth + 1, out, seen, currentPath);
        }
      } else if (Array.isArray(v)) {
        if (v.length === 0) {
          out.push(`${indent}${key}: []`);
        } else {
          out.push(`${indent}${key}:`);
          encodeListItems(v, depth + 1, out, seen, currentPath);
        }
      } else {
        encodeScalarField(indent, key, v, out, currentPath);
      }
    }
  } finally {
    seen.delete(d);
  }
}

function encodeListItems(items, depth, out, seen, path) {
  if (depth > MAX_DEPTH) throw new XunError(`nesting depth exceeds limit at path '${path}'`);
  if (seen.has(items)) throw new XunError(`circular reference detected at path '${path}'`);
  seen.add(items);
  try {
    const indent = "  ".repeat(depth);
    for (let idx = 0; idx < items.length; idx++) {
      const v = items[idx];
      const currentPath = `${path}[${idx}]`;
      if (v !== null && typeof v === "object" && !Array.isArray(v) && !(v instanceof Uint8Array) && !(v instanceof Tagged) && !(v instanceof Date)) {
        if (Object.keys(v).length === 0) {
          out.push(`${indent}- {}`);
        } else {
          out.push(`${indent}-`);
          encodeDictItems(v, depth + 1, out, seen, currentPath);
        }
      } else if (Array.isArray(v)) {
        if (v.length === 0) {
          out.push(`${indent}- []`);
        } else {
          out.push(`${indent}-`);
          encodeListItems(v, depth + 1, out, seen, currentPath);
        }
      } else {
        encodeScalarListItem(indent, v, out, currentPath);
      }
    }
  } finally {
    seen.delete(items);
  }
}

function stripSurroundingQuotes(s) {
  let out = s;
  while (out.length >= 2 && out.startsWith('"') && out.endsWith('"')) {
    out = out.slice(1, -1);
  }
  return out;
}

function encodeScalarField(indent, key, v, out, path) {
  if (v === null || v === undefined) {
    out.push(`${indent}${key}:`);
  } else if (typeof v === "string") {
    v = stripSurroundingQuotes(v);
    if (v.includes("\n") || v.includes("\r")) {
      out.push(`${indent}${key}: |`);
      for (const line of v.split(/\r?\n/)) {
        out.push(`${indent}  ${line}`);
      }
      out.push(`${indent}|`);
    } else if (v === "") {
      out.push(`${indent}${key}:`);
    } else {
      if (v.startsWith("!") || v === "[]" || v === "{}" || v.startsWith("|")) {
        out.push(`${indent}${key}: !s ${v}`);
      } else {
        out.push(`${indent}${key}: ${v}`);
      }
    }
  } else if (typeof v === "boolean") {
    out.push(`${indent}${key}: !b ${v ? "true" : "false"}`);
  } else if (typeof v === "number" || typeof v === "bigint") {
    if (typeof v === "number" && !Number.isInteger(v)) {
      let s = String(v);
      if (!s.includes(".") && !/[eE]/.test(s)) s += ".0";
      out.push(`${indent}${key}: !f ${s}`);
    } else {
      out.push(`${indent}${key}: !i ${v}`);
    }
  } else if (v instanceof Date) {
    out.push(`${indent}${key}: !dt ${v.toISOString()}`);
  } else if (v instanceof Uint8Array) {
    const hex = Array.from(v, (b) => b.toString(16).padStart(2, "0")).join("").toUpperCase();
    out.push(`${indent}${key}: !xb ${hex}`);
  } else if (v instanceof Tagged) {
    if (v.value.includes("\n") || v.value.includes("\r")) {
      out.push(`${indent}${key}: !${v.tag} |`);
      for (const line of v.value.split(/\r?\n/)) {
        out.push(`${indent}  ${line}`);
      }
      out.push(`${indent}|`);
    } else {
      out.push(`${indent}${key}: !${v.tag} ${v.value}`);
    }
  } else {
    throw new XunError(`unsupported value type '${typeof v}' at path '${path}'`);
  }
}

function encodeScalarListItem(indent, v, out, path) {
  if (v === null || v === undefined) {
    out.push(`${indent}-`);
  } else if (typeof v === "string") {
    v = stripSurroundingQuotes(v);
    if (v.includes("\n") || v.includes("\r")) {
      out.push(`${indent}- |`);
      for (const line of v.split(/\r?\n/)) {
        out.push(`${indent}  ${line}`);
      }
      out.push(`${indent}|`);
    } else if (v === "") {
      out.push(`${indent}-`);
    } else {
      if (v.startsWith("!") || v === "[]" || v === "{}" || v.startsWith("|")) {
        out.push(`${indent}- !s ${v}`);
      } else {
        out.push(`${indent}- ${v}`);
      }
    }
  } else if (typeof v === "boolean") {
    out.push(`${indent}- !b ${v ? "true" : "false"}`);
  } else if (typeof v === "number" || typeof v === "bigint") {
    if (typeof v === "number" && !Number.isInteger(v)) {
      let s = String(v);
      if (!s.includes(".") && !/[eE]/.test(s)) s += ".0";
      out.push(`${indent}- !f ${s}`);
    } else {
      out.push(`${indent}- !i ${v}`);
    }
  } else if (v instanceof Date) {
    out.push(`${indent}- !dt ${v.toISOString()}`);
  } else if (v instanceof Uint8Array) {
    const hex = Array.from(v, (b) => b.toString(16).padStart(2, "0")).join("").toUpperCase();
    out.push(`${indent}- !xb ${hex}`);
  } else if (v instanceof Tagged) {
    if (v.value.includes("\n") || v.value.includes("\r")) {
      out.push(`${indent}- !${v.tag} |`);
      for (const line of v.value.split(/\r?\n/)) {
        out.push(`${indent}  ${line}`);
      }
      out.push(`${indent}|`);
    } else {
      out.push(`${indent}- !${v.tag} ${v.value}`);
    }
  } else {
    throw new XunError(`unsupported list item type '${typeof v}' at path '${path}'`);
  }
}
