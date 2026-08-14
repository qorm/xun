# @qorm/xun

Parser for [XUN](https://github.com/qorm/xun) (X Unquoted Notation).

```bash
npm install @qorm/xun
```

```js
import { readFileSync } from "node:fs";
import { parse } from "@qorm/xun";

const doc = parse(readFileSync("config.xun", "utf8"));
```

`parse` takes the file contents as a UTF-8 string, not a path.
