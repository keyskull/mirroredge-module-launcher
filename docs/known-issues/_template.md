# KI-YYYY-NNN: 简短标题（英文或中文）

> 复制本文件为 `KI-YYYY-NNN-short-slug.md`，填入下方各节。删除本提示行。

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-YYYY-NNN |
| **状态** | `open` / `investigating` / `resolved` / `superseded` |
| **首次记录** | YYYY-MM-DD |
| **最后验证** | YYYY-MM-DD |
| **区域** | launcher / injection / module_manager / mmultiplayer / d3d9 / sdk / build |
| **标签** | 可选，如 `alt-tab`, `ime`, `d3d9` |

## 症状（Symptoms）

用户或日志中可观测到的现象。列出 launcher `[mod]` 行、NDJSON 探针、崩溃时机等。

## 根因（Root cause，须已验证）

仅在用 harness、复现步骤或二进制分析**确认**后填写。未确认时写「假设」并标 `investigating`。

## 已验证修复（Verified fix）

- 改了哪些文件 / 提交
- 如何验证（场景名、harness 断言、手动步骤）
- 链接：`troubleshooting.md` 对应节、架构 doc

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

**本节是防重复修复的核心。** 每次失败尝试都要追加一行。

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| YYYY-MM-DD | 例如：在 InitWorker 里 hook Direct3DCreate9 | 崩溃 | 渲染线程 R6025；见 KI-2026-002 |
| | | | |

## 相关

- **源码：** `path/to/file.cpp`
- **文档：** [troubleshooting.md](../troubleshooting.md#...)
- **Harness：** `smoke-split` / `user-flow` / …
- **Memory atom：** （可选）Engra topic `known-issue/KI-YYYY-NNN`

## 变更日志

| 日期 | 作者 | 说明 |
|------|------|------|
| YYYY-MM-DD | | 创建记录 |
