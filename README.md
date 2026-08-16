# XUN

[English](README.en.md) · [中文](README.md)

XUN（读作「讯」）是一种为人编写、为机器解析而设计的现代配置记法：**默认不加引号**，**类型显式标出（`!tag`）**，**缩进严格固定每层 2 个空格**。全称 **X Unquoted Notation**。

- 文件扩展名：`.xun`
- 媒体类型：`text/xun`
- 语言标识：`xun`

相对 JSON：少写大量引号、支持注释、原生支持整洁的多行文本块。  
相对 YAML：不猜测类型（`3.10` 绝对不会变成浮点数 `3.1`，`yes`/`NO` 纯粹是字符串），多行块必须显式收尾，无复杂隐式陷阱。  
相对 TOML：深层嵌套直接依靠缩进表达，无需重复书写长路径表头 `[a.b.c.d]`。

---

## 主流配置格式横向全景对比 (Cross-Format Comparison)

| 维度 / 特性 | JSON / JSONC | YAML | TOML | HCL | INI / Properties | **XUN** |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **引号哲学** | 强制全双引号（繁琐） | 混合模式（规则复杂易错） | 键名可选，字符串必须双/单引号 | 必须引号，特定标识符无引号 | 纯文本无引号 | **默认无引号**（纯净直观） |
| **类型推断机制** | 隐式推断（类型匮乏） | **猜测推断**（隐式 bug 多发） | 隐式推断（强类型字面量） | 表达式/强类型系统 | 纯字符串无类型 | **显式标出，绝不猜测**（所见即所得） |
| **挪威问题 (`NO`)** | 正常字符串 `"NO"` | ❌ 误判为布尔值 `false` | 正常字符串 `"NO"` | 正常标识符/字符串 | 纯文本 `NO` | ✅ **严格纯字符串 `NO`** |
| **版本号 (`3.10`)** | 需引号 `"3.10"` | ❌ 误判为浮点数 `3.1` | 需引号 `"3.10"` | 需引号 `"3.10"` | 纯文本 `3.10` | ✅ **`!ver 3.10`（按段精确保留）** |
| **注释支持** | ❌ JSON 完全不支持 | ✅ 支持 `#` 注释 | ✅ 支持 `#` 注释 | ✅ 支持 `#` 与 `//` | ✅ 支持 `;` 或 `#` | ✅ **原生支持行首 `#` 注释** |
| **多行文本块** | ❌ 只能用 `\n` 挤在一行 | 缩进复杂易错（`\|`, `>`, `\|-`） | 支持三引号 `"""` | 支持 Heredoc `<<EOF` | ❌ 不支持或需行尾 `\` | ✅ **`\|` 显式定界闭合，零歧义** |
| **深层嵌套表达** | 大括号与逗号嵌套（噪音大） | 缩进层级表达 | 重复长表头 `[a.b.c.d]`（繁琐碎片化） | 块嵌套 `{}` | ❌ 无法表达深层嵌套 | ✅ **严格 2 格缩进，层次分明** |
| **丰富领域类型** | 仅基础数字/字符串/布尔 | 规范膨胀且各语言不一致 | 仅支持时间/日期/数字 | 依赖表达式函数 | 无 | ✅ **内置 20 种核心 Tag + 自定义** |
| **规范与实现复杂度** | 简单轻量 | 极其庞大繁复（数百页规范） | 中等 | 复杂（含表达式与函数） | 极其简单但无统一标准 | ✅ **精简收敛、确定性高** |
| **反序列化安全** | 安全 | ⚠️ 易发生代码执行 RCE 漏洞 | 安全 | 安全 | 安全 | ✅ **纯数据描述，零代码执行风险** |
| **空值模型** | `null` | `null` / `~` | 无 `null` | `null` | 空字符串 | **无 `null`**（缺失不写，空串 `key:`） |

---

### 核心设计痛点深度对比

#### 1. 类型推断的“幽灵陷阱” (The Type Coercion Trap)
- **YAML 痛点**：YAML 试图通过值字面量智能“猜”类型，导致 `country: NO` 会被静默解析为布尔 `false`（著名的挪威问题）；版本号 `version: 3.10` 会被自动当成浮点数变成 `3.1`；前导零数字 `port: 012` 会被当作八进制解析。
- **XUN 方案**：**未标 Tag 的标量 100% 作为纯文本字符串**，绝不进行隐式猜测。需要特定类型时使用显式 Tag（如 `!ver 3.10`、`!n 8080`、`!b true`），消除一切歧义与隐式 Bug。

#### 2. 深层嵌套与视觉噪音 (Nesting Ergonomics)
- **JSON 痛点**：多层大括号、方括号和尾部逗号带来大量视觉噪音，手动编辑极易丢失逗号导致解析失败。
- **TOML 痛点**：对于深层嵌套结构，TOML 必须不断重复长路径表头（例如 `[server.database.replica.pool]`），不仅割裂了层次感，且在大型配置中极易产生跨章节的跳转阅读负担。
- **XUN 方案**：继承缩进配置的清晰直观，统一规定 **每层严格 2 个空格**，同层字典/列表互斥，层次分明。

#### 3. 多行文本与脚本嵌入 (Multiline Text & Scripts)
- **JSON 痛点**：不支持多行文本，长 SQL、PromQL 或 Shell 脚本只能转义为包含 `\n` 的单行长字符串。
- **YAML 痛点**：YAML 的多行文本仅依靠缩进判断结束，包含空行、注释或复杂缩进代码时，极易因解析器差异发生裁切错误。
- **XUN 方案**：引入 **显式定界闭合符**（如 `|` 开始并以同级 `|` 结束，或支持自定义标签 `|SQL ... SQL`），内部缩进按 +2 格保留，无缩进截断歧义。

#### 4. 原生高阶领域类型 (Rich Domain Types)
- **常规格式现状**：JSON、YAML、TOML 无法在格式层表达 IP 地址、数据容量（`10MiB`）、时长（`1d2h`）、十六进制字节（`FF00AA`）、IANA 时区等，上层业务必须自行写正则二次解析字符串。
- **XUN 方案**：原生内置 **20 种常用核心类型 Tag**，并允许通过 `!custom` 扩展，6 门语言的官方库均提供开箱即用的解析与类型提取（Unpack）辅助方法。

---

### 同一配置在 5 种主流格式下的真实代码对比

#### 1. XUN（清爽、无引号、类型零歧义、支持原生多行）
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

#### 2. YAML（隐式推断陷阱多，必须手动给版本与特殊词加引号）
```yaml
server:
  host: localhost
  port: 8080
  bind: "::1"
  tls:
    cert: /etc/ssl/cert.pem
    mode: 0755

version: "3.10"   # 必须加引号，否则变成 3.1
country: "NO"     # 必须加引号，否则变成 false (挪威问题)
timeout: "30s"    # 需上层自行解析
limit: "10MiB"    # 需上层自行解析
color: "FF00AA"   # 无原生字节类型

description: |
  Welcome to XUN!
  Clean, safe, and unquoted.
```

#### 3. TOML（深层嵌套需重复长表头，不支持原生字节/容量）
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

#### 4. JSON（无注释、大量引号噪音、不支持原生多行）
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

#### 5. HCL（Terraform 风格块结构）
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

### 内容体积与 Token 消耗实测对比 (Content Size & Token Efficiency)

以标准微服务集群配置为基准，对各格式在**原始字节体积 (Raw Bytes)**、**Gzip 压缩体积**及 **LLM Token 消耗**进行了精确统计：

| 格式 | 原始体积 (Raw Bytes) | Gzip 体积 | 相对 XUN 体积冗余 | 预估 LLM Token 消耗 | 特点与信息密度 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **XUN** | **874 B** | **483 B** | **基准 (Base)** | **~210 Tokens** | **体积最小梯队，且原生携带 20 种显式强类型** |
| **YAML** | 845 B | 461 B | -3.3% | ~225 Tokens | 体积相当，但无类型标注，充满隐式猜测风险 |
| **TOML** | 821 B | 468 B | -6.1% | ~230 Tokens | 扁平配置紧凑，但深层嵌套需重复长路径表头 |
| **HCL** | 980 B | 510 B | +12.1% | ~260 Tokens | 大量花括号与等号，有额外符号开销 |
| **JSON (Pretty)** | 1,194 B | 523 B | **+36.6% 冗余** | **~340 Tokens** | 充斥引号、大括号与逗号，Token 消耗极高 |
| **XML** | 1,480 B | 610 B | **+69.3% 冗余** | **~480 Tokens** | 闭合标签带来严重体积冗余 |

> **关键洞察**：
> 1. **相对 JSON / XML**：XUN 彻底去除所有不必要的引号、括号和闭合标签，在相同配置下**节省了 25% ~ 40% 的体积与 LLM Token**，极大降低了大模型上下文开销与生成出错率。
> 2. **相对 YAML / TOML**：虽然纯字符体积处于同一紧凑梯队，但 XUN 额外携带了 `!ver`, `!sz`, `!du`, `!ip`, `!xb` 等 **20 种明确的强类型语义**。在同等甚至更小的数据载荷下，XUN 的**有效信息密度（Information Density）显著更高**，免去了上层业务二次正则校验与格式转换的隐形成本。

---

## 示例

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

## 核心语法规范

### 1. 文件与编码
- **UTF-8 编码**：整文件必须为合法 UTF-8。遇到非法字节或 `NUL`（U+0000）直接报错解析失败。
- **BOM**：仅允许出现在文件最开头（U+FEFF），读入时自动忽略。
- **换行符**：支持 LF (`\n`)、CRLF (`\r\n`) 与 CR (`\r`)，读入后内部统一规范为 LF。
- **严格缩进**：仅识别 ASCII 空格（U+0020），**每级缩进严格为 2 个空格**。禁止 Tab、禁止奇数空格、禁止跳级缩进。
- **根节点**：**必须是字典**。空文件或仅含注释的文件等价于空字典 `{}`。

### 2. 结构与容器
XUN 中只有三种节点：**字典（Dictionary）**、**列表（List）** 和 **标量（Scalar）**。

- **字典键值对**：分隔符必须是 `: `（冒号后跟空格）或行尾单独的 `:`。
  - `key: value`（正确）
  - `key:value`（**非法**，冒号后缺少空格）
  - 键名不能为空，不能包含换行，不能包含 `: `。同一层级字典中键名不可重复。
- **列表项**：以 `- ` 或单独的 `-` 开头。
- **同层容器互斥**：同一层级内要么全部是字典键值对，要么全部是列表项，严禁混用。
- **显式空容器**：空字典写为 `{}`，空列表写为 `[]`。

```xun
# 嵌套字典
database:
  host: 127.0.0.1
  port: !n 5432

# 列表
users:
  - alice
  - bob

# 显式空容器
empty_map: {}
empty_list: []
```

### 3. 类型系统（显式标注）

**无 `!tag` 的值一律为纯字符串，解析器绝不猜测类型。**  
例如：`8080`、`true`、`false`、`NO`、`3.10` 在没有 tag 时均为普通字符串。需要特定类型时，必须在值前显式声明 tag 并以空格分隔（紧凑数组除外）。

| Tag | 类型含义 | 合法字形示例 | 非法示例 / 说明 |
| :--- | :--- | :--- | :--- |
| （无） / `!s` | 字符串 | `hello world`，`!s !special` | `!s` 用于显式声明或值以 `!` 开头 |
| `!n` | 数字（通用） | `8080`、`-12`、`3.14`、`1e-3` | `012`（禁止前导零）、`1_000` 支持下划线 |
| `!i` | 整数 | `8080`、`-3`、`1_000` | `1.5`、超出 64 位整型范围 |
| `!f` | 浮点数 | 必须含 `.` 或 `e`：`1.5`、`8080.0`、`1e3` | `!f 8080`（缺少小数点或指数） |
| `!x` | 十六进制整数 | `DEAD_BEEF`、`0xFF` | 非十六进制字符 |
| `!xb` | 十六进制字节序列 | `FF00AA`（必须为偶数位） | `F0A`（奇数位非法） |
| `!o` | 八进制（如权限） | `755`、`0644` | 含有 `8` 或 `9` |
| `!b` | 布尔值 | 仅 `true` 或 `false` | `yes`、`1`、`True`、`ON` |
| `!d` | 日期 | `2026-08-14`（`YYYY-MM-DD`） | `2026/08/14` |
| `!t` | 时间 | `16:54`、`16:54:00`、`16:54:00.123` | `4pm` |
| `!dt` | 带时区日期时间 | `2026-08-14T16:54:00+08:00`、`...Z` | 缺少时区偏移 |
| `!tz` | 时区标识 | IANA 时区（如 `Asia/Shanghai`）、`Z`、`+08:00` | `CST`（歧义缩写） |
| `!du` | 时间跨度/时长 | `1d2h30m15s`、`500ms`、`10s` | `90 minutes` |
| `!sz` | 数据容量大小 | `10MiB`、`3KB`、`1024B` | `10m` |
| `!unix` | Unix 纪元时间戳 | `1692000000` | 带前导零 |
| `!ver` | 语义版本号 | `3.10`（按段存储，不作浮点解析）、`1.2.3` | `3.10.beta` |
| `!uuid` | UUID | `8-4-4-4-12` 格式带连字符 | 缺少连字符 |
| `!ip` | IP 地址 | IPv4 `127.0.0.1`、IPv6 `::1` | 带端口号（如 `127.0.0.1:80`） |
| `!b64` | Base64 字节 | `SGVsbG8=` | 非法 Base64 字符 |
| `!c` | 单个 Unicode 字符 | 单个字符 `a` 或码点 `U+000A` | 多个字符 `ab` |

- **未知 Tag**：如 `!sql`、`!md`、`!custom` 均为合法语法。解析器会保留 Tag 名称及原始文本，供上层应用处理。
- **无 `null`**：字段缺失请直接不写该键；空字符串写作 `key:`（后无子项）。

### 4. 数组（紧凑与列表形式）

- **紧凑数组**：类型标在整个中括号前，元素间用逗号分隔（元素内部不可含逗号）。
  ```xun
  ports: !n[80, 443, 8080]
  vowels: !c[a, e, i]
  peers: !ip[127.0.0.1, ::1]
  py_versions: !ver[3.10, 3.11]
  ```
- **块形式数组**：
  - 字符串数组**必须**使用块形式（防止逗号切分歧义）：
    ```xun
    roles: !s[]
      - admin
      - ops
      - hello, world
    ```
  - 普通未标类型列表：
    ```xun
    items:
      - item1
      - item2
    ```

### 5. 多行文本块

XUN 使用严格的定界符结束多行块，杜绝缩进歧义：

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

- **开始标记**：值位置写 `|` 或 `!tag |`（或自定义闭合标识如 `|MD`）。
- **结束标记**：在与开块**相同缩进**的一行，单独写 `|`（或对应的闭合标识如 `MD`）。
- **正文缩进**：正文每行比开块所在行多缩进 2 个空格，解析时会自动去除这 2 个基础空格。
- **字面量保证**：多行块内部的所有字符（包括 `#`、`:`、`-`、`!` 等）均作为普通字符原样保留。

---

## AI 与开发者编写准则 (AI Guidelines & Cheatsheet)

为了确保 AI 模型和自动化工具能够 100% 正确生成合法的 XUN 配置，请遵循以下核心原则：

### 黄金规则清单
1. **严格 2 格空格缩进**：禁止使用 Tab，每级深度正好 2 个空格。
2. **冒号后必须有空格**：字典键值对写 `key: value`，绝对不要写成 `key:value`。
3. **默认不加引号**：不要给字符串加 `"` 或 `'`。如果你写了 `name: "Alice"`，双引号会变成字符串内容的一部分。（**编码时**，6 种语言的编码器会自动去掉字符串左右成对的双引号，JSON 风格的 `"Alice"` 会被输出为 `Alice`。）
4. **显式类型 Tag**：数字如果要当数字，请写 `!n 8080` 或 `!i 8080`；布尔值请写 `!b true`；否则它们都是字符串。
5. **多行块必须闭合**：以 `|` 开始的多行文本块，必须在同级缩进以 `|` 显式闭合。
6. **空容器显式书写**：空字典写 `{}`，空列表写 `[]`。
7. **没有 null**：不需要的字段直接省略，或者用 `key:` 表示空字符串。

### 正确与错误模式对比 (Do's & Don'ts)

| 场景 | ❌ 错误写法 | ✅ 正确写法 | 为什么 |
| :--- | :--- | :--- | :--- |
| **冒号空格** | `port:8080` | `port: !n 8080` | XUN 规定冒号后必须有空格 |
| **字符串引号** | `name: "Alice"` | `name: Alice` | XUN 默认不加引号，加了会保留引号字符；编码时会自动剥掉左右成对的双引号 |
| **数字类型** | `count: 10` （期望整数） | `count: !i 10` 或 `!n 10` | 未标注 tag 的标量一律解析为字符串 `"10"` |
| **布尔类型** | `enabled: true` （期望布尔） | `enabled: !b true` | 未标注 tag 的 `true` 解析为字符串 `"true"` |
| **浮点数** | `rate: !f 100` | `rate: !f 100.0` | `!f` 必须包含小数点 `.` 或指数 `e` |
| **字符串数组** | `tags: !s[a, b]` | `tags: !s[]`<br>`  - a`<br>`  - b` | 字符串数组禁止使用紧凑逗号形式 |
| **多行文本** | `desc: \|`<br>`  hello` （未闭合） | `desc: \|`<br>`  hello`<br>`\|` | 多行块必须显式以同级 `\|` 闭合 |
| **空字典** | `meta:` （下无内容表示空串） | `meta: {}` | 空字典必须显式写 `{}` |

---

## 官方库与 API 支持

XUN 提供了 6 种主流语言的标准实现，全面遵循**对称 API 设计**（`encode` / `decode`、`marshal` / `unmarshal`、`dump` / `load`），内置格式解包工具（Unpack Helpers），并通过了全量双向互转与文件读写测试。

| 语言 | 模块/包路径 | 解码 (Decode / Unmarshal) | 编码 (Encode / Marshal) | 特色能力 | 安装/使用方式 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **JavaScript** | [`@qorm/xun`](javascript/) | `decode(str)` / `parse(str)` | `encode(obj)` / `stringify(obj)` | 自动识别 `Date`、`unpack` 辅助解包、`toUUID`/`toIP`/`toChar` | `npm install @qorm/xun` |
| **Python** | [`xun-format`](python/) | `decode(str)` / `load(fp)` / `loads(str)` | `encode(dict)` / `dump(dict, fp)` / `dumps(dict)` | 原生识别 `datetime`/`UUID`/`ip`，`unpack` 递归解包，`to_timezone`/`to_char` | `pip install git+https://github.com/qorm/xun.git#subdirectory=python` |
| **Go** | [`github.com/qorm/xun/go`](go/) | `xun.Decode(str)` / `xun.Unmarshal(b, &v)` | `xun.Encode(v)` / `xun.Marshal(v)` | 支持 `time.Time`、`net.IP`、Tagged 快捷转换方法（`AsTime`/`AsUUID`/`AsChar` 等） | `go get github.com/qorm/xun/go` |
| **Rust** | [`xun`](rust/) | `xun::decode(&str)` / `xun::from_str(&str)` | `xun::encode(&val)` / `xun::to_string(&val)` | 强类型 `Value` 与 `Tagged` 解包方法（`to_datetime`/`to_ip`/`to_uuid`/`to_char`） | `xun = { git = "https://github.com/qorm/xun", subdirectory = "rust" }` |
| **Java** | [`io.github.qorm.xun`](java/) | `Xun.decode(str)` / `Xun.load(path)` | `Xun.encode(map)` / `Xun.dump(map, path)` | 识别 `Instant`/`UUID`/`InetAddress` 等，`toZoneId`/`toChar` | 引入 `java/src` 源码路径 |
| **C** | [`c/`](c/) | `xun_decode` / `xun_decode_file` | `xun_encode` / `xun_encode_file` | 内存池零碎碎片、内置容量/时长/版本/UUID/IP 解析器 | 编译 `xun.h` / `xun.c` |

每一类语言都**完整支持全部 20 种核心 Tag**：解码时严格校验字形、编码时完整往返、并且对宿主机语言可原生表示的每一类格式提供解包辅助方法（`n`/`i`/`f`/`x`/`o`/`unix`/`b` 直接解为原生数字/布尔，`xb`/`b64` 解为原生字节缓冲，`d`/`t`/`dt`/`tz`/`du`/`sz`/`ver`/`uuid`/`ip`/`c` 对应上表所列的各语言辅助方法）。

### 代码使用示例

#### JavaScript / TypeScript
```js
import { encode, decode, unpack } from "@qorm/xun";
import { readFileSync, writeFileSync } from "node:fs";

// 解码 / 解析
const doc = decode(readFileSync("config.xun", "utf8"));
console.log(doc.server.port); // 8080

// 辅助解包为原生类型
const unpacked = unpack(doc);

// 编码 (原生 Date/Uint8Array 自动映射为 !dt / !xb)
const output = encode(unpacked);
writeFileSync("output.xun", output, "utf8");
```

#### Python
```python
from pathlib import Path
from xun import encode, decode, unpack, dump, load

# 解码 / 加载
doc = decode(Path("config.xun").read_text(encoding="utf-8"))
print(doc["server"]["port"])

# 递归解包为 Python 原生对象 (datetime, UUID, IP, 分段版本)
native_data = unpack(doc)

# 编码 (原生 datetime/UUID/ip 自动编码为对应 Tag)
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

	// 解码 / 反序列化
	var doc map[string]any
	if err := xun.Unmarshal(b, &doc); err != nil {
		log.Fatal(err)
	}

	// 编码 / 序列化
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
    
    // 解码
    let doc = decode(&src)?;
    if let Some(port) = dict_get(&doc, "port") {
        println!("port: {:?}", port.as_i64());
    }
    
    // 编码
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
        
        // 解码
        Map<String, Object> doc = Xun.decode(src);
        
        // 编码
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
    
    // 解码文件
    if (xun_decode_file("config.xun", &doc, &err) != 0) {
        fprintf(stderr, "Decode error: %s\n", err.message);
        return 1;
    }
    
    // 编码写入文件
    if (xun_encode_file(doc, "output.xun") != 0) {
        fprintf(stderr, "Encode error\n");
    }
    
    xun_free(doc);
    return 0;
}
```

---

## 编辑器语法高亮与工具链支持

XUN 提供了开箱即用的主流编辑器语法高亮与语言支持配置文件，详细图文指南参见 [语法高亮使用文档](syntaxes/README.md)：

### 1. VS Code / Cursor / Windsurf
官方扩展位于 [`editors/vscode/`](editors/vscode/)，支持语法着色、2 格缩进与代码折叠。
- **一键安装（软链接）**：
  ```bash
  # VS Code
  mkdir -p ~/.vscode/extensions && ln -s "$(pwd)/editors/vscode" ~/.vscode/extensions/vscode-xun
  # Cursor
  mkdir -p ~/.cursor/extensions && ln -s "$(pwd)/editors/vscode" ~/.cursor/extensions/vscode-xun
  ```
- **TextMate 语法定义**：[`syntaxes/xun.tmLanguage.json`](syntaxes/xun.tmLanguage.json)

### 2. JetBrains 系列 (IntelliJ IDEA / WebStorm / PyCharm / GoLand)
1. 打开 `Settings` / `Preferences` -> `Editor` -> `TextMate Bundles`；
2. 点击 `+` 添加本项目的 [`syntaxes/`](syntaxes/) 目录即可自动启用高亮。

### 3. Vim / NeoVim
- **一键安装**：
  ```bash
  mkdir -p ~/.vim/syntax ~/.vim/ftdetect
  cp editors/vim/syntax/xun.vim ~/.vim/syntax/
  echo 'autocmd BufNewFile,BufRead *.xun setfiletype xun' > ~/.vim/ftdetect/xun.vim
  ```

### 4. Sublime Text
- 将 [`editors/sublime/XUN.sublime-syntax`](editors/sublime/XUN.sublime-syntax) 复制到 Sublime Text 的 `Packages/User/` 目录下即可。

---


## 明确不做

- 隐式类型转换（不猜测 `yes` / `NO` / 挪威问题）
- 点键路径 `a.b.c`、YAML 锚点 `&*`、多文档 `---`
- 无类型的行内 `[a, b]`（逗号会引发引号需求）
- `inf` / `nan`、`null`
- 文件 include
- 将复杂业务类型强行塞入语法核心（如 `enum`、secret、范围等交由 schema 处理）