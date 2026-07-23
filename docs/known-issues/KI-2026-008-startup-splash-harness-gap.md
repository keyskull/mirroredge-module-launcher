# KI-2026-008: Startup splash stall not detected by harness

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-008 |
| **状态** | `resolved` |
| **首次记录** | 2026-07-03 |
| **最后验证** | 2026-07-04 |
| **区域** | harness / module_manager |
| **标签** | `startup`, `splash`, `hang-guard`, `false-negative` |

## 症状（Symptoms）

游戏停在 Mirror's Edge 启动 splash（窗口已出现但主菜单未进入），launcher 日志已经显示：

- `module_manager ... ready`
- `core ready`
- 可见模块加载完成或自动加载被跳过

旧 harness 仍可能判定通过，因为 `smoke-split` 只等 `hooks_installed`，`inject-mp` 只等 `core` / `engine.modReady`，没有要求游戏离开启动 splash 或产生 main-menu/map 信号。

## 根因（Root cause，须已验证）

- Harness 根因：启动阶段成功条件过低。`smoke-split` / `inject-mp` 在 hooks/core ready 后即可 PASS，没有验证游戏是否离开静态 splash，也没有覆盖 `PrintWindow` 卡住或 pre-hooks 窗口无响应。
- Runtime 根因：proxy 模式过早安装 `PeekMessage`/`GetMessage` bootstrap，并在 `CreateDevice` 后很快 patch D3D device vtable；在 splash/首帧阶段会让主窗口停在启动画面。core 过早 auto-load 也会放大该症状。
- 诊断细节：禁用 core auto-load 后仍复现，证明根因先于 core；跳过 proxy 模式 message bootstrap 并延迟 proxy-device presentation hook 后，游戏可进入教程关卡。

## 已验证修复（Verified fix）

- `tools/debug-harness/lib/DebugHarness.psm1` 新增 `Assert-GameBootProgress`
- `smoke-split` 在 hooks ready 后调用 boot-progress 断言；`inject-mp` 在 core ready 后调用 boot-progress 断言
- `Assert-GameBootProgress` 接受两类成功信号：`currentMap`/main-menu 状态，或多次动态帧变化（证明没有停在静态 splash）
- `Assert-GameBootProgress` 对 `IsHungAppWindow` 做连续多次 debounce，避免一次瞬时 `SendMessageTimeout` 误判；窗口已挂起时跳过 `PrintWindow`，避免检测器自身卡住
- `Wait-ManagerHooksReady` 在 hooks 尚未 ready 且窗口无响应时失败并导出 `manager-hooks` bundle
- `runtime/module_manager` proxy 模式跳过早期 message bootstrap；由 module_manager pump thread 使用 proxy device 延迟安装 presentation hooks
- `runtime/module_manager` core auto-load 等待 presentation hooks 后至少出现第一帧，再加载 core

验证：`powershell -NoProfile -ExecutionPolicy Bypass -File tools/debug-harness/run.ps1 smoke-split -SkipBuild` 通过（2026-07-04，约 49s），boot-progress 在 3 次动态帧变化后通过；截图确认已进入教程关卡。

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-07-03 | 将问题归因于 `ValidateRuntime` / FNV code probe gate | 无效 | 正常启动路径不调用 `ValidateRuntime`，`ValidateGameBinary` 和 `ValidateClassLayouts` 也只被 `ValidateRuntime` 调用 |
| 2026-07-03 | 延后 `engine_module_client.cpp` 的 `exports.resolved = true` | 无效 | 该路径影响 launcher GET_STATUS/engine export 缓存，不解释游戏已 splash + module/core ready 的假通过 |
| 2026-07-04 | 只延迟 core auto-load，等待 first render frame | 不充分 | 证明 core 不是首因；游戏仍可在 presentation hook / message bootstrap 阶段卡住 |
| 2026-07-04 | `Assert-GameBootProgress` 单次 `IsHungAppWindow` 立即失败 | 不充分 | `SendMessageTimeout` 可瞬时失败；需连续多次 debounce 并结合帧变化 |

## 相关

- **源码：** `tools/debug-harness/lib/DebugHarness.psm1`
- **场景：** `smoke-split`, `inject-mp`
- **文档：** [ai-debug-harness.md](../ai-debug-harness.md)

## 变更日志

| 日期 | 作者 | 说明 |
|------|------|------|
| 2026-07-03 | Cursor AI | 创建记录；添加候选 boot-progress harness 断言 |
| 2026-07-04 | Cursor AI | 验证并修复：proxy 模式跳过 message bootstrap、延迟 proxy-device presentation hook、boot-progress 使用动态帧作为成功信号；`smoke-split -SkipBuild` 通过 |
