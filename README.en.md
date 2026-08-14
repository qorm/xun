# XUN

XUN (pronounced “shün”, like Chinese 讯) is a configuration notation for humans and machines: unquoted by default, types marked with `!tag`, and exactly two spaces per indent level. The name stands for **X Unquoted Notation**.

- File extension: `.xun`
- Media type: `text/xun`
- Language id: `xun`

[English](README.en.md) · [中文](README.md)

Compared with JSON: fewer quotes, comments, and real multiline text. Compared with YAML: no implicit typing (`3.10` stays `3.10`), and multiline blocks must be closed. Compared with TOML: nesting is indentation, not repeated table paths.

## Example

```xun
$api: https://api.example.com/v2
$ports: !n[80, 443, 8080]
$zone: !tz Asia/Shanghai

server:
  host: localhost
  port: !n 8080
  bind: !ip ::1
  tls:
    cert: /etc/ssl/cert.pem
    mode: !o 755

features:
  - auth
  - cache

ports: $ports
endpoint: ${api}/orders
tz: $zone
py: !ver 3.10
limit: !sz 10MiB
when: !dt 2026-08-14T16:54:00+08:00
color: !xb FF00AA

roles: !s[]
  - admin
  - ops

banner: |
  Welcome
  to XUN
|
```

## File

The entire file must be UTF-8. Invalid bytes and `NUL` (U+0000) are hard errors; they are never replaced with U+FFFD.

- BOM: allowed only at the start of the file; stripped on read
- Newlines: LF, CRLF, and CR each count as one line; normalized to LF
- Indent: ASCII space U+0020 only, exactly 2 per level. Tab, skipped levels, and odd counts are errors
- The root must be a dictionary. An empty file or comments-only file is `{}`
- No file includes

## Structure

Three node kinds: dictionary, list, scalar.

```xun
# dictionary: colon plus space, or a colon at end of line
host: localhost
tls:
  cert: /etc/ssl/cert.pem

# list
features:
  - auth
  - cache

# empty containers must be written out
plugins: []
meta: {}
```

The separator must be `: ` or a trailing `:`. A line like `key:value` (no space after the colon) is illegal.

A single container cannot mix `-` items and `key:` items. It is entirely a dict or entirely a list.

A key runs up to the first `: `. Keys may contain non-ASCII, must be non-empty, and must not contain `: `. Duplicate keys are an error (not last-wins). Keys, tags, and `true` / `false` are case-sensitive. Parse order of keys is preserved.

Trailing whitespace is stripped on structure lines and kept in multiline bodies.

## Types

A value with no `!tag` is a string. Nothing is guessed: `8080`, `true`, `NO`, and `3.10` are all strings. Put the tag in front of the value when you need a type.

| tag | Meaning | Valid form | Invalid |
| --- | --- | --- | --- |
| (none) / `!s` | string | rest of the line, literally. `!s` when the value starts with `!` or `$` | — |
| `!n` | number | integer if no `.` / `e`, otherwise float | leading zeros, `abc` |
| `!i` | integer | `8080` `-3` `1_000` | `1.5`, i64 overflow |
| `!f` | float | must contain `.` or `e`: `1.5` `1e-3` `8080.0` | `!f 8080` |
| `!x` | hex **integer** | `DEAD_BEEF` (leading zeros do not change the value) | empty, non-hex |
| `!xb` | hex **bytes** | `FF00AA` (even length; leading zeros kept) | odd length |
| `!o` | octal (file mode) | `755` `0644` | digits 8 or 9 |
| `!b` | boolean | `true` / `false` only | `yes` `ON` `1` |
| `!d` | date | `YYYY-MM-DD` | `08/14/2026` |
| `!t` | time | `HH:MM` or `HH:MM:SS` | `4pm` |
| `!dt` | date-time | must include `Z` or `±HH:MM` | missing offset |
| `!tz` | time zone | IANA name, or `Z` / `+08:00` | `CST` |
| `!du` | duration | `1d2h30m15s` | `90 minutes`; bare `10m` as mebibytes |
| `!sz` | data size | `10MiB` `3KB` `1024B` | `10m` (that is duration) |
| `!unix` | Unix epoch seconds | `1692000000` or with a fraction | leading zeros |
| `!ver` | version | `3.10` stored by segment, not as a float | `3.10.beta` |
| `!uuid` | UUID | `8-4-4-4-12`, hyphens required | missing hyphens |
| `!ip` | IP | `127.0.0.1` or `::1`, no port | `127.0.0.1:80` |
| `!b64` | Base64 | `SGVsbG8=` or `!b64 \| … \|` | illegal alphabet |
| `!c` | character | one Unicode scalar on the same line, or `U+000A` | multiline, `ab` |

Unknown tags (`!sql`, `!md`, `!json`) are legal: the parser keeps the tag and the raw glyph for the application.

There is no `null`. Absence means omit the key. An empty string is `key:` with no subtree.

## Arrays

The element type is marked on the array. Compact form is allowed when elements cannot contain commas. String arrays must use `-` items.

```xun
ports: !n[80, 443, 8080]
vowels: !c[a, e, i]
peers: !ip[127.0.0.1, ::1]
py: !ver[3.10, 3.11]

roles: !s[]
  - admin
  - ops
  - a, b is still one string

ports: !n[]
  - 80
  - 443
```

- `[]` is an empty, untyped list
- `!n[]` with no children is an empty number array
- A `-` list with no array tag defaults to strings; a single item may still carry `!n`

## Multiline

Closing a block by indent alone is ambiguous (blank lines; a forgotten indent becomes the next key). XUN enters body mode and restores structure only when it sees the closer.

```xun
banner: |
  Hello

  World
|
next: x

query: !sql |
  SELECT
    id,
    name
  FROM users
|
```

- Open: the value slot is `|` or `!tag |`
- Close: same indent as the opener, and the line is exactly `|` after stripping spaces
- Each body line is indented 2 more spaces; those 2 are stripped; extra spaces stay in the value
- Inside the body, `#`, `:`, `-`, `!`, and `$` are literal; variables are not expanded
- Missing closer at EOF is an error; the parser does not guess
- No trailing newline by default; keep a blank line before `|` if you need one

If the body itself may contain a line that is just `|`, tag the opener:

```xun
table: |MD
  | a | b |
  |---|---|
MD
```

`TAG` is `[A-Za-z_][A-Za-z0-9_]*`.

## Variable section

The file starts with a variable section. A line beginning with `$` is a definition. The first line that is not `$`, a comment, or blank ends the section and is the first document line. `$foo:` in the document is an error.

```xun
$api: https://api.example.com/v2
$port: !n 8080
# a normal comment in the variable section

base: $api
users: ${api}/users
port: $port
literal: !s $api
```

- `$name`: replace the whole value, type included
- `${name}`: interpolate in single-line strings only
- No interpolation inside `|…|` bodies
- Define before use; duplicates, undefined names, and cycles are errors
- Variables are not part of the output tree; they exist only until expansion

## Comments

`#` at the start of a structure line is a comment. No end-of-line comments (`#` after a value is content). `#` inside a multiline body is content.

## Parsing

1. Decode UTF-8; stop on failure
2. Split lines; read the variable section into an environment
3. Parse the document with an indent stack; `|` enters body mode until the matching closer
4. Expand `$name` / `${name}` on scalars
5. Bad indent, missing closer, duplicate keys, and illegal glyphs are hard errors; they are never repaired into the next field

Implementations must set limits. At least 64 nesting levels and 1MB files are recommended, to reject hostile input.

## Explicitly out of scope

- Implicit types (`yes` / `NO` / the Norway problem)
- Dotted paths `a.b.c`, YAML anchors `&*`, multi-documents `---`
- Untyped inline `[a, b]` (commas force quoting)
- `inf` / `nan`, `null`
- File includes
- `enum`, secrets, sets, and ranges `1..10` in the syntax (those belong to a schema)

`!url`, `!email`, `!re`, `!cron`, and similar stay unknown tags, not core.

## Status

The language rules have settled. A parser is not implemented yet.
