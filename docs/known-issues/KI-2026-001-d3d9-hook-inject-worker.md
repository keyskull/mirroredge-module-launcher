# KI-2026-001: D3D9 hook / 设备探测不得在 inject worker 线程

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-001 |
| **状态** | resolved |
| **首次记录** | 2026-06 |
| **最后验证** | 2026-06 |
| **区域** | mmultiplayer / d3d9 |
| **标签** | `d3d9`, `inject`, `render-thread` |

## 症状（Symptoms）

- 注入后数秒内游戏崩溃或渲染线程异常
- 日志可能在 `d3d9.dll` 已加载后出现
- 常见于 legacy mmultiplayer 直接注入路径

## 根因（Root cause，须已验证）

在 **inject worker 线程**上对 `Direct3DCreate9` 做 `TrampolineHook`，或从该线程探测/扫描已存在的 D3D9 device，会在渲染线程上触发崩溃（含 R6025 类失败，SEH 无法可靠捕获）。

## 已验证修复（Verified fix）

正确流程见 `legacy/mmultiplayer/engine.cpp`：

1. `InstallPeekMessageBootstrap` — 在主线程消息路径 hook `PeekMessage` / `GetMessage`
2. `InstallRendererCapture` — 若 `d3d9.dll` 已加载则**立即返回**
3. `TryLazyPresentationHook` — 注入后约 12s、前台、45 帧稳定后再装 `EndScene` / `Present`

Split 模式：使用 proxy `OnProxyDeviceCreated`，禁止 inject worker 碰 D3D。

**验证：** 主菜单停留 12s+ 后 overlay 出现；无注入后即时崩溃。

**文档：** [troubleshooting.md](../troubleshooting.md#render-thread-crash-after-d3d9-loads-inject-mode)、[mmultiplayer-mod.md](../mmultiplayer-mod.md)

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-06 前 | InitWorker 中 hook `Direct3DCreate9` | 崩溃 | 错误线程；渲染线程 fault |
| 2026-06 前 | `FindExistingD3D9Device` / 从 inject 线程扫描 live device | 崩溃 | 同上 |
| 2026-06 前 | inject 后立即 `FindDeviceSafe` 装 presentation hook | 不稳定/崩溃 | 设备未就绪或线程错误 |

## 相关

- **源码：** `legacy/mmultiplayer/engine.cpp`
- **约束：** [AGENTS.md](../../AGENTS.md) — D3D9 hook safety

## 变更日志

| 日期 | 说明 |
|------|------|
| 2026-06 | 根因验证；lazy hook + message bootstrap |
| 2026-06-29 | 迁入 known-issues 登记册 |
