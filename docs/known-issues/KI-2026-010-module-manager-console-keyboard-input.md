# KI-2026-010: Module Manager console cannot type in proxy mode

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-010 |
| **状态** | `mitigated` |
| **首次记录** | 2026-07-04 |
| **最后验证** | 2026-07-23 |
| **区域** | module_manager / input |
| **标签** | `console`, `keyboard`, `imgui`, `proxy-mode`, `ime` |

## 症状（Symptoms）

- Module Manager console overlay can open, but the input line does not accept typed characters.
- Mouse-driven overlay UI still works, because module_manager polls mouse state directly.
- Harness `Invoke-ConsoleModuleInject -TryKeyboard` may fall back to `CONSOLE_EXEC` instead of proving typed console input.

## 根因（Root cause，须已验证）

Proxy mode intentionally skips early `PeekMessage` / `GetMessage` bootstrap to avoid startup splash hangs (see KI-2026-008). That left text input without a reliable keyboard path: mouse input was polled per ImGui frame, while keyboard input depended on message hooks / `WM_CHAR`.

Additional runtime detail: polling keyboard translation from the render thread cannot rely on `GetKeyboardState()` alone, because that thread may not own the active message queue state.

## 已实施修复（Pending verification）

- `runtime/module_manager/presentation_input.cpp`
  - Keeps proxy-mode startup free of early message hooks.
  - Installs message hooks only after overlay/console starts blocking input and presentation hooks are already stable.
  - Does not call `TranslateMessage` in the hook and does not swallow keyboard messages, preserving KI-2026-003 IME constraints.
- `runtime/module_manager/presentation_imgui.cpp`
  - Adds render-frame keyboard polling alongside mouse polling.
  - Updates ImGui `KeysDown` from `GetAsyncKeyState`.
  - Translates printable keys with an async-key-derived keyboard state and queues characters with `ImGuiIO::AddInputCharacter`.
  - Suppresses polled character injection shortly after a real `WM_CHAR` to avoid duplicate characters on systems where messages are delivered.

Verification so far:

- `build.ps1` passes and deploys updated `module_manager.dll`.
- Linter diagnostics clean for touched files.
- Automated console-keyboard smoke was attempted, but the current harness run did not reach a reliable console typing assertion because startup/manager status became unstable (`manager-hooks` hang debounce and malformed manager `GET_STATUS` while core was initializing). Manual in-game verification still required.

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-07-04 | Re-enable / depend on early proxy-mode message bootstrap | Not used | KI-2026-008 shows early message bootstrap can hang the startup splash path |
| 2026-07-04 | Call `TranslateMessage` inside the message hook and swallow keyboard | Rejected | KI-2026-003 lists this as an IME regression path |
| 2026-07-04 | Rely on `GetKeyboardState()` from render-thread polling | Insufficient | Render thread may not observe the active keyboard queue state |

## 相关

- **源码：** `runtime/module_manager/presentation_input.cpp`
- **源码：** `runtime/module_manager/presentation_imgui.cpp`
- **源码：** `runtime/module_manager/presentation.cpp`
- **文档：** [troubleshooting.md](../troubleshooting.md#console-opens-but-cannot-type)
- **相关 KI：** [KI-2026-003](KI-2026-003-chinese-ime-stuck.md), [KI-2026-008](KI-2026-008-startup-splash-harness-gap.md)

## 变更日志

| 日期 | 作者 | 说明 |
|------|------|------|
| 2026-07-04 | console input fix session | 创建记录；记录延迟 input hooks + ImGui keyboard polling 修复，待人工验证 |
