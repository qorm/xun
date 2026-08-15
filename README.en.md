# XUN

[English](README.en.md) · [中文](README.md)

XUN (pronounced “shün”, like Chinese 讯) is a modern configuration notation designed for human readability and unambiguous machine parsing: **unquoted by default**, **types explicitly tagged (`!tag`)**, and **strict 2-space indentation per level**. The name stands for **X Unquoted Notation**.

- File extension: `.xun`
- Media type: `text/xun`
- Language identifier: `xun`

Compared with JSON: eliminates repetitive quotes, supports comments, and provides clean native multiline blocks.  
Compared with YAML: never guesses types (`3.10` is never coerced into float `3.1`, `yes`/`NO` are strictly strings), multiline blocks must be explicitly closed, with no implicit typing pitfalls.  
Compared with TOML: deep nesting relies naturally on indentation without repeating long table headers `[a.b.c.d]`.

---

## Example

```xun
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

ports: !n[80, 443, 8080]
endpoint: https://api.example.com/v2/orders
tz: !tz Asia/Shanghai
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

---

## Core Specification

### 1. File and Encoding
- **UTF-8 Only**: The entire file must be valid UTF-8. Invalid byte sequences or `NUL` (U+0000) trigger an immediate fatal parse error.
- **BOM**: Permitted only at the beginning of the file (U+FEFF); stripped on read.
- **Newlines**: LF (`\n`), CRLF (`\r\n`), and CR (`\r`) are recognized and normalized to LF internally.
- **Strict Indentation**: ASCII space (U+0020) only, **exactly 2 spaces per indentation level**. Tabs, skipped levels, and odd indentation counts are errors.
- **Root Node**: **Must be a dictionary**. An empty file or a comments-only file evaluates to `{}`.

### 2. Structure and Containers
XUN has three kinds of nodes: **Dictionary**, **List**, and **Scalar**.

- **Dictionary Pairs**: Separator must be `: ` (colon followed by a space) or a trailing `:` at the end of the line.
  - `key: value` (valid)
  - `key:value` (**invalid**, missing space after colon)
  - Keys cannot be empty, cannot contain newlines, and cannot contain `: `. Duplicate keys in the same container are fatal errors.
- **List Items**: Start with `- ` or a standalone `-`.
- **Container Exclusivity**: A container cannot mix `-` list items and `key:` dictionary pairs at the same level.
- **Explicit Empty Containers**: Empty dictionary is `{}`; empty list is `[]`.

```xun
# Nested dictionary
database:
  host: 127.0.0.1
  port: !n 5432

# List
users:
  - alice
  - bob

# Empty containers
empty_map: {}
empty_list: []
```

### 3. Explicit Type System

**Any scalar without an explicit `!tag` is strictly a string. Parsers never guess.**  
Values like `8080`, `true`, `false`, `NO`, and `3.10` are all strings unless prefixed with a tag.

| Tag | Meaning | Valid Form Examples | Invalid Examples / Notes |
| :--- | :--- | :--- | :--- |
| (none) / `!s` | String | `hello world`, `!s !special` | `!s` used when value starts with `!` |
| `!n` | Number (general) | `8080`, `-12`, `3.14`, `1e-3` | `012` (no leading zeros), `1_000` (underscores allowed) |
| `!i` | Integer | `8080`, `-3`, `1_000` | `1.5`, out-of-range i64 |
| `!f` | Float | Must contain `.` or `e`: `1.5`, `8080.0`, `1e3` | `!f 8080` (missing dot or exponent) |
| `!x` | Hex Integer | `DEAD_BEEF`, `0xFF` | Non-hex characters |
| `!xb` | Hex Bytes | `FF00AA` (must be even length) | `F0A` (odd length is illegal) |
| `!o` | Octal (file mode) | `755`, `0644` | Digits `8` or `9` |
| `!b` | Boolean | `true` or `false` only | `yes`, `1`, `True`, `ON` |
| `!d` | Date | `2026-08-14` (`YYYY-MM-DD`) | `2026/08/14` |
| `!t` | Time | `16:54`, `16:54:00`, `16:54:00.123` | `4pm` |
| `!dt` | Date-Time with TZ | `2026-08-14T16:54:00+08:00`, `...Z` | Missing timezone offset |
| `!tz` | Timezone | IANA name (`Asia/Shanghai`), `Z`, `+08:00` | `CST` (ambiguous abbreviation) |
| `!du` | Duration | `1d2h30m15s`, `500ms`, `10s` | `90 minutes` |
| `!sz` | Data Size | `10MiB`, `3KB`, `1024B` | `10m` |
| `!unix` | Unix Epoch Seconds | `1692000000` | Leading zeros |
| `!ver` | Semantic Version | `3.10` (stored by segment, not float), `1.2.3` | `3.10.beta` |
| `!uuid` | UUID | `8-4-4-4-12` format with hyphens | Missing hyphens |
| `!ip` | IP Address | IPv4 `127.0.0.1`, IPv6 `::1` | With port (e.g. `127.0.0.1:80`) |
| `!b64` | Base64 Bytes | `SGVsbG8=` | Illegal Base64 characters |
| `!c` | Single Unicode Character | Single scalar `a` or codepoint `U+000A` | Multiple scalars `ab` |

- **Unknown Tags**: Tags like `!sql`, `!md`, `!custom` are valid. The parser preserves the tag name and raw text for upstream applications.
- **No `null`**: Absence of a key represents missing value. An empty string is written as `key:` with no trailing subtree.

### 4. Arrays (Compact and Block Form)

- **Compact Array**: The type tag is placed directly on the brackets `!tag[...]`, with elements separated by commas (elements cannot contain commas).
  ```xun
  ports: !n[80, 443, 8080]
  vowels: !c[a, e, i]
  peers: !ip[127.0.0.1, ::1]
  py_versions: !ver[3.10, 3.11]
  ```
- **Block Form Array**:
  - String arrays **must** use block form (to avoid comma splitting ambiguity):
    ```xun
    roles: !s[]
      - admin
      - ops
      - hello, world
    ```
  - Standard untyped list:
    ```xun
    items:
      - item1
      - item2
    ```

### 5. Multiline Blocks

XUN uses explicit delimiters to terminate multiline blocks, avoiding indentation ambiguity:

```xun
banner: |
  Line 1
  Line 2
|

query: !sql |
  SELECT id, name
  FROM users
  WHERE active = true
|
```

- **Open**: Value slot has `|` or `!tag |` (or custom closing tag like `|MD`).
- **Close**: On a line with the **same indentation** as the opening key, write `|` (or the corresponding tag like `MD`).
- **Body Indent**: Each line of the body is indented 2 spaces beyond the opening key; these 2 baseline spaces are stripped during parsing.
- **Literal Contents**: All characters inside the multiline body (including `#`, `:`, `-`, `!`) are preserved literally.

---

## AI & Developer Authoring Guidelines (Do's & Don'ts)

Follow these rules to ensure 100% compliant XUN generation:

### Golden Rules Checklist
1. **Strict 2-Space Indent**: Never use tabs; indent exactly 2 spaces per level.
2. **Colon Followed by Space**: Write `key: value`, never `key:value`.
3. **No Quotes by Default**: Do not wrap strings in `"` or `'`. Writing `name: "Alice"` will keep the quotes as part of the string value.
4. **Explicit Type Tags**: Use `!n 8080` / `!i 8080` for numbers, `!b true` for booleans; otherwise they remain strings.
5. **Always Close Multiline Blocks**: Every multiline block starting with `|` must end with `|` at the matching indentation level.
6. **Explicit Empty Containers**: Write `{}` for empty dictionaries, `[]` for empty lists.
7. **No Null**: Omit the key entirely or use `key:` for an empty string.

### Pattern Comparison (Do's & Don'ts)

| Scenario | ❌ Incorrect | ✅ Correct | Reason |
| :--- | :--- | :--- | :--- |
| **Colon space** | `port:8080` | `port: !n 8080` | Space required after `:` |
| **String quotes** | `name: "Alice"` | `name: Alice` | Quotes are preserved as literal characters |
| **Numeric value** | `count: 10` (wants integer) | `count: !i 10` or `!n 10` | Untagged scalar is parsed as string `"10"` |
| **Boolean value** | `enabled: true` (wants bool) | `enabled: !b true` | Untagged `true` is parsed as string `"true"` |
| **Float value** | `rate: !f 100` | `rate: !f 100.0` | `!f` requires a dot `.` or exponent `e` |
| **String array** | `tags: !s[a, b]` | `tags: !s[]`<br>`  - a`<br>`  - b` | String arrays cannot use compact comma form |
| **Multiline body** | `desc: \|`<br>`  hello` (unclosed) | `desc: \|`<br>`  hello`<br>`\|` | Multiline block must have matching closing `\|` |
| **Empty dict** | `meta:` (empty subtree) | `meta: {}` | Empty dict must be written `{}` explicitly |

---

## Official Libraries & API

Standard implementations are provided for 6 major languages, each with complete **Decoders (`parse`)** and **Encoders (`encode`)**, verified with bidirectional round-trip test suites.

| Language | Package / Path | Decode | Encode | Installation / Usage |
| :--- | :--- | :--- | :--- | :--- |
| **JavaScript** | [`@qorm/xun`](javascript/) | `parse(str)` | `encode(obj)` / `stringify(obj)` | `npm install @qorm/xun` |
| **Python** | [`xun-format`](python/) | `parse(str)` | `encode(dict)` / `dump(dict, fp)` / `dumps(dict)` | `pip install git+https://github.com/qorm/xun.git#subdirectory=python` |
| **Go** | [`github.com/qorm/xun/go`](go/) | `xun.Parse(str)` | `xun.Encode(v)` / `xun.Marshal(v)` | `go get github.com/qorm/xun/go` |
| **Rust** | [`xun`](rust/) | `xun::parse(&str)` | `xun::encode(&val)` / `xun::to_string(&val)` | `xun = { git = "https://github.com/qorm/xun", subdirectory = "rust" }` |
| **Java** | [`io.github.qorm.xun`](java/) | `Xun.parse(str)` | `Xun.encode(map)` / `Xun.dump(map)` | Add `java/src` to source path |
| **C** | [`c/`](c/) | `xun_parse` / `xun_parse_file` | `xun_encode` / `xun_encode_file` | Compile `xun.h` / `xun.c` |

### Code Examples

#### JavaScript / TypeScript
```js
import { parse, encode } from "@qorm/xun";
import { readFileSync, writeFileSync } from "node:fs";

// Decode
const doc = parse(readFileSync("config.xun", "utf8"));
console.log(doc.server.port); // 8080

// Encode
const output = encode(doc);
writeFileSync("output.xun", output, "utf8");
```

#### Python
```python
from pathlib import Path
from xun import parse, encode

# Decode
doc = parse(Path("config.xun").read_text(encoding="utf-8"))
print(doc["server"]["port"])

# Encode
text = encode(doc)
Path("output.xun").write_text(text, encoding="utf-8")
```

#### Go
```go
package main

import (
	"fmt"
	"log"
	"os"

	"github.com/qorm/xun/go"
)

func main() {
	b, err := os.ReadFile("config.xun")
	if err != nil {
		log.Fatal(err)
	}

	// Decode
	doc, err := xun.Parse(string(b))
	if err != nil {
		log.Fatal(err)
	}

	// Encode
	encoded, err := xun.Encode(doc)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(encoded)
}
```

#### Rust
```rust
use xun::{parse, encode};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let src = std::fs::read_to_string("config.xun")?;
    
    // Decode
    let doc = parse(&src)?;
    
    // Encode
    let text = encode(&doc)?;
    println!("{}", text);
    Ok(())
}
```

#### Java
```java
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import io.github.qorm.xun.Xun;

public class Main {
    public static void main(String[] args) throws Exception {
        String src = Files.readString(Path.of("config.xun"), StandardCharsets.UTF_8);
        
        // Decode
        Map<String, Object> doc = Xun.parse(src);
        
        // Encode
        String text = Xun.encode(doc);
        System.out.println(text);
    }
}
```

#### C
```c
#include <stdio.h>
#include <stdlib.h>
#include "xun.h"

int main(void) {
    xun_value *doc = NULL;
    xun_error err;

    // Decode
    if (xun_parse_file("config.xun", &doc, &err) != 0) {
        fprintf(stderr, "line %d: %s\n", err.line, err.message);
        return 1;
    }

    // Encode
    char *text = NULL;
    size_t len = 0;
    if (xun_encode(doc, &text, &len) == 0) {
        printf("%s", text);
        free(text);
    }

    xun_free(doc);
    return 0;
}
```

---

## Explicitly Out of Scope

- Implicit typing (no Norway problem with `yes` / `NO`)
- Dotted paths `a.b.c`, YAML anchors `&*`, multi-document separators `---`
- Untyped inline arrays `[a, b]` (commas force quoting)
- `inf` / `nan`, `null`
- File inclusion / imports
- Schema-level types (`enum`, secrets, range sets) inside core syntax
