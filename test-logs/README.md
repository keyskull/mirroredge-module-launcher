# Harness test logs (多机共享)

每台测试机的 harness 结果写入 **`test-logs/machines/<testMachine>/`**，与 **git commit** 对齐后 push，供其他 Cursor 测试机 / AI 了解各环境状况。

## 目录结构

```
test-logs/
  README.md                 # 本说明
  CHANGELOG.md              # 全机汇总（最新在上）
  index.json                # 各机最近一次 run 摘要（由 machines/*/latest.json 生成）
  machines/
    1号机/
      latest.json           # 最近一次完整记录
      history.jsonl         # 历次 run（一行一条 JSON）
      CHANGELOG.md          # 本机 change history
    2号机/
      ...
```

## 每条记录包含

| 字段 | 说明 |
|------|------|
| `runId` | UTC 时间戳 id |
| `testMachine` | `deploy.config.json` → `testMachine` |
| `startedAt` / `finishedAt` | ISO 8601 UTC |
| `gitAtStart` / `gitAtEnd` | commit、branch、subject、dirty、与 run 对齐 |
| `suite` | 如 `run-all-scenarios`、`smoke-split` |
| `passCount` / `totalCount` | 通过数 |
| `results` | 各 scenario 的 Pass / Attempts / Error |

## 发布方式

全量 run 结束后**自动**写入并 push（需 `deploy.config.json` 中配置 `testMachine`；默认 push，设 `MMOD_HARNESS_LOG_PUSH=0` 可仅本地）：

```powershell
.\tools\debug-harness\run-all-scenarios.ps1
```

手动发布 / 仅本地不写 git：

```powershell
.\tools\debug-harness\publish-test-log.ps1 -Suite smoke-split -Pass -DurationMs 120000
.\tools\debug-harness\publish-test-log.ps1 -Suite run-all-scenarios -Results $r -PassCount 4 -TotalCount 19 -Push
```

环境变量：

| 变量 | 作用 |
|------|------|
| `MMOD_HARNESS_LOG_PUSH=0` | 单次 run 不 push（仅写 `test-logs/` 文件） |
| （默认） | `run-all-scenarios.ps1` 与 `publish-test-log.ps1` 写入后 **git commit + push** |

## 其他机器如何查看

```powershell
git pull
Get-Content test-logs\index.json | ConvertFrom-Json
Get-Content test-logs/CHANGELOG.md -Head 40
Get-Content test-logs/machines/2号机/history.jsonl -Tail 5
.\tools\debug-harness\show-test-logs-status.ps1
.\tools\debug-harness\show-test-logs-status.ps1 -Json
```

## 新机接入（2号机等）

完整 checklist 见 [`docs/test-environments.md`](../docs/test-environments.md)。摘要：

1. 克隆仓库；`deploy.config.json.example` → `deploy.config.json`（`deployPath` + **`testMachine`** 与登记册 id 一致）。
2. 在 `test-environments.json` 登记本机（进 git，供 AI/人工对照）。
3. **`.\tools\debug-harness\setup-test-machine.ps1`** — pull、MCP、merge driver、pre-push hook、校验 `test-logs/`。
4. 重载 Cursor → `.\build.ps1` → `.\tools\debug-harness\run-all-scenarios.ps1`。

每台机**独立**运行 setup；勿复制他机的 `.git/config` merge driver 路径。

## 日常：跑完 suite 后

| 步骤 | 命令 / 行为 |
|------|-------------|
| 拉远端 | `git pull`（suite 开始前也会 `Initialize-HarnessTestLogGit`） |
| 跑 harness | `run-all-scenarios.ps1` / `run-ki-regression.ps1` |
| 写入 | `test-logs/machines/<本机 testMachine>/`（`latest.json`、`history.jsonl`、本机 `CHANGELOG.md`） |
| 生成 index | 从各机 `latest.json` 重建 `test-logs/index.json` |
| push | 默认自动 commit + push；仅本机目录 + `index.json` + 根 `CHANGELOG.md` |
| 仅本地 | `$env:MMOD_HARNESS_LOG_PUSH = "0"` |
| push 前校验 | `.githooks/pre-push` → `validate-test-logs.ps1`（commit 含 `test-logs/` 时） |

AI：修 bug 前读 `test-logs/CHANGELOG.md` 与 `index.json`；**跨机告警**读 `test-logs/alerts/` 或 `show-machine-alerts.ps1`；确认失败是否仅发生在某一测试机。

## Git merge：`index.json` 冲突

两台测试机先后 push harness 结果时，`git pull` 可能只在 **`test-logs/index.json`** 产生冲突（各机只改自己的 `machines/<id>/`，通常不冲突）。

### 格式（object，不是 array）

`index.json` 是**单个 JSON 对象**，`machines` 为各机 id → 最近一次 run 摘要。**权威来源**是各机 `machines/<id>/latest.json`；`Publish-HarnessTestLog` 与 `Rebuild-HarnessTestLogIndex` 从 latest 重新生成 index，多机 push 时冲突更少。

```json
{
  "schemaVersion": 1,
  "updatedAt": "<最新 finishedAt，ISO 8601 UTC>",
  "machines": {
    "1号机": { "runId": "...", "finishedAt": "...", "suite": "...", ... },
    "2号机": { "runId": "...", "finishedAt": "...", "suite": "...", ... }
  }
}
```

旧版曾误写成顶层 **array**（`[{ ... }, { ... }]`）；合并时统一为上述 object。Harness 写入逻辑见 `DebugHarness.psm1` → `Publish-HarnessTestLog`（`Merge-HarnessTestLogIndex`）。

### 自动化

| 机制 | 说明 |
|------|------|
| **push 前 fetch 合并** | `Push-HarnessTestLog` 在 commit 前 `Sync-HarnessTestLogWithRemote`，把远端 `index.json` 与 `CHANGELOG.md` 并入本地 |
| **push 失败重试** | 首次 push 失败会 `pull --rebase`；`index.json` / `CHANGELOG.md` 冲突时 harness 自动 accept-both 后继续 |
| **Git merge driver** | `.gitattributes` + `tools/git-merge-test-logs-index.ps1`（index + CHANGELOG）；`setup.ps1` 配置 `merge.test-logs-index`（Windows 路径含空格时用 8.3 短路径，否则 forward slash 长路径） |
| **pre-push 校验** | `.githooks/pre-push` → `validate-test-logs.ps1`（仅当 push 的 commit 涉及 `test-logs/`）；`setup.ps1` 设置 `core.hooksPath=.githooks` |
| **新机一键配置** | `setup-test-machine.ps1` — `deploy.config.json` + pull + setup + validate + 状态 |
| **手动合并 index** | `.\tools\debug-harness\merge-test-logs-index.ps1 -InputPath .ours.json, .theirs.json` |
| **verify-harness 单测** | `Test-HarnessTestLogMerge` + `Test-HarnessTestLogIndexSchema`（index/changelog 合并、schema） |
| **suite 开始前 sync** | `run-all-scenarios.ps1` / `run-ki-regression.ps1` 调用 `Initialize-HarnessTestLogGit`（fetch 合并 + merge driver 提示） |
| **index 重建** | `Rebuild-HarnessTestLogIndex` / `rebuild-test-logs-index.ps1` — 从 `machines/*/latest.json` 生成 index |
| **CI 校验** | `validate-test-logs.ps1`；GitHub Actions `.github/workflows/test-logs.yml` |
| **状态查询** | `show-test-logs-status.ps1`；MCP `debug_get_harness_test_status` |

### 解决方式：两边都保留（accept both）

冲突两侧通常各只含**一台机**的 `machines` 条目（HEAD = 本机，incoming = 远端另一台机）。**不要二选一**，应合并为：

1. 删除 `<<<<<<<` / `=======` / `>>>>>>>` 标记。
2. 在 `machines` 下**同时保留**两侧的全部机器 key（如 `1号机` + `2号机`），每条 run 摘要用各自侧完整字段。
3. `updatedAt` 取所有保留条目中 **`finishedAt` 最晚** 的时间（与「最近一次更新」语义一致）。
4. 保留 `"schemaVersion": 1`。
5. `git add test-logs/index.json` 后完成 merge commit。

`test-logs/CHANGELOG.md` 若也冲突：两段 changelog 条目**都保留**，按时间**新在上**排列即可（`Merge-HarnessTestLogChangelog` / merge driver 可自动处理）。

## 登记册

机器 id 与路径见根目录 [`test-environments.json`](../test-environments.json)。
