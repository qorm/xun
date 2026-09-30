# XUN 设计文档索引

本目录收录 XUN 格式的设计规范、RFC 草案与决策记录。

## 规范演进路线图

```
v0.1.x ─── 当前稳定版
   │
   ├── RFC-0001  可选 end 块闭合标记  ──→ v0.2.0 (目标 2027-Q2)
   │
   └── RFC-0002  内联对象 / 数组字面量 ─→ v0.2.0 (同上)
```

## RFC 索引

| RFC | 标题 | 状态 | 目标版本 | 实现状态 |
| :--- | :--- | :--- | :--- | :--- |
| [RFC-0001](./RFC-0001-optional-end-block-delimiter.md) | 可选 `end` 块闭合标记 | Draft | v0.2.0 | **JS ✅ Python ✅ Go ✅ Rust ✅ Java ✅ C ✅** |
| [RFC-0002](./RFC-0002-inline-object-array-literals.md) | 内联对象 / 数组字面量 | Draft | v0.2.0 | **JS ✅ Python ✅ Go ✅ Rust ✅ Java ✅ C ✅** |

## 实现进度

| 语言 | RFC-0001 | RFC-0002 | 测试用例 |
| :--- | :--- | :--- | :--- |
| JavaScript | ✅ MVP | ✅ MVP | 86 tests, 0 fail |
| Python | ✅ MVP | ✅ MVP | 81 tests, 0 fail |
| Go | ✅ MVP | ✅ MVP | 51 tests, 0 fail |
| Rust | ✅ MVP | ✅ MVP | 57 tests, 0 fail |
| Java | ✅ MVP | ✅ MVP | 58 tests, 0 fail |
| C | ✅ MVP | ✅ MVP | 43 tests, 0 fail |

### 关键 commit

| 语言 | Commit | 行数 |
| :--- | :--- | :--- |
| JavaScript RFC-0001 | `8f3b294` | +384 |
| JavaScript RFC-0002 | `4c061f9` | +308 |
| Python RFC-0001+0002 | `25c8d8e` | +850 / -48 |
| Go RFC-0001+0002 | `8c198f5` | +893 / -21 |
| Rust RFC-0001+0002 | `88ce01c` | +950 / -39 |
| Java RFC-0001+0002 | `6268ffd` | +1004 / -90 |
| C RFC-0001+0002 | `058200e` | +1418 / -329 |

## 设计哲学摘要

XUN 的所有 RFC 都应回答以下问题：

1. **最小惊讶**：是否符合现有 XUN 用户的直觉？
2. **Token 友好**：是否进一步降低体积 / LLM 生成开销？
3. **向后兼容**：现有 100% 合法的 v0.1.x 文件是否能被新解析器接受？
4. **跨语言一致**：6 个官方库是否能同步、无歧义地实现？
6. **当 100% 严格消歧**：是否会带来新的隐藏歧义？

## 提交流程

1. Fork 仓库，创建 `docs/rfc-NNNN-<short-title>.md`
2. 在 README 索引表中追加一行
3. 提交 PR，附 RFC 状态变更说明
5. 维护期通常 6-12 个月；合并后进入对应版本 roadmap