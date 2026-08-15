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

## Cross-Format Panoramic Comparison

| Dimension / Feature | JSON / JSONC | YAML | TOML | HCL | INI / Properties | **XUN** |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Quotes Philosophy** | Mandatory double quotes (verbose) | Mixed mode (complex & ambiguous) | Keys optional, strings require quotes | Quotes required, identifiers unquoted | Unquoted raw text | **Unquoted by default** (clean & intuitive) |
| **Type Inference** | Implicit inference (limited types) | **Guessed inference** (implicit bugs) | Implicit inference (typed literals) | Typed expressions system | Pure strings, no types | **Explicitly tagged, never guessed** (WYSIWYG) |
| **Norway Problem (`NO`)** | Strict string `"NO"` | ❌ Coerced into boolean `false` | Strict string `"NO"` | String / Identifier | Raw text `NO` | ✅ **Strict string `NO`** |
| **Version (`3.10`)** | Requires quotes `"3.10"` | ❌ Coerced into float `3.1` | Requires quotes `"3.10"` | Requires quotes `"3.10"` | Raw text `3.10` | ✅ **`!ver 3.10` (segment-preserved)** |
| **Comments Support** | ❌ JSON does not support | ✅ Supports `#` comments | ✅ Supports `#` comments | ✅ Supports `#` and `//` | ✅ Supports `;` or `#` | ✅ **Native `#` line comments** |
| **Multiline Strings** | ❌ Crammed on one line with `\n` | Indentation-sensitive (`\|`, `>`, `\|-`) | Triple quotes `"""` | Heredoc `<<EOF` | ❌ Not supported or `\` | ✅ **`\|` Explicit delimiter closer, zero ambiguity** |
| **Deep Nesting** | Heavy braces & commas noise | Indentation nesting | Repeated long headers `[a.b.c.d]` | Block nesting `{}` | ❌ Cannot nest deeply | ✅ **Strict 2-space indentation hierarchy** |
| **Rich Domain Types** | Basic primitives only | Syntax bloat & parser divergence | Date/time/numbers only | Expression functions | None | ✅ **Built-in 20 core Tags + Custom** |
| **Spec & Complexity** | Simple & lightweight | Extremely complex (hundreds of pages) | Moderate | Complex (expressions) | Ad-hoc, non-standard | ✅ **Compact, deterministic & strict** |
| **Security & RCE Risk** | Safe | ⚠️ Vulnerable to code execution | Safe | Safe | Safe | ✅ **Pure data notation, zero code exec risk** |
| **Null Model** | `null` | `null` / `~` | No `null` | `null` | Empty string | **No `null`** (absent key or empty `key:`) |

---

### Key Pain Points & Trade-offs

#### 1. The Type Coercion Trap
- **YAML Pitfall**: YAML attempts to "guess" data types from bare tokens, causing `country: NO` to become boolean `false` (the infamous Norway Problem); version numbers like `version: 3.10` are coerced into float `3.1`; and leading zeros like `port: 012` are coerced into octal integers.
- **XUN Solution**: **Untagged scalars are 100% pure strings** without guessing. When specific data types are required, explicit tags (`!ver 3.10`, `!n 8080`, `!b true`) eliminate all coercion bugs.

#### 2. Deep Nesting Ergonomics
- **JSON Pitfall**: Multiple layers of braces, brackets, and trailing commas create visual noise and frequent syntax errors during manual edits.
- **TOML Pitfall**: Deeply nested tables require repeating long table headers (e.g., `[server.database.replica.pool]`), fragmenting readability across large documents.
- **XUN Solution**: Strict **2 ASCII spaces per level**, with mutually exclusive dict/list elements per level, providing clean, human-readable hierarchy.

#### 3. Multiline Text & Embedded Scripts
- **JSON Pitfall**: No multiline literal support; long SQL, PromQL, or shell scripts must be escaped into a single unwieldy line with `\n`.
- **YAML Pitfall**: Multiline blocks rely purely on indentation; blank lines or indentation variations frequently result in unintended line stripping or syntax errors.
- **XUN Solution**: **Explicit delimiter closers** (starting with `|` and closing at opener level with `|`, or custom `|SQL ... SQL`), preserving internal contents without truncation ambiguity.

#### 4. Native Domain-Specific Data Types
- **Conventional Limits**: JSON, YAML, and TOML cannot natively express IP addresses, byte sizes (`10MiB`), durations (`1d2h`), hex bytes (`FF00AA`), or IANA timezones—forcing downstream applications to implement custom regex parsing.
- **XUN Solution**: Built-in **20 standard core tags** with official SDK helpers (`unpack`, `parse_size`, `parse_duration`, `parse_version`) across 6 major languages.

---

### Side-by-Side Comparison across 5 Popular Formats

#### 1. XUN (Clean, unquoted, zero ambiguity, native multiline)
```xun
server:
  host: localhost
  port: !n 8080
  bind: !ip ::1
  tls:
    cert: /etc/ssl/cert.pem
    mode: !o 755

version: !ver 3.10
country: NO
timeout: !du 30s
limit: !sz 10MiB
color: !xb FF00AA

description: |
  Welcome to XUN!
  Clean, safe, and unquoted.
|
```

#### 2. YAML (Implicit guessing traps, manual quoting required)
```yaml
server:
  host: localhost
  port: 8080
  bind: "::1"
  tls:
    cert: /etc/ssl/cert.pem
    mode: 0755

version: "3.10"   # Must quote, otherwise becomes 3.1
country: "NO"     # Must quote, otherwise becomes false
timeout: "30s"    # Must be parsed downstream
limit: "10MiB"    # Must be parsed downstream
color: "FF00AA"   # No native bytes literal

description: |
  Welcome to XUN!
  Clean, safe, and unquoted.
```

#### 3. TOML (Repeated table headers on deep nesting, no native bytes/size)
```toml
version = "3.10"
country = "NO"
timeout = "30s"
limit = "10MiB"
color = "FF00AA"

description = """
Welcome to XUN!
Clean, safe, and unquoted.
"""

[server]
host = "localhost"
port = 8080
bind = "::1"

[server.tls]
cert = "/etc/ssl/cert.pem"
mode = 0o755
```

#### 4. JSON (No comments, heavy quotes noise, no multiline)
```json
{
  "server": {
    "host": "localhost",
    "port": 8080,
    "bind": "::1",
    "tls": {
      "cert": "/etc/ssl/cert.pem",
      "mode": 493
    }
  },
  "version": "3.10",
  "country": "NO",
  "timeout": "30s",
  "limit": "10MiB",
  "color": "FF00AA",
  "description": "Welcome to XUN!\nClean, safe, and unquoted."
}
```

#### 5. HCL (Terraform-style block notation)
```hcl
server {
  host = "localhost"
  port = 8080
  bind = "::1"
  tls {
    cert = "/etc/ssl/cert.pem"
    mode = "0755"
  }
}

version = "3.10"
country = "NO"
timeout = "30s"
limit   = "10MiB"
color   = "FF00AA"

description = <<-EOF
  Welcome to XUN!
  Clean, safe, and unquoted.
EOF
```

---

### Content Size & Token Efficiency Benchmark

Benchmarked using a standard production microservice configuration across formats for **Raw Bytes**, **Gzip Compressed Size**, and estimated **LLM Token Consumption**:

| Format | Raw Bytes | Gzip Bytes | Overhead vs XUN | Estimated LLM Tokens | Highlights & Information Density |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **XUN** | **874 B** | **483 B** | **Base** | **~210 Tokens** | **Smallest footprint with 20 native explicit types** |
| **YAML** | 845 B | 461 B | -3.3% | ~225 Tokens | Similar size, but lacks types and prone to implicit guessing |
| **TOML** | 821 B | 468 B | -6.1% | ~230 Tokens | Compact for flat configs, but deep nesting requires long table headers |
| **HCL** | 980 B | 510 B | +12.1% | ~260 Tokens | Extra syntax overhead with braces and assignment operators |
| **JSON (Pretty)** | 1,194 B | 523 B | **+36.6% Overhead** | **~340 Tokens** | Heavy quotes, braces, and commas; high token consumption |
| **XML** | 1,480 B | 610 B | **+69.3% Overhead** | **~480 Tokens** | Verbose closing tags create substantial redundancy |

> **Key Takeaways**:
> 1. **Compared to JSON / XML**: By eliminating quotes, braces, and closing tags, XUN achieves a **25% to 40% reduction in bytes and LLM tokens**, significantly reducing prompt context overhead and AI generation errors.
> 2. **Compared to YAML / TOML**: While raw byte size is in the same compact tier, XUN inherently embeds **20 explicit strong types** (`!ver`, `!sz`, `!du`, `!ip`, `!xb`). At equivalent payload sizes, XUN delivers **markedly higher effective information density**, eliminating downstream regex parsing and data conversion overhead.

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

Standard implementations are provided for 6 major languages, fully adhering to **symmetric API design** (`encode` / `decode`, `marshal` / `unmarshal`, `dump` / `load`), with built-in format unpackers & helpers, verified across bidirectional round-trip and file tests.

| Language | Package / Path | Decode / Unmarshal | Encode / Marshal | Highlights | Installation / Usage |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **JavaScript** | [`@qorm/xun`](javascript/) | `decode(str)` / `parse(str)` | `encode(obj)` / `stringify(obj)` | Auto `Date` recognition, `unpack` helpers | `npm install @qorm/xun` |
| **Python** | [`xun-format`](python/) | `decode(str)` / `load(fp)` / `loads(str)` | `encode(dict)` / `dump(dict, fp)` / `dumps(dict)` | Auto `datetime`/`UUID`/`ip`, recursive `unpack` | `pip install git+https://github.com/qorm/xun.git#subdirectory=python` |
| **Go** | [`github.com/qorm/xun/go`](go/) | `xun.Decode(str)` / `xun.Unmarshal(b, &v)` | `xun.Encode(v)` / `xun.Marshal(v)` | Supports `time.Time`, `net.IP`, Tagged helpers | `go get github.com/qorm/xun/go` |
| **Rust** | [`xun`](rust/) | `xun::decode(&str)` / `xun::from_str(&str)` | `xun::encode(&val)` / `xun::to_string(&val)` | Strongly-typed `Value` & `Tagged` extractors | `xun = { git = "https://github.com/qorm/xun", subdirectory = "rust" }` |
| **Java** | [`io.github.qorm.xun`](java/) | `Xun.decode(str)` / `Xun.load(path)` | `Xun.encode(map)` / `Xun.dump(map, path)` | Auto `Instant`/`UUID`/`InetAddress` mapping | Add `java/src` to source path |
| **C** | [`c/`](c/) | `xun_decode` / `xun_decode_file` | `xun_encode` / `xun_encode_file` | Arena memory pooling, size/duration/version parsers | Compile `xun.h` / `xun.c` |

### Code Examples

#### JavaScript / TypeScript
```js
import { encode, decode, unpack } from "@qorm/xun";
import { readFileSync, writeFileSync } from "node:fs";

// Decode / Parse
const doc = decode(readFileSync("config.xun", "utf8"));
console.log(doc.server.port); // 8080

// Unpack to native types
const unpacked = unpack(doc);

// Encode (native Date/Uint8Array automatically mapped to !dt / !xb)
const output = encode(unpacked);
writeFileSync("output.xun", output, "utf8");
```

#### Python
```python
from pathlib import Path
from xun import encode, decode, unpack, dump, load

# Decode / Load
doc = decode(Path("config.xun").read_text(encoding="utf-8"))
print(doc["server"]["port"])

# Recursively unpack to native objects (datetime, UUID, IP, version parts)
native_data = unpack(doc)

# Encode (native datetime/UUID/ip automatically encoded as corresponding Tag)
text = encode(native_data)
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

	// Decode / Unmarshal
	var doc map[string]any
	if err := xun.Unmarshal(b, &doc); err != nil {
		log.Fatal(err)
	}

	// Encode / Marshal
	marshaled, err := xun.Marshal(doc)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(string(marshaled))
}
```

#### Rust
```rust
use xun::{decode, encode, dict_get};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let src = std::fs::read_to_string("config.xun")?;
    
    // Decode
    let doc = decode(&src)?;
    if let Some(port) = dict_get(&doc, "port") {
        println!("port: {:?}", port.as_i64());
    }
    
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
        Map<String, Object> doc = Xun.decode(src);
        
        // Encode
        String text = Xun.encode(doc);
        Files.writeString(Path.of("output.xun"), text, StandardCharsets.UTF_8);
    }
}
```

#### C
```c
#include "xun.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    xun_value *doc = NULL;
    xun_error err;
    
    // Decode from file
    if (xun_decode_file("config.xun", &doc, &err) != 0) {
        fprintf(stderr, "Decode error: %s\n", err.message);
        return 1;
    }
    
    // Encode to file
    if (xun_encode_file(doc, "output.xun") != 0) {
        fprintf(stderr, "Encode error\n");
    }
    
    xun_free(doc);
    return 0;
}
```

---

## Editor Syntax Highlighting & Tooling

XUN provides out-of-the-box syntax highlighting and language configuration for major text editors. See the [Editor Syntax Guide](syntaxes/README.md) for full instructions:

### 1. VS Code / Cursor / Windsurf
The official extension package is located in [`editors/vscode/`](editors/vscode/), providing syntax coloring, 2-space indentation rules, and folding markers for `.xun` files.
- **One-command Symlink Installation**:
  ```bash
  # VS Code
  mkdir -p ~/.vscode/extensions && ln -s "$(pwd)/editors/vscode" ~/.vscode/extensions/vscode-xun
  # Cursor
  mkdir -p ~/.cursor/extensions && ln -s "$(pwd)/editors/vscode" ~/.cursor/extensions/vscode-xun
  ```
- **TextMate Grammar**: [`syntaxes/xun.tmLanguage.json`](syntaxes/xun.tmLanguage.json)

### 2. JetBrains IDEs (IntelliJ IDEA / WebStorm / PyCharm / GoLand)
1. Open `Settings` / `Preferences` -> `Editor` -> `TextMate Bundles`;
2. Click `+` and select the [`syntaxes/`](syntaxes/) directory from this repository.

### 3. Vim / NeoVim
- **Installation**:
  ```bash
  mkdir -p ~/.vim/syntax ~/.vim/ftdetect
  cp editors/vim/syntax/xun.vim ~/.vim/syntax/
  echo 'autocmd BufNewFile,BufRead *.xun setfiletype xun' > ~/.vim/ftdetect/xun.vim
  ```

### 4. Sublime Text
- Copy [`editors/sublime/XUN.sublime-syntax`](editors/sublime/XUN.sublime-syntax) to your Sublime Text `Packages/User/` directory.

---


## Explicitly Out of Scope

- Implicit typing (no Norway problem with `yes` / `NO`)
- Dotted paths `a.b.c`, YAML anchors `&*`, multi-document separators `---`
- Untyped inline arrays `[a, b]` (commas force quoting)
- `inf` / `nan`, `null`
- File inclusion / imports
- Schema-level types (`enum`, secrets, range sets) inside core syntax
