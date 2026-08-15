from __future__ import annotations

import base64
import datetime
import ipaddress
import re
import uuid
from dataclasses import dataclass
from typing import Any, TextIO

__all__ = [
    "parse",
    "decode",
    "encode",
    "dump",
    "dumps",
    "load",
    "loads",
    "unpack",
    "parse_size",
    "parse_duration",
    "parse_version",
    "Tagged",
    "XunError",
]

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
MAX_BYTES = 1024 * 1024
MAX_DEPTH = 64


class XunError(ValueError):
    def __init__(self, message: str, line: int = 0, column: int = 0, source_line: str = "") -> None:
        parts: list[str] = []
        if line:
            parts.append(f"line {line}")
        if column:
            parts.append(f"col {column}")
        prefix = f"[{': '.join(parts)}] " if parts else ""
        detail = f"\n  --> {source_line.strip()}" if source_line else ""
        super().__init__(f"{prefix}{message}{detail}")
        self.message = message
        self.line = line
        self.column = column
        self.source_line = source_line


@dataclass(frozen=True)
class Tagged:
    tag: str
    value: str

    def to_datetime(self) -> datetime.datetime:
        if self.tag != "dt":
            raise XunError(f"cannot convert !{self.tag} to datetime")
        # Handles ISO 8601 format like 2026-08-14T16:54:00+08:00
        val = self.value.replace("Z", "+00:00")
        return datetime.datetime.fromisoformat(val)

    def to_date(self) -> datetime.date:
        if self.tag != "d":
            raise XunError(f"cannot convert !{self.tag} to date")
        return datetime.date.fromisoformat(self.value)

    def to_time(self) -> datetime.time:
        if self.tag != "t":
            raise XunError(f"cannot convert !{self.tag} to time")
        return datetime.time.fromisoformat(self.value)

    def to_ip(self) -> ipaddress.IPv4Address | ipaddress.IPv6Address:
        if self.tag != "ip":
            raise XunError(f"cannot convert !{self.tag} to IP address")
        return ipaddress.ip_address(self.value)

    def to_uuid(self) -> uuid.UUID:
        if self.tag != "uuid":
            raise XunError(f"cannot convert !{self.tag} to UUID")
        return uuid.UUID(self.value)

    def to_size_bytes(self) -> int:
        if self.tag != "sz":
            raise XunError(f"cannot convert !{self.tag} to size bytes")
        return parse_size(self.value)

    def to_duration_seconds(self) -> float:
        if self.tag != "du":
            raise XunError(f"cannot convert !{self.tag} to duration seconds")
        return parse_duration(self.value)

    def to_version_parts(self) -> tuple[int, ...]:
        if self.tag != "ver":
            raise XunError(f"cannot convert !{self.tag} to version parts")
        return parse_version(self.value)

    def to_bytes(self) -> bytes:
        if self.tag == "xb":
            return bytes.fromhex(self.value.replace("_", ""))
        if self.tag == "b64":
            return base64.b64decode(re.sub(r"\s+", "", self.value))
        raise XunError(f"cannot convert !{self.tag} to raw bytes")


def parse_size(s: str) -> int:
    units = {
        "B": 1,
        "KB": 1000,
        "MB": 1000**2,
        "GB": 1000**3,
        "TB": 1000**4,
        "PB": 1000**5,
        "KiB": 1024,
        "MiB": 1024**2,
        "GiB": 1024**3,
        "TiB": 1024**4,
        "PiB": 1024**5,
    }
    m = re.fullmatch(r"(\d+(?:\.\d+)?)(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)", s)
    if not m:
        raise XunError(f"invalid size format: {s!r}")
    num_str, unit = m.group(1), m.group(2)
    return int(float(num_str) * units[unit])


def parse_duration(s: str) -> float:
    if not s:
        raise XunError("empty duration string")
    m = re.fullmatch(r"(?:(\d+)d)?(?:(\d+)h)?(?:(\d+)m)?(?:(\d+(?:\.\d+)?)s)?", s)
    if not m or not any(m.groups()):
        raise XunError(f"invalid duration format: {s!r}")
    days = int(m.group(1) or 0)
    hours = int(m.group(2) or 0)
    minutes = int(m.group(3) or 0)
    seconds = float(m.group(4) or 0)
    return days * 86400.0 + hours * 3600.0 + minutes * 60.0 + seconds


def parse_version(s: str) -> tuple[int, ...]:
    if not re.fullmatch(r"\d+(?:\.\d+)*", s):
        raise XunError(f"invalid version format: {s!r}")
    return tuple(int(x) for x in s.split("."))


def unpack(v: Any) -> Any:
    """Recursively unpack Tagged types into native Python types where applicable."""
    if isinstance(v, Tagged):
        if v.tag == "dt":
            return v.to_datetime()
        if v.tag == "d":
            return v.to_date()
        if v.tag == "t":
            return v.to_time()
        if v.tag == "ip":
            return v.to_ip()
        if v.tag == "uuid":
            return v.to_uuid()
        if v.tag == "ver":
            return v.to_version_parts()
        if v.tag == "sz":
            return v.to_size_bytes()
        if v.tag == "du":
            return v.to_duration_seconds()
        return v.value
    if isinstance(v, dict):
        return {k: unpack(val) for k, val in v.items()}
    if isinstance(v, list):
        return [unpack(item) for item in v]
    return v


def parse(source: str) -> Any:
    if not isinstance(source, str):
        raise XunError("source must be a string")
    if len(source.encode("utf-8")) > MAX_BYTES:
        raise XunError("document exceeds 1MB limit")
    if "\x00" in source:
        raise XunError("NUL (U+0000) byte is not allowed")
    if source.startswith("\ufeff"):
        source = source[1:]
    return Parser(_split_lines(source)).parse_document()


decode = parse
loads = parse


def load(fp: TextIO) -> Any:
    return parse(fp.read())


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
        raise XunError("tab character is not allowed for indentation", line=n, column=i + 1, source_line=raw)
    if i % 2 != 0:
        raise XunError(f"indent must be a multiple of 2, got {i} spaces", line=n, column=i + 1, source_line=raw)
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
            raise XunError("document must start at indent 0", line=first.n, source_line=first.raw)
        if self.is_list_item(first):
            raise XunError("root must be a dictionary", line=first.n, source_line=first.raw)
        return self.parse_dict(0, 0)

    def parse_dict(self, indent: int, depth: int) -> dict[str, Any]:
        if depth > MAX_DEPTH:
            raise XunError("nesting depth exceeds limit of 64", line=self.peek().n if self.peek() else 0)
        obj: dict[str, Any] = {}
        while self.peek():
            self.skip_noise()
            l = self.peek()
            if not l or l.blank:
                break
            if l.indent < indent:
                break
            if l.indent > indent:
                raise XunError(f"invalid indent jump from {indent} to {l.indent}", line=l.n, source_line=l.raw)
            if self.is_list_item(l):
                raise XunError("cannot mix list items into a dictionary", line=l.n, source_line=l.raw)
            key, rest = _split_key(l.text, l.n, l.raw)
            if key in obj:
                raise XunError(f"duplicate key '{key}' in dictionary", line=l.n, source_line=l.raw)
            self.i += 1
            obj[key] = self.parse_value(rest, indent, l.n, l.raw, depth + 1)
        return obj

    def parse_list(self, indent: int, depth: int, item_tag: str | None = None) -> list[Any]:
        if depth > MAX_DEPTH:
            raise XunError("nesting depth exceeds limit of 64", line=self.peek().n if self.peek() else 0)
        arr: list[Any] = []
        while self.peek():
            self.skip_noise()
            l = self.peek()
            if not l or l.blank:
                break
            if l.indent < indent:
                break
            if l.indent > indent:
                raise XunError(f"invalid indent jump from {indent} to {l.indent}", line=l.n, source_line=l.raw)
            if not self.is_list_item(l):
                raise XunError("cannot mix dictionary keys into a list", line=l.n, source_line=l.raw)
            rest = "" if l.text == "-" else l.text[2:]
            self.i += 1
            val = self.parse_value(rest, indent, l.n, l.raw, depth + 1)
            if item_tag:
                val = apply_tag(item_tag, glyph_of(val), l.n, l.raw)
            arr.append(val)
        return arr

    def is_list_item(self, l: Line) -> bool:
        return l.text == "-" or l.text.startswith("- ")

    def parse_value(self, raw: str, parent_indent: int, line_no: int, source_line: str, depth: int) -> Any:
        if raw == "[]":
            return []
        if raw == "{}":
            return {}
        ml = _match_multiline(raw)
        if ml:
            return self.read_multiline(parent_indent, ml[0], ml[1], line_no, source_line)
        if raw.startswith("!"):
            return self.parse_tagged(raw, parent_indent, line_no, source_line, depth)
        if raw == "":
            return self.parse_empty_or_nested(parent_indent, line_no, source_line, depth, None)
        return raw

    def parse_tagged(self, raw: str, parent_indent: int, line_no: int, source_line: str, depth: int) -> Any:
        m = re.match(r"^!([A-Za-z_][A-Za-z0-9_]*)(.*)$", raw)
        if not m:
            raise XunError("invalid type tag format", line=line_no, source_line=source_line)
        tag, rest = m.group(1), m.group(2)
        if rest.startswith("["):
            if tag == "s" and rest != "[]":
                raise XunError("string arrays cannot use compact form !s[...]", line=line_no, source_line=source_line)
            if not rest.endswith("]"):
                raise XunError("unclosed compact array bracket", line=line_no, source_line=source_line)
            inner = rest[1:-1]
            if inner == "":
                return self.parse_empty_or_nested(parent_indent, line_no, source_line, depth, tag)
            return [apply_tag(tag, g, line_no, source_line) for g in _split_compact(inner)]
        if rest == "":
            raise XunError(f"missing value for !{tag}", line=line_no, source_line=source_line)
        if not rest.startswith(" "):
            raise XunError("expected space after type tag", line=line_no, source_line=source_line)
        body = rest[1:]
        ml = _match_multiline(body)
        if ml:
            text = self.read_multiline(parent_indent, ml[0], ml[1], line_no, source_line)
            return text if tag == "s" else apply_tag(tag, text, line_no, source_line)
        if tag == "s":
            return body
        return apply_tag(tag, body, line_no, source_line)

    def parse_empty_or_nested(
        self, parent_indent: int, line_no: int, source_line: str, depth: int, item_tag: str | None
    ) -> Any:
        self.skip_noise()
        n = self.peek()
        child = parent_indent + 2
        if not n or n.blank or n.indent <= parent_indent:
            return [] if item_tag else ""
        if n.indent != child:
            raise XunError(f"child indent must be parent + 2 ({child}), got {n.indent}", line=n.n, source_line=n.raw)
        if self.is_list_item(n):
            return self.parse_list(child, depth, item_tag)
        if item_tag:
            raise XunError(f"!{item_tag}[] expected list items starting with '-'", line=n.n, source_line=n.raw)
        return self.parse_dict(child, depth)

    def read_multiline(self, parent_indent: int, tag: str | None, closer: str, line_no: int, source_line: str) -> Any:
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
                    return apply_tag(tag, s, line_no, source_line)
                return s
            if l.blank:
                parts.append("")
                self.i += 1
                continue
            if ind < base and not l.blank:
                raise XunError("multiline body line must be indented +2 or closed at opener indent", line=l.n, source_line=l.raw)
            if "\t" in l.raw:
                raise XunError("tab character is not allowed in multiline body", line=l.n, source_line=l.raw)
            parts.append(l.raw[base:])
            self.i += 1
        raise XunError(f"unclosed multiline block (expected '{closer}' at indent {parent_indent})", line=line_no, source_line=source_line)


def _split_key(text: str, n: int, source_line: str) -> tuple[str, str]:
    idx = text.find(": ")
    if idx > 0:
        return text[:idx], text[idx + 2 :]
    if text.endswith(":") and len(text) > 1:
        return text[:-1], ""
    raise XunError("expected ': ' or trailing ':' for key-value pair", line=n, source_line=source_line)


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


def _strip_underscores(s: str, n: int, source_line: str) -> str:
    if "__" in s or s.startswith("_") or s.endswith("_"):
        raise XunError("invalid numeric underscores", line=n, source_line=source_line)
    return s.replace("_", "")


def apply_tag(tag: str, glyph: str, n: int, source_line: str = "") -> Any:
    if tag == "s":
        return glyph
    if tag == "n":
        return _parse_n(glyph, n, source_line)
    if tag == "i":
        return _parse_i(glyph, n, source_line)
    if tag == "f":
        return _parse_f(glyph, n, source_line)
    if tag == "x":
        return int(_strip_underscores(glyph, n, source_line), 16)
    if tag == "xb":
        s = glyph.replace("_", "")
        if not re.fullmatch(r"[0-9A-Fa-f]*", s) or len(s) % 2 or not s:
            raise XunError("hex bytes must be an even number of hex digits", line=n, source_line=source_line)
        return bytes.fromhex(s)
    if tag == "o":
        if not re.fullmatch(r"[0-7]+", glyph):
            raise XunError("invalid octal format", line=n, source_line=source_line)
        return int(glyph, 8)
    if tag == "b":
        if glyph == "true":
            return True
        if glyph == "false":
            return False
        raise XunError("boolean value must be exactly 'true' or 'false'", line=n, source_line=source_line)
    if tag == "d":
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", glyph):
            raise XunError("invalid date format, expected YYYY-MM-DD", line=n, source_line=source_line)
        return Tagged("d", glyph)
    if tag == "t":
        if not re.fullmatch(r"\d{2}:\d{2}(:\d{2}(\.\d+)?)?", glyph):
            raise XunError("invalid time format, expected HH:MM[:SS[.sss]]", line=n, source_line=source_line)
        return Tagged("t", glyph)
    if tag == "dt":
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})", glyph):
            raise XunError("datetime must include ISO timezone offset (e.g. Z or +08:00)", line=n, source_line=source_line)
        return Tagged("dt", glyph)
    if tag == "tz":
        if glyph not in {"Z", "UTC"} and not re.fullmatch(r"[+-]\d{2}:\d{2}", glyph) and not re.fullmatch(r"[A-Za-z_]+(/[A-Za-z0-9_+-]+)+", glyph):
            raise XunError("invalid time zone name or offset", line=n, source_line=source_line)
        return Tagged("tz", glyph)
    if tag == "du":
        if not glyph or not re.fullmatch(r"(\d+d)?(\d+h)?(\d+m)?(\d+(\.\d+)?s)?", glyph):
            raise XunError("invalid duration format (e.g. 1d2h30m)", line=n, source_line=source_line)
        return Tagged("du", glyph)
    if tag == "sz":
        if not re.fullmatch(r"\d+(\.\d+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)", glyph):
            raise XunError("invalid data size format (e.g. 10MiB, 3KB)", line=n, source_line=source_line)
        return Tagged("sz", glyph)
    if tag == "unix":
        return _parse_unix(glyph, n, source_line)
    if tag == "ver":
        if not re.fullmatch(r"\d+(\.\d+)*", glyph):
            raise XunError("invalid version format, expected segment-separated numbers (e.g. 3.10)", line=n, source_line=source_line)
        return Tagged("ver", glyph)
    if tag == "uuid":
        if not re.fullmatch(r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}", glyph):
            raise XunError("invalid UUID format, expected 8-4-4-4-12 hex with hyphens", line=n, source_line=source_line)
        return Tagged("uuid", glyph)
    if tag == "ip":
        try:
            ipaddress.ip_address(glyph)
        except ValueError as e:
            raise XunError(f"invalid IP address '{glyph}'", line=n, source_line=source_line) from e
        return Tagged("ip", glyph)
    if tag == "b64":
        s = re.sub(r"\s+", "", glyph)
        try:
            return base64.b64decode(s, validate=True)
        except Exception as e:
            raise XunError("invalid Base64 payload", line=n, source_line=source_line) from e
    if tag == "c":
        u = re.fullmatch(r"U\+([0-9A-Fa-f]{4,6})", glyph)
        if u:
            cp = int(u.group(1), 16)
            if cp > 0x10FFFF:
                raise XunError(f"invalid Unicode code point U+{cp:X}", line=n, source_line=source_line)
            return Tagged("c", chr(cp))
        if len(glyph) != 1:
            raise XunError(f"character !c must be a single scalar, got '{glyph}'", line=n, source_line=source_line)
        return Tagged("c", glyph)
    return Tagged(tag, glyph)


def _parse_n(g: str, n: int, source_line: str) -> int | float:
    s = _strip_underscores(g, n, source_line)
    if re.match(r"^-?0\d", s):
        raise XunError("leading zeros are not allowed in numbers", line=n, source_line=source_line)
    if re.fullmatch(r"-?\d+", s):
        v = int(s)
        if v > 2**63 - 1 or v < -(2**63):
            raise XunError("integer overflow (exceeds signed 64-bit int)", line=n, source_line=source_line)
        return v
    if re.fullmatch(r"-?\d+\.\d+([eE][+-]?\d+)?", s) or re.fullmatch(r"-?\d+[eE][+-]?\d+", s):
        return float(s)
    raise XunError(f"invalid number literal '{g}'", line=n, source_line=source_line)


def _parse_i(g: str, n: int, source_line: str) -> int:
    s = _strip_underscores(g, n, source_line)
    if not re.fullmatch(r"-?\d+", s):
        raise XunError(f"invalid integer literal '{g}'", line=n, source_line=source_line)
    if re.match(r"^-?0\d", s):
        raise XunError("leading zeros are not allowed in integers", line=n, source_line=source_line)
    v = int(s)
    if v > 2**63 - 1 or v < -(2**63):
        raise XunError("integer overflow (exceeds signed 64-bit int)", line=n, source_line=source_line)
    return v


def _parse_f(g: str, n: int, source_line: str) -> float:
    s = _strip_underscores(g, n, source_line)
    if "." not in s and not re.search(r"[eE]", s):
        raise XunError("float !f must contain '.' or 'e'", line=n, source_line=source_line)
    return float(s)


def _parse_unix(g: str, n: int, source_line: str) -> int | float:
    s = _strip_underscores(g, n, source_line)
    if re.match(r"^-?0\d", s):
        raise XunError("leading zeros are not allowed in timestamp", line=n, source_line=source_line)
    if re.fullmatch(r"-?\d+", s):
        return int(s)
    if re.fullmatch(r"-?\d+\.\d+", s):
        return float(s)
    raise XunError(f"invalid unix timestamp literal '{g}'", line=n, source_line=source_line)


# --- Encoder ---

def encode(value: Any) -> str:
    if not isinstance(value, dict):
        raise XunError(f"root must be a dictionary, got {type(value).__name__}")
    if not value:
        return ""
    lines: list[str] = []
    seen: set[int] = set()
    _encode_dict_items(value, 0, lines, seen, path="root")
    return "\n".join(lines) + "\n"


def dumps(value: Any) -> str:
    return encode(value)


def dump(value: Any, fp: TextIO) -> None:
    fp.write(encode(value))


def _validate_key(key: Any, path: str) -> str:
    if not isinstance(key, str) or not key:
        raise XunError(f"key at path '{path}' must be a non-empty string, got: {key!r}")
    if "\n" in key or "\r" in key or ": " in key or key.endswith(":"):
        raise XunError(f"invalid key format '{key}' at path '{path}' (cannot contain newlines or ': ')")
    return key


def _encode_dict_items(d: dict[str, Any], depth: int, out: list[str], seen: set[int], path: str) -> None:
    if depth > MAX_DEPTH:
        raise XunError(f"nesting depth exceeds limit of 64 at path '{path}'")
    obj_id = id(d)
    if obj_id in seen:
        raise XunError(f"circular reference detected at path '{path}'")
    seen.add(obj_id)
    try:
        indent = "  " * depth
        for k, v in d.items():
            current_path = f"{path}.{k}"
            key = _validate_key(k, current_path)
            if isinstance(v, dict):
                if not v:
                    out.append(f"{indent}{key}: {{}}")
                else:
                    out.append(f"{indent}{key}:")
                    _encode_dict_items(v, depth + 1, out, seen, current_path)
            elif isinstance(v, (list, tuple)):
                if not v:
                    out.append(f"{indent}{key}: []")
                else:
                    out.append(f"{indent}{key}:")
                    _encode_list_items(v, depth + 1, out, seen, current_path)
            else:
                _encode_scalar_field(indent, key, v, out, current_path)
    finally:
        seen.remove(obj_id)


def _encode_list_items(items: list[Any] | tuple[Any, ...], depth: int, out: list[str], seen: set[int], path: str) -> None:
    if depth > MAX_DEPTH:
        raise XunError(f"nesting depth exceeds limit of 64 at path '{path}'")
    obj_id = id(items)
    if obj_id in seen:
        raise XunError(f"circular reference detected at path '{path}'")
    seen.add(obj_id)
    try:
        indent = "  " * depth
        for idx, v in enumerate(items):
            current_path = f"{path}[{idx}]"
            if isinstance(v, dict):
                if not v:
                    out.append(f"{indent}- {{}}")
                else:
                    out.append(f"{indent}-")
                    _encode_dict_items(v, depth + 1, out, seen, current_path)
            elif isinstance(v, (list, tuple)):
                if not v:
                    out.append(f"{indent}- []")
                else:
                    out.append(f"{indent}-")
                    _encode_list_items(v, depth + 1, out, seen, current_path)
            else:
                _encode_scalar_list_item(indent, v, out, current_path)
    finally:
        seen.remove(obj_id)


def _encode_scalar_field(indent: str, key: str, v: Any, out: list[str], path: str) -> None:
    if v is None:
        out.append(f"{indent}{key}:")
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
    elif isinstance(v, datetime.datetime):
        # Format as ISO-8601 with timezone if available
        iso = v.isoformat()
        if v.tzinfo is None:
            iso += "Z"
        out.append(f"{indent}{key}: !dt {iso}")
    elif isinstance(v, datetime.date):
        out.append(f"{indent}{key}: !d {v.isoformat()}")
    elif isinstance(v, datetime.time):
        out.append(f"{indent}{key}: !t {v.isoformat()}")
    elif isinstance(v, (ipaddress.IPv4Address, ipaddress.IPv6Address)):
        out.append(f"{indent}{key}: !ip {v}")
    elif isinstance(v, uuid.UUID):
        out.append(f"{indent}{key}: !uuid {v}")
    elif isinstance(v, Tagged):
        if "\n" in v.value or "\r" in v.value:
            out.append(f"{indent}{key}: !{v.tag} |")
            for line in v.value.splitlines():
                out.append(f"{indent}  {line}")
            out.append(f"{indent}|")
        else:
            out.append(f"{indent}{key}: !{v.tag} {v.value}")
    else:
        raise XunError(f"unsupported value type '{type(v).__name__}' at path '{path}'")


def _encode_scalar_list_item(indent: str, v: Any, out: list[str], path: str) -> None:
    if v is None:
        out.append(f"{indent}-")
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
    elif isinstance(v, datetime.datetime):
        iso = v.isoformat()
        if v.tzinfo is None:
            iso += "Z"
        out.append(f"{indent}- !dt {iso}")
    elif isinstance(v, datetime.date):
        out.append(f"{indent}- !d {v.isoformat()}")
    elif isinstance(v, datetime.time):
        out.append(f"{indent}- !t {v.isoformat()}")
    elif isinstance(v, (ipaddress.IPv4Address, ipaddress.IPv6Address)):
        out.append(f"{indent}- !ip {v}")
    elif isinstance(v, uuid.UUID):
        out.append(f"{indent}- !uuid {v}")
    elif isinstance(v, Tagged):
        if "\n" in v.value or "\r" in v.value:
            out.append(f"{indent}- !{v.tag} |")
            for line in v.value.splitlines():
                out.append(f"{indent}  {line}")
            out.append(f"{indent}|")
        else:
            out.append(f"{indent}- !{v.tag} {v.value}")
    else:
        raise XunError(f"unsupported list item type '{type(v).__name__}' at path '{path}'")
