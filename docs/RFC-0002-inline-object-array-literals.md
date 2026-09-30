# RFC-0002: 内联对象 / 数组字面量 (Inline Object & Array Literals)

| 字段 | 值 |
| :--- | :--- |
| **状态** | Draft（草案）；JavaScript 已落地 MVP（v0.2.0-dev） |
| **作者** | XUN Working Group |
| **创建日期** | 2026-09-29 |
| **目标版本** | XUN 0.2.0（待定） |
| **优先级** | P1（Token 效率） |
| **影响语言** | JavaScript ✅ / Python / Go / Rust / Java / C |
| **JS 实现 commit** | `4c061f9` feat(javascript): implement RFC-0002 inline object/array literals |

---

## 1. 摘要 (Summary)

在 XUN 现有"行内紧凑数组 `!n[80, 443, 8080]`"基础上，**扩展内联表达力至对象（字典）字面量**，并允许行内紧凑形式与块形式在同一层级混用。目标：在保留 XUN 缩进哲学的前提下，将"大量重复对象（如 k8s pod 列表、Terraform resource 列表）"的 Token 消耗再砍 20% ~ 30%。

## 2. 动机与背景 (Motivation)

XUN 当前列表项只能写：

```xun
replicas:
  - host: 10.0.0.1
    port: 80
    zone: us-east-1a
  - host: 10.0.0.2
    port: 80
    zone: us-east-1b
  - host: 10.0.0.3
    port: 80
    zone: us-east-1c
```

**问题**：N 个 replicas 平均每个 4 行，20 个 pod 就是 80 行；体积与 HCL 持平（+12% vs 纯 JSON）。

**对比**：在 HCL / JSON 中同等配置：

```hcl
replicas = [
  { host = "10.0.0.1", port = 80, zone = "us-east-1a" },
  { host = "10.0.0.2", port = 80, zone = "us-east-1b" },
]
```

HCL 行内紧凑但每个对象 1 行 —— **XUN 应该也能做到。**

## 3. 提案语法 (Proposal Syntax)

### 3.1 内联对象字面量

```xun
replicas: [{host: 10.0.0.1, port: 80, zone: us-east-1a},
           {host: 10.0.0.2, port: 80, zone: us-east-1b},
           {host: 10.0.0.3, port: 80, zone: us-east-1c}]
```

- `{key: value, key2: value2}` 是新引入的**内联对象字面量**
- 元素间 `,` 分隔（与现有紧凑数组一致）
- `{` `}` 必须成对出现
- 字符串值：裸无引号 / `"..."` 引号字面量（与现有 §3.1 一致）

### 3.2 类型 Tag 前缀

```xun
ports: !n[80, 443, 8080]                   # 已有：紧凑标量数组
servers: ![{host: a, port: 80}, {host: b}] # 新：对象数组
matrix: !n[[1,2,3], [4,5,6], [7,8,9]]     # 新：嵌套紧凑
```

类型 Tag 沿用现有规则：
- `!n` / `!i` / `!f` 等数字 Tag 仍要求元素是数字
- 新增 `!o`（任意对象字面量）作为对象数组的默认
- 字符串数组内联：必须加 `!s` 前缀（与现有规则一致）

### 3.3 与块形式自由混用

```xun
servers:
  - {host: 10.0.0.1, port: 80}    # 行内对象
  - {host: 10.0.0.2, port: 80}    # 行内对象
  -                              # 行内对象也可独立成行
    host: 10.0.0.3
    port: 80
    tls:
      cert: /etc/ssl/cert.pem
```

混用时**保持类型一致性**：列表内不能部分用对象、部分用裸字符串（除非显式类型 Tag）。

### 3.4 多行内联对象

```xun
big_list: [
  {name: very-long-key-1, value: extremely-long-value-with-many-characters},
  {name: very-long-key-2, value: another-long-value-here},
  {name: very-long-key-3, value: yet-another-long-value},
]
```

每行一个对象，但保留在 `[ ]` 内 —— 这是 HCL 也支持的最佳实践。

## 4. 形式语义 (Formal Semantics)

### 4.1 词法扩展

新增 4 个 token：
- `{` ：对象字面量开始
- `}` ：对象字面量结束
- `,` ：元素分隔符（已存在，作用域扩展）
- `;` （可选）：对象键值对分隔符（仅在 `[ ]` 内、与 `:` 二选一；避免与时间戳混淆）

### 4.2 解析规则

```
inside_inline_object_or_array:
  - 遇到 `,` 或 `;` → 结束当前元素，等待下一个
  - 遇到 `}` 或 `]` → 闭合当前字面量
  - 遇到嵌套 `{` / `[` → 递归进入
  - 遇到 `:` → 当前元素的键值对分隔
  - 字符串 / 数字 / Tag → 当前值
```

### 4.3 同层互斥规则的例外

XUN §2 现有规则"同层字典/列表互斥"。本 RFC 提议引入**块标记前缀**作为显式声明：

```xun
matrix: [&]                    # 显式声明"下面是块列表"
  - {host: a, port: 80}        # 行内对象
  - {host: b, port: 81}        # 行内对象
end
```

`[&]` 是"块标记前缀"（block-style list marker prefix）：
- `[&]` 表示"下面是块形式列表"
- `[|]` 表示"下面是流形式（紧凑）列表"
- 默认根据首个 `-` 的存在推断；显式声明仅在歧义时需要

## 5. 向后兼容性 (Backward Compatibility)

| 场景 | 旧解析器 | 新解析器 |
| :--- | :--- | :--- |
| 旧 XUN 文档（无内联对象） | ✅ 通过 | ✅ 通过 |
| 含 `{...}` 的新文档 | ❌ 旧解析器报 unexpected '{' | ✅ 通过 |
| 含 `[&]` 的新文档 | 视作语法错误 | ✅ 通过 |
| 现有 `!n[...]` 紧凑数组 | ✅ 通过 | ✅ 通过（语法兼容） |

**关键不兼容点**：`{` `}` `,` 在根内 `[]` 之外的旧代码中已经被使用吗？

经过 6 语言官方库测试集（v0.1.5）扫描：
- **`{` 仅出现在 §5 多行块的尾部 `\|` 后面不可能**；行内 `\` 用作读取对象闭合不出现
- **`}` 同上**
- 现有 `!n[...]` 形式内 `,` 已是合法元素分隔符，无冲突

**结论**：向后兼容性良好。

## 6. 错误码扩展 (Error Code Additions)

| 错误码 | 描述 |
| :--- | :--- |
| `E0051` | unclosed inline object/array (expected '}' / ']') |
| `E0052` | mixed object/array/scalar in inline list |
| `E0053` | inline object key must be identifier or "string" |
| `W0021` | consider block-form for readability (style hint) |

## 7. 体积收益估算 (Token Savings Estimate)

以 k8s pod 列表（20 个 pod × 5 字段）为例：

| 格式 | 行数 | 字节 | Token |
| :--- | :--- | :--- | :--- |
| **XUN 现状（块形式）** | 100 行 | ~2400 B | ~620 |
| **XUN RFC-0002 实施后** | 23 行 | ~1100 B | ~290 |
| **HCL** | 22 行 | ~1300 B | ~340 |
| **JSON（pretty）** | 100 行 | ~3200 B | ~880 |

**结论**：本 RFC 实施后，XUN 在"大量重复对象"场景下 **Token 消耗再降 53%**，并首次低于 HCL 约 15%。

## 8. 替代方案 (Alternatives Considered)

### 8.1 完全引入 JSON 子集
- ❌ 违反 XUN "默认无引号"哲学；引号噪音

### 8.2 仅支持单行 `{...}`，不支持 `[{...}, {...}]`
- ❌ 用户仍会手写多次 `[ {...}`，失去压缩价值

### 8.3 改为 `key=value`（HCL 风格）
- ❌ 引入新分隔符；与现有 `:` 冲突，破坏 XUN 直觉

### 8.4 引入 KDL 节点语法
- ❌ 与 XUN 缩进哲学对立；超出 RFC 范围

**结论**：内联 `{key: value, ...}` 是 XUN 哲学下最自然、最小惊讶的扩展。

## 9. 开放问题 (Open Questions)

1. 内联对象是否支持**多层嵌套**？建议：支持（已写入 §4.2）
2. 内联对象内是否支持 `!tag`？建议：支持（按 §3.2 一致）
3. `;` 与 `,` 在内联对象中是否二选一？建议：可选（默认 `,`）
4. `[&]` 与 `[|]` 是否必需？建议：默认推断，仅在歧义时显式声明
5. 行内 `{key: value}` 是否也支持"`end` 块标记"？建议：暂不支持（保持简洁）

## 10. 实现计划 (Implementation Plan)

| 阶段 | 内容 | 时间 |
| :--- | :--- | :--- |
| Phase 1 | RFC 进入 Draft；6 语言官方库增加 token 扫描器 | 2026-10 |
| Phase 2 | 解析器实现 + 编码器输出策略（默认仍块形式） | 估 2026-11 ~ 12 |
| Phase 3 | 兼容性测试集（`test/inline-literal/`，≥ 50 case） | 估 2027-Q1 |
| Phase 4 | XUN 0.2.0 正式发布 | 估 2027-Q2 |

## 11. 测试用例 (Test Cases Outline)

```xun
# TC01: 简单内联对象
config: {host: localhost, port: 80}

# TC02: 内联对象 + 字符串引号
owner: {name: "Alice", role: "admin"}

# TC03: 内联对象 + 类型 Tag
ports: !n[{port: 80, proto: tcp}, {port: 443, proto: tcp}]

# TC04: 嵌套内联
matrix: !n[[1,2,3], [4,5,6], [7,8,9]]

# TC05: 块形式与内联混用
servers:
  - {host: a, port: 80}
  - {host: b, port: 81}
  - host: c
    port: 82

# TC06: 多行内联（每行一个对象）
big: [
  {name: k1, val: v1},
  {name: k2, val: v2},
]

# TC07: 错误 - 闭合不匹配
bad: {host: a, port: 80   # E0051

# TC08: 错误 - 混用类型
bad2: [{host: a}, 42]     # E0052
```

## 12. 参考 (References)

- HCL 块与内联表达：https://github.com/hashicorp/hcl/blob/main/hclsyntax/spec.md#functions
- JSON5 内联对象语法：https://json5.org/
- KDL 节点子节点表达：https://github.com/kdl-org/kdl/blob/main/SPEC.md
- Python dataclass 紧凑构造：PEP 3155