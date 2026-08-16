# xun-format

Python implementation of XUN (X Unquoted Notation) — a modern configuration
notation that is unquoted by default, explicitly typed via `!tag`, and strictly
indented with 2 spaces per level.

See the [full specification](../../README.md) for the syntax, the 20 core
tags, and cross-language documentation.

```python
from pathlib import Path
from xun import encode, decode, unpack, dump, load

doc = decode(Path("config.xun").read_text(encoding="utf-8"))
print(doc["server"]["port"])

native_data = unpack(doc)
text = encode(native_data)
Path("output.xun").write_text(text, encoding="utf-8")
```

Install locally:

```bash
pip install -e .
```
