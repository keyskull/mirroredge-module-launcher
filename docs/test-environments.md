# Test environments (多机 Cursor)

每台测试机都安装 **Cursor** + 本仓库。路径、盘符、用户名不同是常态；**禁止**在脚本或源码里写死某台机器的路径。

## 原则

| 规则 | 说明 |
|------|------|
| **路径只配本地** | 游戏根目录 → `deploy.config.json`（gitignore）或 `ME_DEPLOY_PATH` |
| **机器标签可追踪** | `deploy.config.json` 的 `testMachine` → 部署时写入游戏根 `settings.json` → 启动时 `MMOD_TEST_MACHINE` → `logs/.../environment.json` |
| **登记册进 git** | 在 [`test-environments.json`](../test-environments.json) 登记机器 id / 备注；**真实本机路径用占位符**，本地可复制 [`test-environments.json.example`](../test-environments.json.example) 覆盖（勿把 `C:\Users\<you>\...` 提交回公共仓库） |
| **MCP 用 setup** | 克隆或换机后运行 `.\tools\debug-harness\setup.ps1`；勿手抄绝对路径到 MCP |
| **约束不变** | 所有机器遵守 [AGENTS.md](../AGENTS.md) Critical constraints（x86、proxy 模式、D3D9 hook 安全等） |

## 新机 / 换机 checklist

> **一键脚本（推荐）**：配置好 `deploy.config.json` 后运行  
> `.\tools\debug-harness\setup-test-machine.ps1`  
> 会依次：`git pull` → MCP + merge driver + pre-push hook → 同步 `test-logs/` → 校验 index → 打印各机状态。

1. 克隆仓库到任意路径（可与游戏同盘或不同盘）。
2. 复制 `deploy.config.json.example` → `deploy.config.json`，填写：
   - `deployPath` — 游戏根目录（含 `Binaries\MirrorsEdge.exe` 的上一级）
   - `testMachine` — 登记册中的 id（如 `1号机`、`2号机`）
3. 在 `test-environments.json` 添加或更新对应条目（id、deployPath、repoPath、notes）。
4. 安装 VS 2022 x86、Node.js（MCP）、可选 Go；无 DXSDK 时 `.\scripts\setup-dxsdk.ps1`。
5. **`.\tools\debug-harness\setup-test-machine.ps1`**（已配好 `deploy.config.json` 时；仅更新 MCP/hook/merge driver 且跳过 pull：`setup-test-machine.ps1 -SkipPull`）。
6. **重载 Cursor**（启用 `mirroredge-debug` MCP）。
7. `.\build.ps1`（按 `deploy.config.json` 部署并写入游戏根 `settings.json` 的 `diagnostics.testMachine`）。
8. 仓库内 **不要**提交 `deploy.config.json`；`.cursor/mcp.json` 为本地生成（见 `.gitignore`）。

### 每台机本地 git 配置（`setup.ps1` 自动写入，勿手抄）

| 配置项 | 值 | 作用 |
|--------|-----|------|
| `merge.test-logs-index.driver` | 本机仓库路径下的 `tools/git-merge-test-logs-index.ps1` | `index.json` / `CHANGELOG.md` 冲突时 accept-both |
| `core.hooksPath` | `.githooks` | push 前校验 `test-logs/`（若 commit 涉及该目录） |

**每台机必须各自运行 `setup.ps1` 或 `setup-test-machine.ps1`**：merge driver 里的绝对路径是本机路径（含空格时用 8.3 短路径），不能从 1号机复制 `.git/config`。

### 日常 harness 流程（每台机相同）

```powershell
git pull                                    # 拉远端 test-logs/ 与其他改动
.\build.ps1                                 # 按需
.\tools\debug-harness\run-all-scenarios.ps1  # 默认写完 test-logs/ 并 push
# 或仅本地：$env:MMOD_HARNESS_LOG_PUSH = "0"
```

push 失败（`index.json` 冲突）时：`Push-HarnessTestLog` 会 pull/rebase 并自动 merge；仍失败见 [`test-logs/README.md`](../test-logs/README.md)「accept both」。

查看各机 harness 状态：`.\tools\debug-harness\show-test-logs-status.ps1`（或 `git pull` 后读 `test-logs/index.json`）。

## 配置文件分工

| 文件 | 进 git | 作用 |
|------|--------|------|
| `deploy.config.json` | 否 | 本机 `deployPath`、`deployProxy`、**`testMachine`** |
| `deploy.config.json.example` | 是 | 模板（占位符，无真实用户名路径） |
| `settings.json`（仓库） | 是 | 默认 launcher/mods 选项；**不含**机器 id |
| `settings.json`（游戏根） | 部署生成 | 含本机 `diagnostics.testMachine` |
| `test-environments.json` | 是 | 各测试机登记（供人与 AI 对照） |
| `.cursor/mcp.json` | 否（本地） | `setup.ps1` 按当前仓库路径写入 `mirroredge-debug` MCP |

## 环境变量（诊断 / harness）

| 变量 | 来源 |
|------|------|
| `MMOD_TEST_MACHINE` | 启动器从游戏根 `settings.json` → `diagnostics.testMachine` |
| `MMOD_GAME_ROOT` | 启动器启动游戏时设置 |
| `MMOD_DIAGNOSTICS` / `MMOD_DEBUG_SESSION` | 见 [diagnostics-logging.md](diagnostics-logging.md)、[ai-debug-harness.md](ai-debug-harness.md) |

收集日志：`.\tools\collect-diagnostics.ps1 -GameRoot "<deployPath>"` — zip 内 `environment.json` 含 `testMachine`。

## Harness 结果共享（`test-logs/`）

每台测试机跑完 **`run-all-scenarios.ps1`** 后，结果写入 [`test-logs/`](../test-logs/README.md) 并 **默认 git push**，供其他机器 / Cursor 会话了解各环境状况：

| 路径 | 内容 |
|------|------|
| `test-logs/index.json` | 各机最近一次 run 摘要（pass 数、commit、branch） |
| `test-logs/CHANGELOG.md` | 全机汇总 change history（最新在上） |
| `test-logs/machines/<id>/history.jsonl` | 本机历次 run（一行一条 JSON，含 `gitAtStart`/`gitAtEnd`） |
| `test-logs/machines/<id>/CHANGELOG.md` | 本机可读 changelog，与 commit 对齐 |

前提：`deploy.config.json` 配置 **`testMachine`**（与登记册 id 一致）。仅本地不写 git：`$env:MMOD_HARNESS_LOG_PUSH = "0"`。

KI 回归（Alt+Tab / IME）：`.\tools\debug-harness\run-ki-regression.ps1` — 建议 **2号机** 在 overlay/input 相关改动后跑，结果同样写入 `test-logs/`。

其他机器：`git pull` 后读 `test-logs/index.json` 与 `CHANGELOG.md`；修 bug 前确认失败是否只发生在某一测试机。

`index.json` 多机同时 push 时可能 merge 冲突：见 [`test-logs/README.md`](../test-logs/README.md)「Git merge：`index.json` 冲突」— **两边机器条目都保留**，`updatedAt` 取最新 `finishedAt`。

## 脚本与工具的路径解析

- **构建/部署**：`build.ps1` → `Resolve-DeploySettings`（`deploy.config.json` → `ME_DEPLOY_PATH` → 仓库父目录）。
- **Harness / toggle**：读 `deploy.config.json` 或 `ME_DEPLOY_PATH`，勿写死 `C:\Users\...`。
- **collect-diagnostics**：`-GameRoot` 或自动探测常见 Steam/EA 路径。

## AI Agent 注意

1. 修 bug 前读 [known-issues-workflow.md](known-issues-workflow.md)；对比日志时先看 `testMachine` / `environment.json` 确认是哪台机。
2. 性能、崩溃结论需注明测试机 id；勿把单机现象当成全局结论。
3. 新增工具脚本时复用 `deploy.config.json` / `GamePath` 解析，禁止新增用户目录硬编码。
4. 改 MCP/harness 路径逻辑时更新本文与 `ai-debug-harness.md`。

## 登记册

见根目录 [`test-environments.json`](../test-environments.json)。新增机器时复制已有条目并改 `id`、`deployPath`、`notes`。

## LAN dual real-client soak

Two-machine soak for real peers (heartbeat / level sync). Bots alone do **not** replace this.

**Order:** start **host** first (it prints `PeerIp` and waits `-ClientGraceSec`, default 90s), then start **client** on the peer with that IP.

| Role | Command |
|------|---------|
| Host (1号机) | `.\tools\mp-lan-dual-soak.ps1 -Role host -SoakMinutes 15` |
| Client (2号机) | `.\tools\mp-lan-dual-soak.ps1 -Role client -PeerIp <host-lan-ip> -Room lan-soak -SoakMinutes 15` |

- Host advertises LAN IPv4 via `obj/harness-coord.json` + `obj/lan-soak-host.json` (includes a copy-paste client command), then runs `mp-real-level-bots` for the soak window.
- Host params: `-ClientGraceSec` (default 90) delay before bots so the peer can launch; lists multiple LAN IPs if present.
- Client **automates** boot -> `START_NEW_GAME` -> **wait TCP `PeerIp:5222`** -> inject MP (retries) -> `FORCE_HOSTED_LIVE` -> **wait `activation set live`** -> soak poll on `client.log` / `GET_STATUS`.
- Client params: `-ServerWaitSec` (default 600), `-InjectRetries` (default 3), `-SkipServerWait`, `-AllowNoLive` (debug only), or env `ME_LAN_PEER_IP` instead of `-PeerIp`.
- Pass criteria: runbook 8 gates on host; client requires `activation set live` (unless `-AllowNoLive`) and remote pose/bones / `udp seq stream` / remotes when soak >= 5 minutes.
- Interim single-machine gate: `.\tools\mp-real-level-bots.ps1 -BotCount 2 -PlaySeconds 90` (also requires `udp seq stream` for 1.2.11+).

