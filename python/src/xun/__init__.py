from __future__ import annotations

import base64
import datetime
import ipaddress
import re
import uuid
from dataclasses import dataclass
from typing import Any, TextIO
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

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

    def to_timezone(self) -> datetime.tzinfo:
        if self.tag != "tz":
            raise XunError(f"cannot convert !{self.tag} to timezone")
        if self.value in {"Z", "UTC"}:
            return datetime.timezone.utc
        if re.fullmatch(r"[+-]\d{2}:\d{2}", self.value):
            sign = 1 if self.value[0] == "+" else -1
            hh, mm = (int(x) for x in self.value[1:].split(":"))
            return datetime.timezone(sign * datetime.timedelta(hours=hh, minutes=mm))
        try:
            return ZoneInfo(self.value)
        except (ZoneInfoNotFoundError, ValueError) as e:
            raise XunError(f"unknown timezone {self.value!r}") from e

    def to_char(self) -> str:
        if self.tag != "c":
            raise XunError(f"cannot convert !{self.tag} to char")
        m = re.fullmatch(r"U\+([0-9A-Fa-f]{4,6})", self.value)
        if m:
            cp = int(m.group(1), 16)
            if cp > 0x10FFFF:
                raise XunError(f"invalid Unicode code point U+{cp:X}")
            return chr(cp)
        if len(self.value) != 1:
            raise XunError(f"value {self.value!r} is not a single character")
        return self.value

    def to_bytes(self) -> bytes:
        if self.tag == "xb":
            return bytes.fromhex(self.value.replace("_", ""))
        if self.tag == "b64":
            return base64.b64decode(re.sub(r"\s+", "", self.value))
        raise XunError(f"cannot convert !{self.tag} to raw bytes")

    def to_number(self) -> int | float:
        if self.tag == "o":
            if not re.fullmatch(r"[0-7]+", self.value):
                raise XunError(f"invalid octal: {self.value}")
            return int(self.value, 8)
        if self.tag == "x":
            s = self.value.replace("_", "")
            if not re.fullmatch(r"[0-9A-Fa-f]+", s):
                raise XunError(f"invalid hex: {self.value}")
            return int(s, 16)
        if self.tag == "unix":
            return _parse_unix(self.value, 0, "")
        if self.tag in {"n", "i", "f"}:
            s = self.value.replace("_", "")
            if re.fullmatch(r"[+-]?\d+", s):
                return int(s)
            try:
                return float(s)
            except ValueError as e:
                raise XunError(f"invalid number: {self.value}") from e
        raise XunError(f"cannot convert !{self.tag} to number")


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
    m = re.fullmatch(r"(?:(\d+)d)?(?:(\d+)h)?(?:(\d+)m)?(?:(\d+(?:\.\d+)?)s)?(?:(\d+(?:\.\d+)?)ms)?", s)
    if not m or not any(m.groups()):
        raise XunError(f"invalid duration format: {s!r}")
    days = int(m.group(1) or 0)
    hours = int(m.group(2) or 0)
    minutes = int(m.group(3) or 0)
    seconds = float(m.group(4) or 0)
    millis = float(m.group(5) or 0)
    return days * 86400.0 + hours * 3600.0 + minutes * 60.0 + seconds + millis / 1000.0


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
        if v.tag in {"xb", "b64"}:
            return v.to_bytes()
        if v.tag == "c":
            return v.to_char()
        if v.tag == "tz":
            return v.to_timezone()
        if v.tag in {"o", "x", "unix"}:
            return v.to_number()
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
    code: str
    n: int
    blank: bool


def _strip_trailing_comment(text: str) -> str:
    """Strip a trailing ` # ...` comment outside of quoted strings."""
    in_quote = False
    i = 0
    while i < len(text):
        ch = text[i]
        if in_quote:
            if ch == "\\":
                i += 2
                continue
            if ch == '"':
                in_quote = False
            i += 1
            continue
        if ch == '"':
            in_quote = True
            i += 1
            continue
        if ch == "#" and (i == 0 or text[i - 1] in " \t"):
            return text[:i].rstrip(" \t")
        i += 1
    return text


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
    code = _strip_trailing_comment(text)
    return Line(raw, i, text, code, n, len(text) == 0)


class Parser:
    def __init__(self, lines: list[Line]) -> None:
        self.lines = lines
        self.i = 0
        # RFC-0001: stack of (indent, expected_key) for 'end' validation.
        self.open_blocks: list[tuple[int, str | None]] = []

    def peek(self) -> Line | None:
        return self.lines[self.i] if self.i < len(self.lines) else None

    def skip_noise(self) -> None:
        while self.peek():
            l = self.peek()
            assert l is not None
            if l.blank or len(l.code) == 0:
                self.i += 1
            else:
                break

    # RFC-0001: try to consume an 'end' or 'end <key>' statement at given indent.
    # Returns True if consumed, False if not an end statement.
    # Raises XunError on key mismatch.
    def try_consume_end(self, indent: int, expected_key: str | None) -> bool:
        l = self.peek()
        if not l or l.blank or l.indent != indent:
            return False
        code = l.code
        if code == "end":
            self.i += 1
            return True
        m = re.match(r"^end\s+(\S+)$", code)
        if m:
            end_key = m.group(1)
            if expected_key is not None and expected_key != end_key:
                raise XunError(
                    f"end-key mismatch: expected '{expected_key}', got '{end_key}'",
                    line=l.n,
                    source_line=l.raw,
                )
            self.i += 1
            return True
        return False

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
        # RFC-0001: track root block so trailing 'end' can close it.
        self.open_blocks.append((0, None))
        try:
            obj = self.parse_dict(0, 0, None)
            self.skip_noise()
            self.try_consume_end(0, None)
            return obj
        finally:
            self.open_blocks.pop()

    def parse_dict(self, indent: int, depth: int, dict_key: str | None = None) -> dict[str, Any]:
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
            # RFC-0001: 'end' at body indent stops the dict (caller validates via try_consume_end).
            if l.code == "end" or l.code.startswith("end "):
                break
            if self.is_list_item(l):
                raise XunError("cannot mix list items into a dictionary", line=l.n, source_line=l.raw)
            key, rest = _split_key(l.code, l.n, l.raw)
            if key in obj:
                raise XunError(f"duplicate key '{key}' in dictionary", line=l.n, source_line=l.raw)
            self.i += 1
            obj[key] = self.parse_value(rest, indent, l.n, l.raw, depth + 1, key)
        return obj

    def parse_list(
        self, indent: int, depth: int, item_tag: str | None = None, parent_key: str | None = None
    ) -> list[Any]:
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
            # RFC-0001: 'end' at list level stops the list.
            if l.code == "end" or l.code.startswith("end "):
                break
            if not self.is_list_item(l):
                raise XunError("cannot mix dictionary keys into a list", line=l.n, source_line=l.raw)
            rest = "" if l.code == "-" else l.code[2:]
            self.i += 1
            if _looks_like_dict_entry(rest):
                if item_tag:
                    raise XunError(f"!{item_tag}[] cannot contain dictionary entries", line=l.n, source_line=l.raw)
                # RFC-0002: inline object literal {key: value, ...} as a list item.
                if rest.strip().startswith("{"):
                    arr.append(_parse_inline_dict(rest, l.n, l.raw))
                    continue
                arr.append(self.parse_inline_dict_entry(rest, indent + 2, l.n, l.raw, depth + 1))
                continue
            val = self.parse_value(rest, indent, l.n, l.raw, depth + 1)
            if item_tag:
                val = apply_tag(item_tag, glyph_of(val), l.n, l.raw)
            arr.append(val)
        return arr

    def parse_inline_dict_entry(
        self, first: str, key_indent: int, line_no: int, source_line: str, depth: int
    ) -> dict[str, Any]:
        obj: dict[str, Any] = {}
        key, rest = _split_key(first, line_no, source_line)
        if key in obj:
            raise XunError(f"duplicate key '{key}' in dictionary", line=line_no, source_line=source_line)
        obj[key] = self.parse_value(rest, key_indent, line_no, source_line, depth + 1, key)
        while self.peek():
            self.skip_noise()
            l = self.peek()
            if not l or l.blank:
                break
            if l.indent < key_indent:
                break
            if l.indent > key_indent:
                raise XunError(
                    f"invalid indent jump from {key_indent} to {l.indent}", line=l.n, source_line=l.raw
                )
            if self.is_list_item(l):
                break
            key, rest = _split_key(l.text, l.n, l.raw)
            if key in obj:
                raise XunError(f"duplicate key '{key}' in dictionary", line=l.n, source_line=l.raw)
            self.i += 1
            obj[key] = self.parse_value(rest, key_indent, l.n, l.raw, depth + 1, key)
        return obj

    def is_list_item(self, l: Line) -> bool:
        return l.code == "-" or l.code.startswith("- ")

    def parse_value(
        self,
        raw: str,
        parent_indent: int,
        line_no: int,
        source_line: str,
        depth: int,
        value_key: str | None = None,
    ) -> Any:
        raw = _strip_trailing_comment(raw)
        if raw == "[]":
            return []
        if raw == "{}":
            return {}
        ml = _match_multiline(raw)
        if ml:
            return self.read_multiline(parent_indent, ml[0], ml[1], line_no, source_line)
        if raw.startswith("!"):
            return self.parse_tagged(raw, parent_indent, line_no, source_line, depth, value_key)
        # RFC-0002: untagged compact array of inline objects: [{...}, {...}]
        if raw.startswith("[") and raw.endswith("]") and raw != "[]":
            inner = raw[1:-1]
            pieces = _split_top_level_commas(inner, line_no, source_line)
            if any(p.strip().startswith("{") for p in pieces):
                result: list[Any] = []
                for piece in pieces:
                    t = piece.strip()
                    if t.startswith("{"):
                        result.append(_parse_inline_dict(t, line_no, source_line))
                    else:
                        result.append(t)
                return result
        if raw == "":
            # RFC-0001: nested block — header at parent_indent, named value_key.
            self.open_blocks.append((parent_indent, value_key))
            try:
                result = self.parse_empty_or_nested(parent_indent, line_no, source_line, depth, None, value_key)
                self.skip_noise()
                self.try_consume_end(parent_indent, value_key)
                return result
            finally:
                self.open_blocks.pop()
        if raw.startswith('"'):
            return _parse_quoted_string(raw, line_no, source_line)
        return raw

    def parse_tagged(
        self,
        raw: str,
        parent_indent: int,
        line_no: int,
        source_line: str,
        depth: int,
        value_key: str | None = None,
    ) -> Any:
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
                # RFC-0001: track tagged block for 'end' validation.
                self.open_blocks.append((parent_indent, value_key))
                try:
                    result = self.parse_empty_or_nested(parent_indent, line_no, source_line, depth, tag, value_key)
                    self.skip_noise()
                    self.try_consume_end(parent_indent, value_key)
                    return result
                finally:
                    self.open_blocks.pop()
            # RFC-0002: detect inline object elements {key: val, ...} within compact arrays.
            pieces = _split_top_level_commas(inner, line_no, source_line)
            if any(p.strip().startswith("{") for p in pieces):
                result2: list[Any] = []
                for piece in pieces:
                    t = piece.strip()
                    if t.startswith("{"):
                        result2.append(_parse_inline_dict(t, line_no, source_line))
                    else:
                        result2.append(apply_tag(tag, t, line_no, source_line))
                return result2
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
            return _parse_string_body(body, line_no, source_line)
        return apply_tag(tag, body, line_no, source_line)

    def parse_empty_or_nested(
        self,
        parent_indent: int,
        line_no: int,
        source_line: str,
        depth: int,
        item_tag: str | None,
        value_key: str | None = None,
    ) -> Any:
        self.skip_noise()
        n = self.peek()
        child = parent_indent + 2
        if not n or n.blank or n.indent <= parent_indent:
            return [] if item_tag else ""
        if n.indent != child:
            raise XunError(f"child indent must be parent + 2 ({child}), got {n.indent}", line=n.n, source_line=n.raw)
        if self.is_list_item(n):
            return self.parse_list(child, depth, item_tag, value_key)
        if item_tag:
            raise XunError(f"!{item_tag}[] expected list items starting with '-'", line=n.n, source_line=n.raw)
        return self.parse_dict(child, depth, value_key)

    def read_multiline(self, parent_indent: int, closer: str, chomp: str, line_no: int, source_line: str) -> str:
        base = parent_indent + 2
        parts: list[str] = []
        while self.peek():
            l = self.peek()
            assert l is not None
            stripped = l.raw.rstrip(" \t")
            content = stripped.lstrip(" ")
            ind = len(l.raw) - len(l.raw.lstrip(" "))
            closer_text = _strip_trailing_comment(content)
            if not l.blank and ind == parent_indent and closer_text == closer:
                self.i += 1
                s = "\n".join(parts)
                if chomp == "strip":
                    s = s.rstrip("\n")
                elif chomp == "clip" and s and not s.endswith("\n"):
                    s += "\n"
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


def _parse_quoted_prefix(raw: str, line_no: int, source_line: str = "") -> tuple[str, int]:
    if not raw.startswith('"'):
        raise XunError("quoted string must start with '\"'", line=line_no, source_line=source_line)
    out: list[str] = []
    i = 1
    while i < len(raw):
        ch = raw[i]
        if ch == "\\":
            if i + 1 >= len(raw):
                raise XunError("unclosed escape in quoted string", line=line_no, source_line=source_line)
            nxt = raw[i + 1]
            if nxt in ("\\", '"'):
                out.append(nxt)
                i += 2
                continue
            raise XunError(f"invalid escape \\{nxt} in quoted string", line=line_no, source_line=source_line)
        if ch == '"':
            return "".join(out), i + 1
        out.append(ch)
        i += 1
    raise XunError("unclosed quoted string", line=line_no, source_line=source_line)


def _split_key(text: str, n: int, source_line: str) -> tuple[str, str]:
    if text.startswith('"'):
        key, end = _parse_quoted_prefix(text, n, source_line)
        after = text[end:]
        if after == ":":
            return key, ""
        if after.startswith(": "):
            return key, after[2:]
        raise XunError("expected ': ' or trailing ':' after quoted key", line=n, source_line=source_line)
    idx = text.find(": ")
    if idx > 0:
        key = text[:idx]
        if key.endswith(":"):
            raise XunError(f"key must not end with ':': '{key}'", line=n, source_line=source_line)
        return key, text[idx + 2 :]
    if text.endswith(":") and len(text) > 1:
        key = text[:-1]
        if key.endswith(":"):
            raise XunError(f"key must not end with ':': '{key}'", line=n, source_line=source_line)
        return key, ""
    raise XunError("expected ': ' or trailing ':' for key-value pair", line=n, source_line=source_line)


def _match_multiline(raw: str) -> tuple[str, str] | None:
    """Return (closer, chomp) when raw opens a multiline block, else None."""
    if raw == "|":
        return "|", "exact"
    if raw == "|-":
        return "|", "strip"
    if raw == "|+":
        return "|", "clip"
    m = re.match(r"^\|([A-Za-z_][A-Za-z0-9_]*)([-+]?)$", raw)
    if m:
        chomp = "strip" if m.group(2) == "-" else "clip" if m.group(2) == "+" else "exact"
        return m.group(1), chomp
    return None


def _looks_like_dict_entry(s: str) -> bool:
    if not s or s.startswith("!") or s.startswith('"') or s.startswith("|"):
        return False
    # RFC-0002: inline object literal {key: val, ...} counts as a dict entry.
    if s.startswith("{"):
        return True
    return ": " in s or (s.endswith(":") and len(s) > 1)


# RFC-0002: split a string by top-level commas, respecting brace and quote nesting.
def _split_top_level_commas(inner: str, line_no: int, source_line: str) -> list[str]:
    out: list[str] = []
    start = 0
    brace = 0
    bracket = 0
    in_quote = False
    escape = False
    i = 0
    while i < len(inner):
        ch = inner[i]
        if escape:
            escape = False
            i += 1
            continue
        if in_quote:
            if ch == "\\":
                escape = True
                i += 1
                continue
            if ch == '"':
                in_quote = False
            i += 1
            continue
        if ch == '"':
            in_quote = True
            i += 1
            continue
        if ch == "{":
            brace += 1
            i += 1
            continue
        if ch == "}":
            brace -= 1
            i += 1
            continue
        if ch == "[":
            bracket += 1
            i += 1
            continue
        if ch == "]":
            bracket -= 1
            i += 1
            continue
        if ch == "," and brace == 0 and bracket == 0:
            out.append(inner[start:i])
            start = i + 1
        i += 1
    last = inner[start:]
    if last.strip():
        out.append(last)
    return out


# RFC-0002: parse inline object literal {key: value, key2: value2}.
def _parse_inline_dict(text: str, line_no: int, source_line: str) -> dict[str, Any]:
    trimmed = text.strip()
    if not trimmed.startswith("{") or not trimmed.endswith("}"):
        raise XunError(
            "inline object must be wrapped in '{...}'", line=line_no, source_line=source_line
        )
    inner = trimmed[1:-1]
    if inner.strip() == "":
        return {}
    pieces = _split_top_level_commas(inner, line_no, source_line)
    obj: dict[str, Any] = {}
    for piece in pieces:
        t = piece.strip()
        if not t:
            continue
        # Find top-level key separator ": " (or trailing ":" at end).
        in_quote = False
        escape = False
        found_idx = -1
        for idx, c in enumerate(t):
            if escape:
                escape = False
                continue
            if in_quote:
                if c == "\\":
                    escape = True
                    continue
                if c == '"':
                    in_quote = False
                continue
            if c == '"':
                in_quote = True
                continue
            if c == ":":
                # Trailing ":" separator (key with no value).
                if idx == len(t) - 1:
                    found_idx = idx
                    break
                # ": " separator (key: value) — require the next char to be a space.
                if idx + 1 < len(t) and t[idx + 1] == " ":
                    found_idx = idx
                    break
        if found_idx == -1:
            raise XunError(
                f"inline object entry missing ': ' separator: '{t}'",
                line=line_no,
                source_line=source_line,
            )
        key_raw = t[:found_idx].strip()
        rest = t[found_idx + 1:].strip()
        if key_raw.startswith('"'):
            key, end = _parse_quoted_prefix(key_raw, line_no, source_line)
            key_raw = key
        if not key_raw:
            raise XunError("empty key in inline object", line=line_no, source_line=source_line)
        if key_raw in obj:
            raise XunError(f"duplicate key '{key_raw}'", line=line_no, source_line=source_line)
        value: Any
        if rest == "":
            value = ""
        elif rest.startswith('"'):
            value = _parse_quoted_string(rest, line_no, source_line)
        elif rest.startswith("!"):
            tm = re.match(r"^!([A-Za-z_][A-Za-z0-9_]*)(.*)$", rest)
            if not tm:
                raise XunError(
                    f"invalid tag in inline object: '{rest}'", line=line_no, source_line=source_line
                )
            tag = tm.group(1)
            tail = tm.group(2)
            if tail == "":
                raise XunError(
                    f"missing value for !{tag}", line=line_no, source_line=source_line
                )
            if tail.startswith(" "):
                value = apply_tag(tag, tail[1:], line_no, source_line)
            elif tail.startswith("["):
                if not tail.endswith("]"):
                    raise XunError(
                        "unclosed compact array in inline object",
                        line=line_no,
                        source_line=source_line,
                    )
                compact_inner = tail[1:-1]
                if compact_inner == "":
                    value = []
                else:
                    value = [
                        apply_tag(tag, g, line_no, source_line)
                        for g in _split_compact(compact_inner)
                    ]
            else:
                raise XunError(
                    "expected space or '[' after type tag in inline object",
                    line=line_no,
                    source_line=source_line,
                )
        else:
            value = rest
        obj[key_raw] = value
    return obj


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
        s = _strip_underscores(glyph, n, source_line)
        if not re.fullmatch(r"[0-9A-Fa-f]+", s):
            raise XunError("invalid hex", line=n, source_line=source_line)
        return Tagged("x", glyph)
    if tag == "xb":
        s = glyph.replace("_", "")
        if not re.fullmatch(r"[0-9A-Fa-f]*", s) or len(s) % 2 or not s:
            raise XunError("hex bytes must be an even number of hex digits", line=n, source_line=source_line)
        return bytes.fromhex(s)
    if tag == "o":
        if not re.fullmatch(r"[0-7]+", glyph):
            raise XunError("invalid octal format", line=n, source_line=source_line)
        return Tagged("o", glyph)
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
        if not glyph or not re.fullmatch(r"(\d+d)?(\d+h)?(\d+m)?(\d+(\.\d+)?s)?(\d+(\.\d+)?ms)?", glyph):
            raise XunError("invalid duration format (e.g. 1d2h30m, 500ms)", line=n, source_line=source_line)
        return Tagged("du", glyph)
    if tag == "sz":
        if not re.fullmatch(r"\d+(\.\d+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)", glyph):
            raise XunError("invalid data size format (e.g. 10MiB, 3KB)", line=n, source_line=source_line)
        return Tagged("sz", glyph)
    if tag == "unix":
        _parse_unix(glyph, n, source_line)
        return Tagged("unix", glyph)
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


def _is_ver_tuple(v: Any) -> bool:
    return isinstance(v, tuple) and all(isinstance(x, int) and not isinstance(x, bool) for x in v)


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
            elif _is_ver_tuple(v):
                out.append(f"{indent}{key}: !ver {'.'.join(str(x) for x in v)}")
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
            elif _is_ver_tuple(v):
                out.append(f"{indent}- !ver {'.'.join(str(x) for x in v)}")
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


def _tz_glyph(v: datetime.tzinfo) -> str:
    if isinstance(v, ZoneInfo):
        return v.key
    offset = v.utcoffset(None)
    if offset is None:
        raise XunError("cannot encode a naive timezone")
    if offset == datetime.timedelta(0):
        return "Z"
    total_minutes = int(offset.total_seconds() // 60)
    sign = "+" if total_minutes >= 0 else "-"
    total_minutes = abs(total_minutes)
    return f"{sign}{total_minutes // 60:02d}:{total_minutes % 60:02d}"


# Glyphs that JavaScript Number() would coerce, plus syntactic specials.
_LOOKS_LIKE_JS_NUMBER = re.compile(
    r"^[ \t\n\r\f\v]*[+-]?(?:Infinity|0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)[ \t\n\r\f\v]*$"
)


def _needs_string_tag(s: str) -> bool:
    return s.startswith("!") or s in ("[]", "{}") or s.startswith("|") or _LOOKS_LIKE_JS_NUMBER.match(s) is not None


def _parse_quoted_string(raw: str, line_no: int, source_line: str = "") -> str:
    if not raw.startswith('"'):
        raise XunError("quoted string must start with '\"'", line=line_no, source_line=source_line)
    out: list[str] = []
    i = 1
    while i < len(raw):
        ch = raw[i]
        if ch == "\\":
            if i + 1 >= len(raw):
                raise XunError("unclosed escape in quoted string", line=line_no, source_line=source_line)
            nxt = raw[i + 1]
            if nxt in ('\\', '"'):
                out.append(nxt)
                i += 2
                continue
            raise XunError(f"invalid escape \\{nxt} in quoted string", line=line_no, source_line=source_line)
        if ch == '"':
            if i != len(raw) - 1:
                raise XunError("unexpected trailing content after quoted string", line=line_no, source_line=source_line)
            return "".join(out)
        out.append(ch)
        i += 1
    raise XunError("unclosed quoted string", line=line_no, source_line=source_line)


def _needs_quoted_glyph(s: str) -> bool:
    return (
        s != s.strip()
        or '"' in s
        or "\\" in s
        or " #" in s
        or s.startswith("#")
        or ": " in s
        or (s.endswith(":") and len(s) > 1)
        or s == "|"
        or s.startswith("|")
    )


def _quote_glyph(s: str) -> str:
    parts = ['"']
    for ch in s:
        if ch == "\\":
            parts.append("\\\\")
        elif ch == '"':
            parts.append('\\"')
        else:
            parts.append(ch)
    parts.append('"')
    return "".join(parts)


def _encode_string_glyph(s: str) -> str:
    body = _quote_glyph(s) if _needs_quoted_glyph(s) else s
    return f"!s {body}" if _needs_string_tag(s) else body


def _parse_string_body(body: str, line_no: int, source_line: str = "") -> str:
    if body.startswith('"'):
        return _parse_quoted_string(body, line_no, source_line)
    return body


def _encode_scalar_field(indent: str, key: str, v: Any, out: list[str], path: str) -> None:
    if v is None:
        out.append(f"{indent}{key}:")
    elif isinstance(v, str):
        if "\n" in v or "\r" in v:
            out.append(f"{indent}{key}: |")
            for line in re.split(r"\r?\n", v):
                out.append(f"{indent}  {line}")
            out.append(f"{indent}|")
        elif v == "":
            out.append(f"{indent}{key}:")
        else:
            out.append(f"{indent}{key}: {_encode_string_glyph(v)}")
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
    elif isinstance(v, datetime.tzinfo):
        out.append(f"{indent}{key}: !tz {_tz_glyph(v)}")
    elif isinstance(v, Tagged):
        if "\n" in v.value or "\r" in v.value:
            out.append(f"{indent}{key}: !{v.tag} |")
            for line in re.split(r"\r?\n", v.value):
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
            for line in re.split(r"\r?\n", v):
                out.append(f"{indent}  {line}")
            out.append(f"{indent}|")
        elif v == "":
            out.append(f"{indent}-")
        else:
            out.append(f"{indent}- {_encode_string_glyph(v)}")
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
    elif isinstance(v, datetime.tzinfo):
        out.append(f"{indent}- !tz {_tz_glyph(v)}")
    elif isinstance(v, Tagged):
        if "\n" in v.value or "\r" in v.value:
            out.append(f"{indent}- !{v.tag} |")
            for line in re.split(r"\r?\n", v.value):
                out.append(f"{indent}  {line}")
            out.append(f"{indent}|")
        else:
            out.append(f"{indent}- !{v.tag} {v.value}")
    else:
        raise XunError(f"unsupported list item type '{type(v).__name__}' at path '{path}'")
