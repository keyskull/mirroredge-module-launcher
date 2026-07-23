# KI-2026-003: 注入后中文 IME 全局失效

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-003 |
| **状态** | resolved |
| **首次记录** | 2026-06 |
| **最后验证** | 2026-06 |
| **区域** | module_manager / mmultiplayer |
| **标签** | `ime`, `input`, `peekmessage`, `alt-tab` |

## 症状（Symptoms）

- 注入或打开 Module Manager 后 Alt+Tab，其他程序间歇性失去键鼠；**中文 IME** 全局失效直至关游戏或重启 `TextInputHost.exe`
- 英文输入或鼠标有时在 refocus 后恢复

## 根因（Root cause，须已验证）

`module_manager` 的 `PeekMessage`/`GetMessage` hook 在 overlay 打开时：

1. 将键盘消息 **queue 到渲染线程** 并 off-thread 调用 `ImGui_ImplWin32_WndProcHandler`，破坏 Text Input Application / 全局 IME
2. Alt+Tab 时 device `DEVICELOST`，`SyncInputBlockWithForeground` 仅在 `D3D_OK` 的 EndScene 路径运行，可能错过 `WM_ACTIVATEAPP`

## 已验证修复（Verified fix）

`shared/win_input.h` + `runtime/module_manager/presentation.cpp`（legacy mmultiplayer：`engine.cpp` 同规则）：

1. 不吞 `WM_IME_*` / `WM_INPUT`；hook 内不调用 `TranslateMessage`
2. `blockInput`/菜单打开：仅吞 **鼠标**；键盘走 `DispatchMessage` 供 IME
3. 被吞的 **鼠标** 在 **消息线程** 转发给 `ImGui_ImplWin32_WndProcHandler`（勿 queue 键鼠到渲染线程）
4. 失焦/关菜单：`ImmNotifyIME(CPS_CANCEL)` + `ClipCursor(nullptr)`
5. `PollUnfocusInputRelease()` 从 message hook 与 `ProcessLostFrameSideEffects` 调用
6. `WinInput_ShutdownForProcessDetach()` 于所有 `DLL_PROCESS_DETACH`；`ApplyImGuiInputReset` 清 `io.KeysDown`

**验证：** 开菜单 → Alt+Tab 到 Notepad/微信 → IME 正常；关游戏后其他程序立即可输入。

**文档：** [troubleshooting.md](../troubleshooting.md#chinese-ime-stuck-system-wide-after-inject--alt-tab)

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-06 前 | 菜单打开时 swallow 全部键盘消息 | IME 全局坏 | Text Input Application 无法工作 |
| 2026-06 前 | 键盘/鼠标 queue 到 render thread 再喂 ImGui | IME 全局坏 | WndProc 必须在 UI 线程 |
| 2026-06 前 | hook 内 `TranslateMessage` | IME 异常 | 双重翻译 |
| 2026-06 前 | 仅 EndScene 路径做 unfocus 清理 | 间歇失效 | DEVICELOST 时错过 ACTIVATEAPP |

## 相关

- **源码：** `shared/win_input.h`, `runtime/module_manager/presentation.cpp`, `runtime/engine/engine.cpp` (archived: `legacy/mmultiplayer/engine.cpp`)
- **临时缓解：** 结束 `TextInputHost.exe`；launcher 开著退出游戏时 `InputRestore::RestoreDesktopNow()`

## 变更日志

| 日期 | 说明 |
|------|------|
| 2026-06 | 验证修复 |
| 2026-06-29 | 迁入 known-issues 登记册 |
