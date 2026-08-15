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
      throw new XunError("document must start at indent 0", first.n);
    }
    if (this.isListItem(first)) {
      throw new XunError("root must be a dictionary", first.n);
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
        throw new XunError("invalid indent jump", l.n);
      }
      if (this.isListItem(l)) {
        throw new XunError("cannot mix list items into a dictionary", l.n);
      }
      const { key, rest } = splitKey(l.text, l.n);
      if (Object.prototype.hasOwnProperty.call(obj, key)) {
        throw new XunError(`duplicate key '${key}'`, l.n);
      }
      this.i++;
      obj[key] = this.parseValue(rest, indent, l.n, depth + 1);
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
      let val = this.parseValue(rest, indent, l.n, depth + 1);
      if (itemTag) val = applyTag(itemTag, glyphOf(val), l.n);
      arr.push(val);
    }
    return arr;
  }

  isListItem(l) {
    return l.text === "-" || l.text.startsWith("- ");
  }

  parseValue(raw, parentIndent, lineNo, depth) {
    if (raw === "[]") return [];
    if (raw === "{}") return {};

    const ml = matchMultiline(raw);
    if (ml) return this.readMultiline(parentIndent, ml.tag, ml.closer, lineNo);

    if (raw.startswith?.("!") || raw.startsWith("!")) {
      return this.parseTagged(raw, parentIndent, lineNo, depth);
    }

    if (raw === "") {
      return this.parseEmptyOrNested(parentIndent, lineNo, depth, null);
    }

    return raw;
  }

  parseTagged(raw, parentIndent, lineNo, depth) {
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
      const text = this.readMultiline(parentIndent, ml.tag, ml.closer, lineNo);
      if (tag === "s") return text;
      return applyTag(tag, text, lineNo);
    }
    if (tag === "s") return body;
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

  readMultiline(parentIndent, tag, closer, lineNo) {
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
}

function splitKey(text, n) {
  const idx = text.indexOf(": ");
  if (idx > 0) return { key: text.slice(0, idx), rest: text.slice(idx + 2) };
  if (text.endsWith(":") && text.length > 1) {
    return { key: text.slice(0, -1), rest: "" };
  }
  throw new XunError("expected ': ' or trailing ':'", n);
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

function stripUnderscores(s, n) {
  if (s.includes("__") || s.startsWith("_") || s.endsWith("_")) {
    throw new XunError("invalid numeric underscores", n);
  }
  return s.replace(/_/g, "");
}

function applyTag(tag, glyph, n) {
  if (tag === "s") return glyph;
  if (tag === "n") return parseN(glyph, n);
  if (tag === "i") return parseI(glyph, n);
  if (tag === "f") return parseF(glyph, n);
  if (tag === "x") {
    const s = stripUnderscores(glyph, n);
    if (!/^[0-9A-Fa-f]+$/.test(s)) throw new XunError("invalid hex", n);
    return parseInt(s, 16);
  }
  if (tag === "xb") {
    const s = glyph.replace(/_/g, "");
    if (!/^[0-9A-Fa-f]*$/.test(s) || s.length % 2 !== 0 || s.length === 0) {
      throw new XunError("hex bytes must be an even number of digits", n);
    }
    const arr = new Uint8Array(s.length / 2);
    for (let i = 0; i < s.length; i += 2) {
      arr[i / 2] = parseInt(s.slice(i, i + 2), 16);
    }
    return arr;
  }
  if (tag === "o") {
    if (!/^[0-7]+$/.test(glyph)) throw new XunError("invalid octal", n);
    return parseInt(glyph, 8);
  }
  if (tag === "b") {
    if (glyph === "true") return true;
    if (glyph === "false") return false;
    throw new XunError("boolean must be true or false", n);
  }
  if (tag === "d") {
    if (!/^\d{4}-\d{2}-\d{2}$/.test(glyph)) throw new XunError("invalid date", n);
    return new Tagged("d", glyph);
  }
  if (tag === "t") {
    if (!/^\d{2}:\d{2}(:\d{2}(\.\d+)?)?$/.test(glyph)) throw new XunError("invalid time", n);
    return new Tagged("t", glyph);
  }
  if (tag === "dt") {
    if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})$/.test(glyph)) {
      throw new XunError("datetime must include a timezone offset", n);
    }
    return new Tagged("dt", glyph);
  }
  if (tag === "tz") {
    if (glyph !== "Z" && glyph !== "UTC" && !/^[+-]\d{2}:\d{2}$/.test(glyph) && !/^[A-Za-z_]+(\/[A-Za-z0-9_+-]+)+$/.test(glyph)) {
      throw new XunError("invalid time zone", n);
    }
    return new Tagged("tz", glyph);
  }
  if (tag === "du") {
    if (!glyph || !/^(\d+d)?(\d+h)?(\d+m)?(\d+(\.\d+)?s)?$/.test(glyph)) {
      throw new XunError("invalid duration", n);
    }
    return new Tagged("du", glyph);
  }
  if (tag === "sz") {
    if (!/^\d+(\.\d+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$/.test(glyph)) {
      throw new XunError("invalid data size", n);
    }
    return new Tagged("sz", glyph);
  }
  if (tag === "unix") return parseUnix(glyph, n);
  if (tag === "ver") {
    if (!/^\d+(\.\d+)*$/.test(glyph)) throw new XunError("invalid version", n);
    return new Tagged("ver", glyph);
  }
  if (tag === "uuid") {
    if (!/^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/.test(glyph)) {
      throw new XunError("invalid uuid", n);
    }
    return new Tagged("uuid", glyph);
  }
  if (tag === "ip") {
    if (!isIPv4(glyph) && !isIPv6(glyph)) throw new XunError("invalid ip", n);
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
      throw new XunError("invalid base64", n);
    }
  }
  if (tag === "c") {
    const u = glyph.match(/^U\+([0-9A-Fa-f]{4,6})$/);
    if (u) {
      const cp = parseInt(u[1], 16);
      if (cp > 0x10ffff) throw new XunError("invalid code point", n);
      return new Tagged("c", String.fromCodePoint(cp));
    }
    if ([...glyph].length !== 1) throw new XunError("character must be a single scalar", n);
    return new Tagged("c", glyph);
  }
  return new Tagged(tag, glyph);
}

function parseN(g, n) {
  const s = stripUnderscores(g, n);
  if (/^-?0\d/.test(s)) throw new XunError("leading zeros are not allowed", n);
  if (/^-?\d+$/.test(s)) {
    const v = Number(s);
    return v;
  }
  if (/^-?\d+\.\d+([eE][+-]?\d+)?$/.test(s) || /^-?\d+[eE][+-]?\d+$/.test(s)) {
    return Number(s);
  }
  throw new XunError("invalid number", n);
}

function parseI(g, n) {
  const s = stripUnderscores(g, n);
  if (!/^-?\d+$/.test(s)) throw new XunError("invalid integer", n);
  if (/^-?0\d/.test(s)) throw new XunError("leading zeros are not allowed", n);
  return Number(s);
}

function parseF(g, n) {
  const s = stripUnderscores(g, n);
  if (!s.includes(".") && !/[eE]/.test(s)) {
    throw new XunError("float must contain '.' or 'e'", n);
  }
  return Number(s);
}

function parseUnix(g, n) {
  const s = stripUnderscores(g, n);
  if (/^-?0\d/.test(s)) throw new XunError("leading zeros are not allowed", n);
  if (/^-?\d+$/.test(s)) return parseInt(s, 10);
  if (/^-?\d+\.\d+$/.test(s)) return parseFloat(s);
  throw new XunError("invalid unix timestamp", n);
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
    throw new XunError("root must be a dictionary");
  }
  const keys = Object.keys(value);
  if (keys.length === 0) return "";
  const lines = [];
  encodeDictItems(value, 0, lines);
  return lines.join("\n") + "\n";
}

export const stringify = encode;

function validateKey(key) {
  if (typeof key !== "string" || key.length === 0) {
    throw new XunError(`key must be a non-empty string, got: ${key}`);
  }
  if (key.includes("\n") || key.includes("\r") || key.includes(": ") || key.endsWith(":")) {
    throw new XunError(`invalid key format: ${key}`);
  }
  return key;
}

function encodeDictItems(d, depth, out) {
  if (depth > MAX_DEPTH) throw new XunError("nesting depth exceeds limit");
  const indent = "  ".repeat(depth);
  for (const [k, v] of Object.entries(d)) {
    const key = validateKey(k);
    if (v !== null && typeof v === "object" && !Array.isArray(v) && !(v instanceof Uint8Array) && !(v instanceof Tagged)) {
      if (Object.keys(v).length === 0) {
        out.push(`${indent}${key}: {}`);
      } else {
        out.push(`${indent}${key}:`);
        encodeDictItems(v, depth + 1, out);
      }
    } else if (Array.isArray(v)) {
      if (v.length === 0) {
        out.push(`${indent}${key}: []`);
      } else {
        out.push(`${indent}${key}:`);
        encodeListItems(v, depth + 1, out);
      }
    } else if (typeof v === "string") {
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
      throw new XunError(`unsupported value type: ${typeof v} for key '${key}'`);
    }
  }
}

function encodeListItems(items, depth, out) {
  if (depth > MAX_DEPTH) throw new XunError("nesting depth exceeds limit");
  const indent = "  ".repeat(depth);
  for (const v of items) {
    if (v !== null && typeof v === "object" && !Array.isArray(v) && !(v instanceof Uint8Array) && !(v instanceof Tagged)) {
      if (Object.keys(v).length === 0) {
        out.push(`${indent}- {}`);
      } else {
        out.push(`${indent}-`);
        encodeDictItems(v, depth + 1, out);
      }
    } else if (Array.isArray(v)) {
      if (v.length === 0) {
        out.push(`${indent}- []`);
      } else {
        out.push(`${indent}-`);
        encodeListItems(v, depth + 1, out);
      }
    } else if (typeof v === "string") {
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
      throw new XunError(`unsupported list item type: ${typeof v}`);
    }
  }
}
