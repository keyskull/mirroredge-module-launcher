# 已知错误与修复记录流程

防止**重复修复**和**用已证无效的方式再试一次**。本仓库采用「登记册 + 检索清单 + 可选 Memory」三层机制。

## 目标

1. 改代码前先查是否已有记录。
2. 每次失败尝试写入 **「已尝试且无效」** 表，而不是只记最终成功方案。
3. 根因与修复须可验证（harness、复现步骤或二进制分析），避免「猜测性修复」进入登记册。
4. 与 [troubleshooting.md](troubleshooting.md) 分工：登记册保完整历史；troubleshooting 保快速查表。

## 三层存储

| 层 | 位置 | 权威程度 | 何时用 |
|----|------|----------|--------|
| **登记册** | [docs/known-issues/](known-issues/README.md) | **最高**（进 git，可审查） | 复杂/易复发问题；须记录失败方案 |
| **症状速查** | [troubleshooting.md](troubleshooting.md) | 高 | 用户症状 → 修复步骤；链接到 KI ID |
| **会话记忆** | Engra Memory MCP（`scope: engineering`, `topic: known-issue/…`） | 辅助检索 | 跨会话快速语义搜索；**不替代** git 登记册 |

## 何时必须新建 KI 条目

满足任一条件即建 [`known-issues/KI-YYYY-NNN-*.md`](known-issues/_template.md)：

- 同一问题被修复或讨论 **≥2 次**
- 至少有一种**已尝试且证明无效**的方案
- 违反 [AGENTS.md](../AGENTS.md) 中「Critical constraints」类约束（如 inject worker 上 hook D3D9）
- 修复涉及多文件或非显而易见的设计（Alt+Tab、IME、设备丢失等）

简单配置/环境问题（错误游戏路径、未装 VS）只更新 troubleshooting，不必建 KI。

## 动手修 bug 之前（AI / 开发者必做）

按顺序执行，**至少完成前两步**：

```text
1. 登记册     → docs/known-issues/README.md 索引 + grep 症状关键词
2. 速查表     → docs/troubleshooting.md
3. 架构约束   → AGENTS.md「Critical constraints」
4. Memory     → memory_search(query=症状/模块名, scope=engineering)
5. 源码注释   → grep「Do not reintroduce」「verified 20」
6. Harness    → 若已有场景，先跑相关 scenario（见 ai-debug-harness.md）
```

若找到 **resolved** 的 KI 条目：

- 优先采用 **Verified fix** 中的方案
- **禁止**重试「Failed approaches」表中的行，除非有**新证据**说明环境已变（须在 KI 变更日志中说明）

若症状匹配但无 KI 条目：修复完成后评估是否应新建条目（见下文「修复后」）。

## 修复过程中

1. 将 KI 状态设为 `investigating`（新建或更新现有条目）。
2. 每尝试一种新方案，**无论成败**，在「已尝试且无效」表追加一行；若成功，移到 Verified fix 并注明验证方式。
3. 不要只在聊天或 PR 描述里留痕——**登记册是防重复的权威来源**。

## 修复后（关闭循环）

1. **更新 KI 文件**：状态 `resolved`，填写 `最后验证`、Verified fix、相关 commit/文件。
2. **更新 troubleshooting.md**：增加或更新症状 → 修复行，并链接 `KI-YYYY-NNN`。
3. **更新主题 doc**（如 [module-manager.md](module-manager.md)）若行为/架构说明变化。
4. **可选 Memory**：`memory_save_atom`，例如：
   - `scope`: `engineering`
   - `topic`: `known-issue/KI-2026-001`
   - `tags`: `d3d9`, `do-not-retry`
   - `document`: 简短摘要 + 「勿重试：…」+ 链接 `docs/known-issues/…`
5. 若旧 Memory 与登记册矛盾：用 `memory_correct_atom` 修正，勿 duplicate。

## 条目格式

复制 [known-issues/_template.md](known-issues/_template.md)。**「已尝试且无效 — 勿重复」** 是必填节（resolved 条目也保留，不可删）。

ID 规则：`KI-{年}-{三位序号}`，如 `KI-2026-001`。文件名：`KI-YYYY-NNN-short-slug.md`。

## 状态含义

| 状态 | 含义 |
|------|------|
| `open` | 已报告，根因未确认 |
| `investigating` | 正在试方案；失败表持续更新 |
| `resolved` | 根因与修复已验证 |
| `superseded` | 被新 KI 或架构变更取代；链接到新条目 |

## 与 AI Debug Harness 的配合

- 验证修复：在 KI 的 Verified fix 中写明 scenario（如 `user-flow`、`smoke-split`）及期望 NDJSON/日志断言。
- 回归：相关 KI 应在 `ci-gate` 或 PR 说明中注明已跑过的 layer。
- 失败 bundle：`auto-loop -RebuildOnFail` 产出的日志路径可写入 KI「相关」节。

## 反模式（禁止）

| 反模式 | 正确做法 |
|--------|----------|
| 只改代码不更新登记册 | 复杂问题必须更新 KI + troubleshooting |
| 删除失败方案历史 | 保留 Failed approaches 表 |
| 用 Memory 代替 git 文档 | Memory 仅辅助；登记册进仓库 |
| 未验证就标 resolved | 状态保持 `investigating` |
| 重试 Failed approaches 中方案 | 先读 KI；有新证据则更新表并说明 |

## 相关文档

- [known-issues/README.md](known-issues/README.md) — 条目索引
- [troubleshooting.md](troubleshooting.md) — 症状速查
- [AGENTS.md](../AGENTS.md) — AI 约束与 doc 索引
- [ai-debug-harness.md](ai-debug-harness.md) — 自动化验证
