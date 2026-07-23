# 跨环境诊断日志

在其他机器、非 harness 环境复现问题时，用本方案收集**可读的文本日志**、**NDJSON 探针**和**环境快照**，无需 Cursor MCP。

## 启用方式（三选一）

| 方式 | 适用场景 |
|------|----------|
| **`settings.json`** | 长期对用户机开启：`"diagnostics": { "enabled": true }` |
| **`MMOD_DIAGNOSTICS=1`** | 单次排查：在启动 Module Launcher **之前**设置用户/系统环境变量 |
| **Harness / MCP** | 已设置 `MMOD_DEBUG_SESSION` 时自动写入（与下文路径兼容） |

通过 Module Launcher 点击 **启动游戏** 时，子进程会继承环境变量。若已启用诊断，启动器会显示 session id 与 `session.log` 路径。

> 必须在 **启动游戏前** 设置环境变量或改好 `settings.json`，已运行的游戏进程不会自动拾取。

## 输出位置

每次会话一个目录：

```
<游戏根目录>/logs/<sessionId>/
  session.log        # 人类可读：module_manager / core / engine / 插件 ModLog
  session.ndjson     # 结构化探针（与 harness NDJSON 相同格式）
  environment.json   # 一次性环境快照（OS、gameRoot、进程路径）
<游戏根目录>/logs/last-session.json   # 指向最近一次会话
%TEMP%\mirroredge-debug\last-session.json   # 镜像 manifest（便于脚本查找）
```

Harness 未指定游戏根时，回退到：

```
%TEMP%\mirroredge-debug\<sessionId>\session.log
```

## 环境变量

| 变量 | 说明 |
|------|------|
| `MMOD_DIAGNOSTICS` | `1` 启用诊断（launcher 会生成 session id） |
| `MMOD_DEBUG_SESSION` | 会话 id（harness 或 launcher 自动设置） |
| `MMOD_DEBUG_LOG` | NDJSON 完整路径（默认 `…/session.ndjson`） |
| `MMOD_SESSION_LOG` | 文本日志完整路径（默认 `…/session.log`） |
| `MMOD_GAME_ROOT` | 游戏根目录（launcher 自动设置，用于解析 `logs/` 路径） |

权威定义：`shared/module_contract.h`。

## 日志来源

| 组件 | 写入 `session.log` | 写入 `session.ndjson` |
|------|-------------------|----------------------|
| module_manager `ModLog` | 是 | 诊断开启时镜像 `component: mod_log` |
| core / engine `ModLog` | 是 | 同上 |
| 插件（trainer / multiplayer / dolly） | 是 | 同上 |
| `DebugTrace` / `AgentDebugLog` | 否 | 是（探针事件） |
| `memory_fault_log` | 是（`[memory_fault]` 行） | 是（`message: memory_fault`） |

`MeSdk::Safe::*`（`safe_access`、`safe_gui`、`safe_gameplay` 等）里 SEH 吞掉的访问错误也会写入同一张表；跨 DLL 通过进程内共享映射 `Local\\MMOD_MemoryFaultLog_v1` 合并，`GET_STATUS` 的 `memoryFaults` 可见全部记录。
| Module Launcher 日志管道 | 是（中继行） | 否 |

### 内存访问错误列表（`memoryFaults`）

SEH 捕获的非法内存访问会写入环形缓冲（最近 64 条），包含：

| 字段 | 含义 |
|------|------|
| `elapsedMs` | 相对会话开始的毫秒数 |
| `threadId` | 发生线程 |
| `exceptionCode` / `exceptionName` | 如 `0xC0000005` / `ACCESS_VIOLATION` |
| `faultAddress` | 访问的虚拟地址（若有） |
| `instructionPointer` | 故障指令 EIP |
| `context` | 场景，如 `plugin_init`、`load_library` |
| `location` | 源码位置 |

查询方式：

```powershell
# 游戏运行时
.\tools\debug-harness\show-memory-faults.ps1
.\tools\debug-harness\show-memory-faults.ps1 -Json

# GET_STATUS JSON 字段 memoryFaults / memoryFaultCount
```

实现：`shared/memory_fault_log.cpp`（`mod_load_safe_seh.c` 等 SEH 站点挂钩）。

## 收集诊断包（其他机器）

在问题复现后（游戏可已退出），于仓库根目录或任意位置运行：

```powershell
# 自动读 <游戏根>/logs/last-session.json 与 %TEMP%\mirroredge-debug\
.\tools\collect-diagnostics.ps1 -GameRoot "C:\...\Mirror's Edge"

# 指定输出 zip 目录
.\tools\collect-diagnostics.ps1 -GameRoot "C:\...\Mirror's Edge" -OutDir "C:\Temp\me-bug-report"
```

脚本打包：

- `logs/<session>/session.log`、`session.ndjson`、`environment.json`
- `settings.json`、`%TEMP%\core.settings`
- 最近 Windows **应用程序错误**（Mirror's Edge / ModuleLauncher WER 1000）
- 可选：若游戏仍在运行，通过 pipe `GET_LOG` 拉取 manager 环形缓冲

将生成的 `mirroredge-diagnostics-*.zip` 附在 issue 或发给维护者。

## 与 AI Debug Harness 的关系

| | Harness | 本方案（diagnostics） |
|--|---------|----------------------|
| 触发 | `run.ps1` / MCP 自动 | 用户 `settings.json` 或 `MMOD_DIAGNOSTICS` |
| NDJSON | `%TEMP%\mirroredge-debug\<id>.ndjson` | 优先 `<game>/logs/<id>/session.ndjson` |
| 文本日志 | 仅 `mod_log` 镜像到 NDJSON | 专用 `session.log` |
| 收集 | MCP `debug_tail_log` | `collect-diagnostics.ps1` |

Harness 的 `Initialize-DebugSession` 会同时设置 `MMOD_SESSION_LOG`，便于与手动诊断使用同一收集脚本。

## 故障排查速查

| 现象 | 检查 |
|------|------|
| 没有 `logs/` 目录 | 是否通过 Module Launcher 启动；`diagnostics.enabled` 或 `MMOD_DIAGNOSTICS=1` 是否在启动前生效 |
| `session.log` 为空 | 代理是否加载成功；`logs/last-session.json` 路径是否与当前会话一致 |
| 只有 launcher 行、无 mod 行 | 游戏未跑到 module_manager；查 `session.ndjson` 中 `d3d9proxy` / `module_manager` 事件 |
| 其他机器路径不同 | 收集脚本传 `-GameRoot`；manifest 内带 `gameRoot` 字段 |

更多症状表见 [troubleshooting.md](troubleshooting.md)。

## 源码索引

| 文件 | 职责 |
|------|------|
| `shared/session_file_log.h` | 文本日志落地 |
| `shared/debug_log.h` | NDJSON + `AgentDiagnosticsEnabled` |
| `launcher/diagnostics_session.cpp` | 启动前配置 env + manifest |
| `launcher/log_server.cpp` | 启动器 UI 日志中继到 `session.log` |
| `shared/deploy_settings.cpp` | `diagnostics.enabled` |
| `tools/collect-diagnostics.ps1` | 跨环境收集 zip |
