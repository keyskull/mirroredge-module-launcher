# KI-2026-006: Core hosted boot stuck at "init: game not ready yet"

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-006 |
| **状态** | `resolved` |
| **首次记录** | 2026-07-02 |
| **最后验证** | 2026-07-02 |
| **区域** | sdk / core / module_manager |
| **标签** | `core-bootstrap`, `probe-globals`, `borderless`, `multi-machine` |

## 症状（Symptoms）

- Module Manager shows `core` **Loaded** (`PluginInitialize OK`) but UI/harness never reaches `init: core ready`.
- Session log spam: `init: game not ready yet` for 240s+ (`runtime/core/main.cpp` hosted path).
- `GET_STATUS` may show `hooksInstalled:true`, core module `Loaded`, `gameReady:false`.
- Reproduced on **2号机** with borderless launcher settings (`settings.json` → `launcher.display.mode: borderless`).

## 根因（Root cause，须已验证）

1. **Primary:** `MeSdk::ProbeGlobals()` resolves GNames/GObjects with `assign=false`, returns `true` once, but does not set `Classes::FName::GNames`. The 100ms throttle then makes the next `IsGameReadyForModInit()` call return `false` on the main thread when `CompleteModInitialization` re-checks — infinite defer loop.
2. **Contributing (1号机 commit `192427a`):** `SyncWindowLayoutIfNeededImpl()` in `PumpFromMessageThread` runs borderless `SetWindowPos` during early boot before `core.dll` is mapped; on 2号机 borderless config this worsened message-thread timing during core init.

## 已验证修复（Verified fix）

- `shared/me_sdk/runtime/init.cpp` — cache positive `ProbeGlobals` result (`g_probeSucceeded`).
- `runtime/module_manager/presentation.cpp` — skip window layout sync until `core.dll` is loaded.
- Cross-machine alert: `test-logs/alerts/` + `post-machine-alert.ps1` (2号机 → 1号机).

Verify: launch via Module Launcher on 2号机; session log should reach `init: core ready` within ~60s of main menu.

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-07-02 | Only reduce InitWorkerHosted sleep (b295334) | Still stuck | Throttle race unchanged |
| 2026-07-02 | Block engine.dll load in GET_STATUS (8d6fe88) | Crash avoided, still stuck | ProbeGlobals race remained |

## 相关

- **源码:** `shared/me_sdk/runtime/init.cpp`, `runtime/core/main.cpp`, `runtime/module_manager/presentation.cpp`
- **1号机 commit:** `192427a` (window layout IPC / message-thread sync)
- **Harness:** `smoke-split`, `mp-core-functional`
- **Alerts:** [test-logs/alerts/README.md](../../test-logs/alerts/README.md)

## 变更日志

| 日期 | 作者 | 说明 |
|------|------|------|
| 2026-07-02 | 2号机 | 创建记录；ProbeGlobals + layout defer fix |
