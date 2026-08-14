# @qorm/xun

Parser for [XUN](https://github.com/qorm/xun) (X Unquoted Notation).

```bash
npm install @qorm/xun
```

```js
import { parse } from "@qorm/xun";

const doc = parse(`
host: localhost
port: !n 8080
`);
```
