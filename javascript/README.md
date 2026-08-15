# @qorm/xun

Parser and encoder for [XUN](https://github.com/qorm/xun) (X Unquoted Notation).

```bash
npm install @qorm/xun
```

```js
import { readFileSync, writeFileSync } from "node:fs";
import { parse, encode } from "@qorm/xun";

// Parse
const doc = parse(readFileSync("config.xun", "utf8"));

// Encode
const text = encode(doc);
writeFileSync("output.xun", text, "utf8");
```
