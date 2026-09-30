# RFC-0001: 可选 `end` 块闭合标记 (Optional `end` Block Delimiter)

| 字段 | 值 |
| :--- | :--- |
| **状态** | Draft（草案）；JavaScript 已落地 MVP（v0.2.0-dev） |
| **作者** | XUN Working Group |
| **创建日期** | 2026-09-29 |
| **目标版本** | XUN 0.2.0（待定） |
| **优先级** | P0（核心编辑体验） |
| **影响语言** | JavaScript ✅ / Python / Go / Rust / Java / C |
| **JS 实现 commit** | `8f3b294` feat(javascript): implement RFC-0001 optional 'end' block delimiter |

---

## 1. 摘要 (Summary)

在 XUN 当前"严格 2 空格缩进"语法之上，**新增可选的 `end` 关键字作为块的显式闭合标记**。当块以 `end` 闭合时，缩进恢复规则由"必须严格递减"放宽为"可选递减"；当块不以 `end` 闭合时，沿用旧规则。

**核心原则**：纯增量向后兼容。现有 100% 合法 XUN 文档保持 100% 兼容；`end` 仅作为可选优化。

## 3. 动机与背景 (Motivation)

XUN 当前所有结构均依靠缩进表达，存在 4 类实战痛点：

### 3.1 拷贝粘贴吞噬子节点
```xun
server:
  host: localhost
  port: !n 8080
    # ← 用户从别处粘贴一段代码进来，原 4 空格缩进被识别为"非法跳级"
```
报错信息只能指向"非法缩进"，对新手不友好。

### 3.2 大段配置难以嵌套 HCL/JSON 片段
用户在 `.xun` 文件中嵌入 Terraform 风格子配置时，必须手动重新计算缩进，违反 DRY 原则。

### 3.3 错误定位粒度不足
"YAML 缩进错误"在大型文件中只能给出"第 87 行缩进错误"，无法指明是哪个块的哪一层。

### 3.4 自动生成器（k8s manifest、CI pipeline）的脆弱性
生成器输出 `.xun` 时如果 BOM 或前置注释扰乱了起始列，所有后续缩进会全部错位。

## 4. 提案语法 (Proposal Syntax)

### 4.1 基本形式
```xun
server:                      # 字典头（缩进 0）
  host: localhost
  port: !n 8080
  tls:
    cert: /etc/ssl/cert.pem
  end                        # ← 可选显式闭合 "server" 块
end                          # ← 可选显式闭合根字典
```

### 4.2 三种语义边界

| 写法 | 含义 | 是否必须 |
| :--- | :--- | :--- |
| `end` | 闭合**最内层**未闭合的字典块 | 否（可选） |
| `end <key>` | 显式闭合**名为 `<key>`** 的最近祖先块（强校验） | 否 |
| 缩进 ≤ 父层缩进 | 按旧规则隐式闭合 | 仍然支持 |

### 4.3 块嵌套语义
```xun
server:                      # A（缩进 0）
  tls:                       # B（缩进 2）
    mode: !o 755
  end tls                    # 显式闭合 B
end server                   # 显式闭合 A

# 注意：`end <key>` 中的 <key> 必须匹配最近未闭合祖先块的键名；
# 否则报 E0042 "end-key mismatch: expected 'foo', got 'bar'"
```

### 4.4 与列表项的交互
```xun
replicas:
  - host: 10.0.0.1
    port: 80
  - host: 10.0.0.2
    port: 81
end                          # 闭合 "replicas"（列表块也支持 end）
```

列表项使用 `-` 开头，本身就是"行首标记式闭合"，但顶层列表块的 `end` 仍然合法、可选。

### 4.5 注释行可与 `end` 同列
```xun
server:
  host: localhost
  end                        # 主机关闭
  # tls 节暂时禁用
  tls:
    cert: /etc/ssl/cert.pem
end server                   # ← 此处的 end 在第 0 列，闭合 server；tls 自动随之闭合
```

## 5. 形式语义 (Formal Semantics)

引入 `end` 后，块的状态机从"单层缩进栈"升级为"双层栈（缩进 + end 标记）"：

```
parse_state:
  indent_stack:  [0]                     # 旧：行首缩进等级栈
  end_stack:     []                     # 新：每个块是否声明了 end
  pending_key:   Optional[str] = None   # 当前块键名（用于 end-key 校验）
```

每行解析时新增 3 条规则：

1. 若行的内容（去注释、去空白）等于 `end` 或 `end <key>`：
   - 若 `end_stack` 为空 → **报错 E0041 unexpected 'end'**（无未闭合块）
   - 若未指定 `<key>` 或 `<key>` 等于 `pending_key` → 弹出 `end_stack`、弹出 `indent_stack`
   - 否则 → **报错 E0042 'end-key mismatch'**

2. 若行是普通键值对且**未声明 `end` 的子块起始**：
   - 压入新 `pending_key`，压入 `indent_stack`，**`end_stack` 不压入**（块未声明 end）

3. 若行的缩进 ≤ 当前 `indent_stack` 顶层（隐式闭合路径）：
   - 按旧规则闭合；若有对应 `end_stack` 项未消费 → **不报错，仅发 warning W0011 "block lacks explicit end"**

## 6. 向后兼容性 (Backward Compatibility)

| 场景 | 旧解析器 | 新解析器 |
| :--- | :--- | :--- |
| 旧 XUN 文档（无 `end`） | ✅ 通过 | ✅ 通过（缩进规则完全沿用） |
| 文档以 `end` 结尾（旧解析器无法识别） | ❌ 报"unexpected token 'end'" | ✅ 通过 |
| 同一文件混合"显式 end"和"纯缩进" | n/a | ✅ 通过（推荐写法） |
| `end` 前置空格、Tail 空格差异 | n/a | ✅ 容错 |

**重大不兼容**：旧解析器读到带 `end` 的文件会报错。**缓解策略**：
- 在 XUN 0.2.0 引入 6-12 个月过渡期
- 6 个官方库在过渡期内同时支持 0.1.x 和 0.2.x 两套解析器
- 编码器在过渡期不自动加 `end`（保持原样）

## 7. 错误码扩展 (Error Code Additions)

| 错误码 | 描述 |
| :--- | :--- |
| `E0041` | unexpected 'end' (no open block) |
| `E0042` | 'end-key mismatch: expected '%s', got '%s'' |
| `W0011` | block '%s' lacks explicit 'end' (style hint) |
| `W0012` | redundant 'end' (block already closed by dedent) |

## 8. 边界情况 (Edge Cases)

### 8.1 `end` 作为普通字符串值（key 名冲突）
```xun
end: foo                       # ← 字典的键名叫 "end"，合法；解析器需区分 "key: end" 与裸 "end"
end2: bar                      # ← 同样合法
```
**规则**：仅当行内容**去注释、去空白**后**严格等于** `end` 或 `end <key>` 才识别为闭合；含前缀后缀或包含 `:` 视为普通内容。

### 8.2 行首 BOM 干扰
`end` 必须在行第一个非空字符处；BOM 视为白读字节。

### 8.3 多行块 `| ... |` 内部出现 `end`
```xun
script: |
  echo "end of script"        # ← 内部字符串，不识别为闭合
|
```
**规则**：多行块内一切字符字面保留，包括 `end` 一词。

## 9. 替代方案 (Alternatives Considered)

### 9.1 使用 `}` 闭合
- ❌ 与 JSON 风格混淆，丧失 XUN 的无括号哲学

### 9.2 使用 `dedent` 关键字
- ❌ Python 概念引入，增加学习成本

### 9.3 使用缩进=0 作为根闭合
- ❌ 失去多文件包含、include、宏等扩展能力

### 9.4 不引入，纯靠工具补救
- ❌ 无法解决拷贝粘贴的根问题

**结论**：`end` 是与 XUN 哲学最匹配的方案（仅一个关键字、可选、向后兼容）。

## 10. 开放问题 (Open Questions)

1. `end` 是否应该接受**多个目标键**？如 `end server tls`
2. 编码器在过渡期后默认是否**自动加 `end`**？建议：默认不加，提供 `--end` CLI flag
3. 列表项 `-` 是否也需要 `end` 闭合？建议：不必要
4. `end` 是否需要支持**单行多块闭合** `end;end;end`？建议：0.2.0 不支持

## 11. 实现计划 (Implementation Plan)

| 阶段 | 内容 | 时间 |
| :--- | :--- | :--- |
| Phase 1 | 规范草案、本 RFC 进入 Draft | 2026-09 |
| Phase 2 | 6 语言官方库同步实现 `end` 解析 | 估 2026-10 ~ 12 |
| Phase 3 | 过渡期，旧解析器继续支持 | 估 2027-Q1 |
| Phase 4 | XUN 0.2.0 正式发布 | 估 2027-Q2 |
| Phase 5 | 编码器可选 `--end` flag | 估 2027-Q2 |

## 12. 参考 (References)

- YAML 缩进规范的"硬伤"：https://noyaml.com/
- Python `endif` 历史讨论：https://mail.python.org/pipermail/python-3000/2006-April/000751.html
- HCL 块闭合语义：https://github.com/hashicorp/hcl/blob/main/hclsyntax/spec.md
- Ruby `end` 关键字演化：Matz 1995 决策记录