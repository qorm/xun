# Changelog

All notable changes to XUN (X Unquoted Notation) are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] — v0.2.0-dev

### Added

- **RFC-0001: 可选 `end` 块闭合标记** ([RFC-0001](docs/RFC-0001-optional-end-block-delimiter.md))
  - 块可选地以 `end` 或 `end <key>` 显式闭合，与现有缩进规则完全兼容
  - 解决拷贝粘贴吞子节点、HCL/JSON 片段嵌入、错误定位粒度、自动生成器脆弱性 4 类实战痛点
  - 6 语言官方库全部实现（JavaScript / Python / Go / Rust / Java / C）
  - 错误码扩展：`E0041` unexpected 'end'、`E0042` end-key mismatch、`W0011` block lacks 'end'

- **RFC-0002: 内联对象 / 数组字面量** ([RFC-0002](docs/RFC-0002-inline-object-array-literals.md))
  - `{key: value, key2: value2}` 内联对象字面量作为列表项
  - `[{...}, {...}]` 无 tag 内联对象数组；`!o[{...}, {...}]` 显式类型 tag
  - 块形式与内联形式在同一列表自由混用
  - 大量重复对象（如 k8s pod / Terraform resource 列表）Token 消耗降低约 **53%**
  - 6 语言官方库全部实现

- **综合 demo 测试**：k8s Deployment 配置同时使用 `end` 关键字和内联对象数组
  - `javascript/test/rfc-demo.test.js`
  - `python/tests/test_rfc_demo.py`

### Changed

- README 新增 KDL（HCL 继任者）对比章节（9 维度横向对比）
- docs/ 目录新增 RFC 索引（`docs/README.md`）和设计哲学摘要
- `encode()` 在 6 个语言中保持**纯输出**：不自动加 `end`、不自动用内联对象（RFC Phase 1）

### Implementation Status

| 语言    | RFC-0001 | RFC-0002 | 测试用例 |
| :------ | :------- | :------- | :------- |
| JavaScript | ✅ | ✅ | 86 tests, 0 fail |
| Python     | ✅ | ✅ | 81 tests, 0 fail |
| Go         | ✅ | ✅ | 51 tests, 0 fail |
| Rust       | ✅ | ✅ | 57 tests, 0 fail |
| Java       | ✅ | ✅ | 58 tests, 0 fail |
| C          | ✅ | ✅ | 43 tests, 0 fail |
| **Total**  | **6/6** | **6/6** | **376 tests** |

## [0.1.5] — 2026-09-29

### Added

- Java 与 C 解析器（与现有 4 语言对齐）
- 紧凑数组与块形式数组混用、`!s[]` 字符串数组块形式强制
- 解包辅助方法（unpack helpers）覆盖所有 20 种核心 tag
- 编码器自动剥除数字形态字符串外的双引号
- 官方媒体类型：`application/xun`（兼容 `text/xun`）

## [0.1.0] — 2025-08

### Added

- 初始规范发布：JavaScript / Python / Go / Rust 4 语言官方解析器
- 20 种核心类型 Tag（`!n`, `!i`, `!f`, `!s`, `!d`, `!t`, `!dt`, `!tz`, `!du`, `!sz`, `!ver`, `!uuid`, `!ip`, `!o`, `!x`, `!xb`, `!b`, `!b64`, `!c`, `!unix`）
- 严格 2 空格缩进规则
- `\|` ... `\|` 多行块定界语法
- VS Code / Vim / Sublime 语法高亮
- README 中英双语