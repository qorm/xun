from __future__ import annotations

import base64
import ipaddress
import re
from dataclasses import dataclass
from typing import Any, TextIO

__all__ = ["parse", "encode", "dump", "dumps", "Tagged", "XunError"]

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
MAX_BYTES = 1024 * 1024
MAX_DEPTH = 64


class XunError(ValueError):
    def __init__(self, message: str, line: int = 0) -> None:
        super().__init__(f"line {line}: {message}" if line else message)
        self.line = line


@dataclass(frozen=True)
class Tagged:
    tag: str
    value: str


def parse(source: str) -> Any:
    if not isinstance(source, str):
        raise XunError("source must be a string")
    if len(source.encode("utf-8")) > MAX_BYTES:
        raise XunError("document exceeds 1MB")
    if "\x00" in source:
        raise XunError("NUL is not allowed")
    if source.startswith("\ufeff"):
        source = source[1:]
    return Parser(_split_lines(source)).parse_document()


@dataclass
class Line:
    raw: str
    indent: int
    text: str
    n: int
    blank: bool


def _split_lines(source: str) -> list[Line]:
    if source == "":
        return []
    parts: list[Line] = []
    start = 0
    n = 1
    i = 0
    while i <= len(source):
        at_end = i == len(source)
        ch = source[i] if not at_end else ""
        if not at_end and ch not in "\n\r":
            i += 1
            continue
        raw = source[start:i]
        if ch == "\r" and i + 1 < len(source) and source[i + 1] == "\n":
            i += 1
        parts.append(_make_line(raw, n))
        n += 1
        i += 1
        start = i
    return parts


def _make_line(raw: str, n: int) -> Line:
    i = 0
    while i < len(raw) and raw[i] == " ":
        i += 1
    if i < len(raw) and raw[i] == "\t":
        raise XunError("tab is not allowed", n)
    if i % 2 != 0:
        raise XunError("indent must be a multiple of 2", n)
    text = raw[i:].rstrip(" \t")
    return Line(raw, i, text, n, len(text) == 0)


class Parser:
    def __init__(self, lines: list[Line]) -> None:
        self.lines = lines
        self.i = 0

    def peek(self) -> Line | None:
        return self.lines[self.i] if self.i < len(self.lines) else None

    def skip_noise(self) -> None:
        while self.peek():
            l = self.peek()
            assert l is not None
            if l.blank or l.text.startswith("#"):
                self.i += 1
            else:
                break

    def parse_document(self) -> Any:
        self.skip_noise()
        if not self.peek():
            return {}
        first = self.peek()
        assert first is not None
        if first.indent != 0:
            raise XunError("document must start at indent 0", first.n)
        if self.is_list_item(first):
            raise XunError("root must be a dictionary", first.n)
        return self.parse_dict(0, 0)

    def parse_dict(self, indent: int, depth: int) -> dict[str, Any]:
        if depth > MAX_DEPTH:
            raise XunError("nesting exceeds 64", self.peek().n if self.peek() else 0)
        obj: dict[str, Any] = {}
        while self.peek():
            self.skip_noise()
            l = self.peek()
            if not l or l.blank:
                break
            if l.indent < indent:
                break
            if l.indent > indent:
                raise XunError("invalid indent jump", l.n)
            if self.is_list_item(l):
                raise XunError("cannot mix list items into a dictionary", l.n)
            key, rest = _split_key(l.text, l.n)
            if key in obj:
                raise XunError(f"duplicate key '{key}'", l.n)
            self.i += 1
            obj[key] = self.parse_value(rest, indent, l.n, depth + 1)
        return obj

    def parse_list(self, indent: int, depth: int, item_tag: str | None = None) -> list[Any]:
        if depth > MAX_DEPTH:
            raise XunError("nesting exceeds 64", self.peek().n if self.peek() else 0)
        arr: list[Any] = []
        while self.peek():
            self.skip_noise()
            l = self.peek()
            if not l or l.blank:
                break
            if l.indent < indent:
                break
            if l.indent > indent:
                raise XunError("invalid indent jump", l.n)
            if not self.is_list_item(l):
                raise XunError("cannot mix dictionary keys into a list", l.n)
            rest = "" if l.text == "-" else l.text[2:]
            self.i += 1
            val = self.parse_value(rest, indent, l.n, depth + 1)
            if item_tag:
                val = apply_tag(item_tag, glyph_of(val), l.n)
            arr.append(val)
        return arr

    def is_list_item(self, l: Line) -> bool:
        return l.text == "-" or l.text.startswith("- ")

    def parse_value(self, raw: str, parent_indent: int, line_no: int, depth: int) -> Any:
        if raw == "[]":
            return []
        if raw == "{}":
            return {}
        ml = _match_multiline(raw)
        if ml:
            return self.read_multiline(parent_indent, ml[0], ml[1], line_no)
        if raw.startswith("!"):
            return self.parse_tagged(raw, parent_indent, line_no, depth)
        if raw == "":
            return self.parse_empty_or_nested(parent_indent, line_no, depth, None)
        return raw

    def parse_tagged(self, raw: str, parent_indent: int, line_no: int, depth: int) -> Any:
        m = re.match(r"^!([A-Za-z_][A-Za-z0-9_]*)(.*)$", raw)
        if not m:
            raise XunError("invalid type tag", line_no)
        tag, rest = m.group(1), m.group(2)
        if rest.startswith("["):
            if tag == "s" and rest != "[]":
                raise XunError("string arrays cannot use compact form", line_no)
            if not rest.endswith("]"):
                raise XunError("unclosed compact array", line_no)
            inner = rest[1:-1]
            if inner == "":
                return self.parse_empty_or_nested(parent_indent, line_no, depth, tag)
            return [apply_tag(tag, g, line_no) for g in _split_compact(inner)]
        if rest == "":
            raise XunError(f"missing value for !{tag}", line_no)
        if not rest.startswith(" "):
            raise XunError("expected space after type tag", line_no)
        body = rest[1:]
        ml = _match_multiline(body)
        if ml:
            text = self.read_multiline(parent_indent, ml[0], ml[1], line_no)
            return text if tag == "s" else apply_tag(tag, text, line_no)
        if tag == "s":
            return body
        return apply_tag(tag, body, line_no)

    def parse_empty_or_nested(
        self, parent_indent: int, line_no: int, depth: int, item_tag: str | None
    ) -> Any:
        self.skip_noise()
        n = self.peek()
        child = parent_indent + 2
        if not n or n.blank or n.indent <= parent_indent:
            return [] if item_tag else ""
        if n.indent != child:
            raise XunError("child indent must be parent + 2", n.n)
        if self.is_list_item(n):
            return self.parse_list(child, depth, item_tag)
        if item_tag:
            raise XunError(f"!{item_tag}[] expected list items", n.n)
        return self.parse_dict(child, depth)

    def read_multiline(self, parent_indent: int, tag: str | None, closer: str, line_no: int) -> Any:
        base = parent_indent + 2
        parts: list[str] = []
        while self.peek():
            l = self.peek()
            assert l is not None
            stripped = l.raw.rstrip(" \t")
            content = stripped.lstrip(" ")
            ind = len(l.raw) - len(l.raw.lstrip(" "))
            if not l.blank and ind == parent_indent and content == closer:
                self.i += 1
                s = "\n".join(parts)
                if tag and tag != "s":
                    return apply_tag(tag, s, line_no)
                return s
            if l.blank:
                parts.append("")
                self.i += 1
                continue
            if ind < base and not l.blank:
                raise XunError("multiline body must indent +2, or close at opener indent", l.n)
            if "\t" in l.raw:
                raise XunError("tab is not allowed", l.n)
            parts.append(l.raw[base:])
            self.i += 1
        raise XunError("unclosed multiline block", line_no)


def _split_key(text: str, n: int) -> tuple[str, str]:
    idx = text.find(": ")
    if idx > 0:
        return text[:idx], text[idx + 2 :]
    if text.endswith(":") and len(text) > 1:
        return text[:-1], ""
    raise XunError("expected ': ' or trailing ':'", n)


def _match_multiline(raw: str) -> tuple[str | None, str] | None:
    if raw == "|":
        return None, "|"
    m = re.match(r"^\|([A-Za-z_][A-Za-z0-9_]*)$", raw)
    if m:
        return None, m.group(1)
    return None


def glyph_of(v: Any) -> str:
    if isinstance(v, Tagged):
        return v.value
    if isinstance(v, (bytes, bytearray)):
        return v.hex()
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (str, int, float)):
        return str(v)
    raise XunError("cannot stringify a collection as scalar glyph")


def _split_compact(inner: str) -> list[str]:
    return [s.strip() for s in inner.split(",")]


def _strip_underscores(s: str, n: int) -> str:
    if "__" in s or s.startswith("_") or s.endswith("_"):
        raise XunError("invalid numeric underscores", n)
    return s.replace("_", "")


def apply_tag(tag: str, glyph: str, n: int) -> Any:
    if tag == "s":
        return glyph
    if tag == "n":
        return _parse_n(glyph, n)
    if tag == "i":
        return _parse_i(glyph, n)
    if tag == "f":
        return _parse_f(glyph, n)
    if tag == "x":
        return int(_strip_underscores(glyph, n), 16)
    if tag == "xb":
        s = glyph.replace("_", "")
        if not re.fullmatch(r"[0-9A-Fa-f]*", s) or len(s) % 2 or not s:
            raise XunError("hex bytes must be an even number of digits", n)
        return bytes.fromhex(s)
    if tag == "o":
        if not re.fullmatch(r"[0-7]+", glyph):
            raise XunError("invalid octal", n)
        return int(glyph, 8)
    if tag == "b":
        if glyph == "true":
            return True
        if glyph == "false":
            return False
        raise XunError("boolean must be true or false", n)
    if tag == "d":
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", glyph):
            raise XunError("invalid date", n)
        return Tagged("d", glyph)
    if tag == "t":
        if not re.fullmatch(r"\d{2}:\d{2}(:\d{2}(\.\d+)?)?", glyph):
            raise XunError("invalid time", n)
        return Tagged("t", glyph)
    if tag == "dt":
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})", glyph):
            raise XunError("datetime must include a timezone offset", n)
        return Tagged("dt", glyph)
    if tag == "tz":
        if glyph not in {"Z", "UTC"} and not re.fullmatch(r"[+-]\d{2}:\d{2}", glyph) and not re.fullmatch(r"[A-Za-z_]+(/[A-Za-z0-9_+-]+)+", glyph):
            raise XunError("invalid time zone", n)
        return Tagged("tz", glyph)
    if tag == "du":
        if not glyph or not re.fullmatch(r"(\d+d)?(\d+h)?(\d+m)?(\d+(\.\d+)?s)?", glyph):
            raise XunError("invalid duration", n)
        return Tagged("du", glyph)
    if tag == "sz":
        if not re.fullmatch(r"\d+(\.\d+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)", glyph):
            raise XunError("invalid data size", n)
        return Tagged("sz", glyph)
    if tag == "unix":
        return _parse_unix(glyph, n)
    if tag == "ver":
        if not re.fullmatch(r"\d+(\.\d+)*", glyph):
            raise XunError("invalid version", n)
        return Tagged("ver", glyph)
    if tag == "uuid":
        if not re.fullmatch(r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}", glyph):
            raise XunError("invalid uuid", n)
        return Tagged("uuid", glyph)
    if tag == "ip":
        try:
            ipaddress.ip_address(glyph)
        except ValueError as e:
            raise XunError("invalid ip", n) from e
        return Tagged("ip", glyph)
    if tag == "b64":
        s = re.sub(r"\s+", "", glyph)
        try:
            return base64.b64decode(s, validate=True)
        except Exception as e:
            raise XunError("invalid base64", n) from e
    if tag == "c":
        u = re.fullmatch(r"U\+([0-9A-Fa-f]{4,6})", glyph)
        if u:
            cp = int(u.group(1), 16)
            if cp > 0x10FFFF:
                raise XunError("invalid code point", n)
            return Tagged("c", chr(cp))
        if len(glyph) != 1:
            raise XunError("character must be a single scalar", n)
        return Tagged("c", glyph)
    return Tagged(tag, glyph)


def _parse_n(g: str, n: int) -> int | float:
    s = _strip_underscores(g, n)
    if re.match(r"^-?0\d", s):
        raise XunError("leading zeros are not allowed", n)
    if re.fullmatch(r"-?\d+", s):
        v = int(s)
        if v > 2**63 - 1 or v < -(2**63):
            raise XunError("integer overflow", n)
        return v
    if re.fullmatch(r"-?\d+\.\d+([eE][+-]?\d+)?", s) or re.fullmatch(r"-?\d+[eE][+-]?\d+", s):
        return float(s)
    raise XunError("invalid number", n)


def _parse_i(g: str, n: int) -> int:
    s = _strip_underscores(g, n)
    if not re.fullmatch(r"-?\d+", s):
        raise XunError("invalid integer", n)
    if re.match(r"^-?0\d", s):
        raise XunError("leading zeros are not allowed", n)
    v = int(s)
    if v > 2**63 - 1 or v < -(2**63):
        raise XunError("integer overflow", n)
    return v


def _parse_f(g: str, n: int) -> float:
    s = _strip_underscores(g, n)
    if "." not in s and not re.search(r"[eE]", s):
        raise XunError("float must contain '.' or 'e'", n)
    return float(s)


def _parse_unix(g: str, n: int) -> int | float:
    s = _strip_underscores(g, n)
    if re.match(r"^-?0\d", s):
        raise XunError("leading zeros are not allowed", n)
    if re.fullmatch(r"-?\d+", s):
        return int(s)
    if re.fullmatch(r"-?\d+\.\d+", s):
        return float(s)
    raise XunError("invalid unix timestamp", n)


# --- Encoder ---

def encode(value: Any) -> str:
    if not isinstance(value, dict):
        raise XunError("root must be a dictionary")
    if not value:
        return ""
    lines: list[str] = []
    _encode_dict_items(value, 0, lines)
    return "\n".join(lines) + "\n"


def dumps(value: Any) -> str:
    return encode(value)


def dump(value: Any, fp: TextIO) -> None:
    fp.write(encode(value))


def _validate_key(key: Any) -> str:
    if not isinstance(key, str) or not key:
        raise XunError(f"key must be a non-empty string, got: {key!r}")
    if "\n" in key or "\r" in key or ": " in key or key.endswith(":"):
        raise XunError(f"invalid key format: {key!r}")
    return key


def _encode_dict_items(d: dict[str, Any], depth: int, out: list[str]) -> None:
    if depth > MAX_DEPTH:
        raise XunError("nesting depth exceeds limit")
    indent = "  " * depth
    for k, v in d.items():
        key = _validate_key(k)
        if isinstance(v, dict):
            if not v:
                out.append(f"{indent}{key}: {{}}")
            else:
                out.append(f"{indent}{key}:")
                _encode_dict_items(v, depth + 1, out)
        elif isinstance(v, (list, tuple)):
            if not v:
                out.append(f"{indent}{key}: []")
            else:
                out.append(f"{indent}{key}:")
                _encode_list_items(v, depth + 1, out)
        elif isinstance(v, str):
            if "\n" in v or "\r" in v:
                out.append(f"{indent}{key}: |")
                for line in v.splitlines():
                    out.append(f"{indent}  {line}")
                out.append(f"{indent}|")
            elif v == "":
                out.append(f"{indent}{key}:")
            else:
                if v.startswith("!") or v in ("[]", "{}", "|") or v.startswith("|"):
                    out.append(f"{indent}{key}: !s {v}")
                else:
                    out.append(f"{indent}{key}: {v}")
        elif isinstance(v, bool):
            out.append(f"{indent}{key}: !b {'true' if v else 'false'}")
        elif isinstance(v, int):
            out.append(f"{indent}{key}: !i {v}")
        elif isinstance(v, float):
            s = str(v)
            if "." not in s and "e" not in s and "E" not in s:
                s += ".0"
            out.append(f"{indent}{key}: !f {s}")
        elif isinstance(v, (bytes, bytearray)):
            out.append(f"{indent}{key}: !xb {v.hex().upper()}")
        elif isinstance(v, Tagged):
            if "\n" in v.value or "\r" in v.value:
                out.append(f"{indent}{key}: !{v.tag} |")
                for line in v.value.splitlines():
                    out.append(f"{indent}  {line}")
                out.append(f"{indent}|")
            else:
                out.append(f"{indent}{key}: !{v.tag} {v.value}")
        else:
            raise XunError(f"unsupported value type: {type(v).__name__} for key '{key}'")


def _encode_list_items(items: list[Any] | tuple[Any, ...], depth: int, out: list[str]) -> None:
    if depth > MAX_DEPTH:
        raise XunError("nesting depth exceeds limit")
    indent = "  " * depth
    for v in items:
        if isinstance(v, dict):
            if not v:
                out.append(f"{indent}- {{}}")
            else:
                out.append(f"{indent}-")
                _encode_dict_items(v, depth + 1, out)
        elif isinstance(v, (list, tuple)):
            if not v:
                out.append(f"{indent}- []")
            else:
                out.append(f"{indent}-")
                _encode_list_items(v, depth + 1, out)
        elif isinstance(v, str):
            if "\n" in v or "\r" in v:
                out.append(f"{indent}- |")
                for line in v.splitlines():
                    out.append(f"{indent}  {line}")
                out.append(f"{indent}|")
            elif v == "":
                out.append(f"{indent}-")
            else:
                if v.startswith("!") or v in ("[]", "{}", "|") or v.startswith("|"):
                    out.append(f"{indent}- !s {v}")
                else:
                    out.append(f"{indent}- {v}")
        elif isinstance(v, bool):
            out.append(f"{indent}- !b {'true' if v else 'false'}")
        elif isinstance(v, int):
            out.append(f"{indent}- !i {v}")
        elif isinstance(v, float):
            s = str(v)
            if "." not in s and "e" not in s and "E" not in s:
                s += ".0"
            out.append(f"{indent}- !f {s}")
        elif isinstance(v, (bytes, bytearray)):
            out.append(f"{indent}- !xb {v.hex().upper()}")
        elif isinstance(v, Tagged):
            if "\n" in v.value or "\r" in v.value:
                out.append(f"{indent}- !{v.tag} |")
                for line in v.value.splitlines():
                    out.append(f"{indent}  {line}")
                out.append(f"{indent}|")
            else:
                out.append(f"{indent}- !{v.tag} {v.value}")
        else:
            raise XunError(f"unsupported list item type: {type(v).__name__}")
