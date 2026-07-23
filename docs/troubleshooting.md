# Troubleshooting

Symptom → cause → fix reference for launcher, injection, and in-game modules (core, engine, feature mods).

**防重复修复：** 动手改代码前先查 [known-issues 登记册](known-issues/README.md) 与 [known-issues-workflow.md](known-issues-workflow.md)。复杂问题须记录 **已尝试且无效** 的方案（KI 条目），勿重试表中方案。

## Launcher / deploy

### Game not found / wrong path

**Symptoms:** `未找到游戏`, deploy warning `MirrorsEdge.exe not found`.

**Causes:**

- `deploy.config.json` `deployPath` wrong (build/deploy only)
- Launcher exe not in game root or ancestor of `Binaries\MirrorsEdge.exe`
- Game installed on another drive; auto-detect missed it

**Fix:**

1. In the launcher UI, click **浏览...** and select the game root (folder containing `Binaries\`) or the `Binaries` folder directly.
2. Saved path: `<gameRoot>\settings.json` (`launcher.gameRoot`; legacy `%TEMP%\mirroredge-launcher.settings` migrates on first launch).
3. Or set runtime env `ME_GAME_PATH` / `ME_DEPLOY_PATH` to the game root.
4. For `build.ps1` deploy: set `deployPath` in `deploy.config.json`, or `ME_DEPLOY_PATH`.
5. Path logic: `launcher/game_path.cpp` (auto-detect) then `launcher/paths.cpp` (fallback walk from launcher exe).

### DefaultEngine.ini / DefaultGame.ini corrupt

**Symptoms:** Dialog: `The file ..\TdGame\Config\DefaultEngine.ini is corrupt` (or `DefaultGame.ini`).

**Cause:** Mirror's Edge validates install-time `TdGame\Config\Default*.ini` files. Any edit (resolution tweaks, copied installs, partial repair) triggers this. The module launcher does **not** modify these files.

**Fix:**

| Goal | Action |
|------|--------|
| Restore vanilla configs | EA App / Steam → **Repair** or verify game files |
| Keep edited or failing `Default*.ini` checks | Launch via **Module Launcher** (not double-click / Steam directly). The launcher **always** applies `launcher/config_integrity_bypass.cpp` in-memory patches at **CREATE_SUSPENDED** launch (`TryApplyBeforeFirstRun`) unless `MMOD_DISABLE_CONFIG_BYPASS=1`. User settings belong in `Documents\EA Games\Mirror's Edge\TdGame\Config\TdEngine.ini`, not `Default*.ini`. |

**Harness / automation:** `Assert-NoBlockingGameDialogs` polls MirrorsEdge top-level windows during `Wait-GameWindow`, `Start-SplitInjectionSession`, and `Wait-ManagerHooksReady`. Failures include dialog class, title, body text, rule id, and remediation (not a generic timeout). Known rules: `corrupt-default-ini`, `securom-disc`, `ue3-fatal`. See `docs/ai-debug-harness.md`.

### "No CD/DVD inserted" (SecuROM)

**Symptoms:** Game exits or shows a disc-check error on a PC without an optical drive, often after copying an old EA/Origin install to a new machine.

**Cause:** Legacy **SecuROM** protection in `Binaries\MirrorsEdge.exe`. The launcher does not cause this; it starts the same executable as double-clicking the game. Steam builds after ~2015 ship a SecuROM-free exe.

**Fix (user setup, not launcher deploy):**

| Source | Action |
|--------|--------|
| **Steam** | Library → Mirror's Edge → Properties → Local Files → **Verify integrity of game files** (replaces exe with SecuROM-free build). |
| **EA App / old Origin** | Reinstall from EA App, or apply [Mirror's Edge Origin Fix](https://community.pcgamingwiki.com/files/file/2260-mirrors-edge-origin-fix/) (replaces exe; user choice). |
| **Copied folder** | Do not copy only the game folder from an old PC; reinstall from your store or verify/repair so `MirrorsEdge.exe` is updated. |

The launcher logs a **SecuROM warning** before launch when it detects the protection string in the exe. See `GamePath::HasSecuRomProtection` in `launcher/game_path.cpp`.

### Build fails: MSBuild / DirectX SDK

**Symptoms:** `MSBuild not found`, d3dx9 link errors.

**Fix:**

- Install VS 2022 with **Desktop development with C++**
- Install [DirectX SDK June 2010](https://www.microsoft.com/en-us/download/details.aspx?id=6812) for `engine.dll`

### Stale files in Binaries

**Symptoms:** Old `Client.dll`, duplicate launcher exes, wrong mod loads.

**Fix:** `build.ps1` `Invoke-Deploy` removes stale `Binaries\Client.dll`, launcher exes in Binaries. Re-run deploy or delete `Binaries\Client.dll` manually.

### MirrorsEdge suspended / cannot kill (build or harness stuck)

**Symptoms:** Task Manager shows **Suspended**; `Stop-Process` / `taskkill` returns **Access denied**; `build.ps1` fails with game still running.

**Cause (two patterns):**

1. **Suspended threads** — `SuspendThread` during hook install (`Hook::WithSuspendedThreads`, legacy crash handler). Force-kill mid-suspend leaves the game unresponsive.
2. **Zombie EPROCESS** — game **crashed** or harness **`Stop-Process -Force` / raw `taskkill /F`** while threads were suspended (`CREATE_SUSPENDED` launch window, or hook install). Task Manager shows ~20 KB, **0 threads**, `taskkill` → Access denied. Only **reboot** clears the PID.

**Fix:**

```powershell
.\tools\stop-game.ps1
# elevated (SeDebugPrivilege):
.\tools\stop-game.ps1 -Elevate
# include launcher:
.\tools\stop-game.ps1 -IncludeLauncher
```

This resumes threads (when any exist), waits briefly, then `taskkill /F` (skipped for 0-thread zombies), then `TerminateProcess` with `SeDebugPrivilege`, then WMI `Terminate`. `build.ps1` attempts the same automatically before reporting a lock on `d3d9.dll`.

If PID still listed with **0 threads**: reboot — Windows kept a defunct EPROCESS. Do **not** retry raw `taskkill` on a 0-thread PID; it cannot help and may wedge cleanup further.

**Code-side mitigation:** `TrampolineHook` no longer uses process-wide `SuspendThread`. Launcher `CREATE_SUSPENDED` bypass uses an RAII guard so the primary thread is always resumed. Harness `run.ps1` calls `Stop-HarnessGameSession` in a `finally` block (resume + terminate, never bare `Stop-Process -Force` on MirrorsEdge). `Hook::ReleaseProcessThreadSuspensions()` runs on DLL detach.

---

## Injection / proxy load

### module_manager never ready / timeout

**Symptoms:** Waits 180s, launcher shows `等待 module_manager 超时`.

**Causes & fixes:**

| Log step | Meaning | Action |
|----------|---------|--------|
| 等待 Module Manager 就绪 | Proxy not loaded yet | Enter main menu; wait for D3D9 init |
| 等待 module_manager 超时 | No proxy or wrong d3d9.dll | Confirm `Binaries\d3d9.dll` is our proxy from deploy |
| 未找到游戏进程 | Game not running | Start via launcher button |

**Also check:**

- Close game before `build.ps1` (locked `d3d9.dll` causes link failure)
- Start game **after** launcher deploys proxy (not before)
- Game process name is `MirrorsEdge.exe` (`launcher/config.h`)

### Game crashes after proxy load

**Symptoms:** `游戏在注入后退出`, process dies within 2s of inject.

**Common causes:**

1. **Conflicting `Binaries\d3d9.dll`** — DXVK, ReShade, old proxy, or third-party wrapper
2. **Mod hooks D3D9 from wrong thread** — see D3D9 section below
3. **Duplicate mod DLL** in `Binaries\` and `modules\`

**Fix:**

1. Remove conflicting third-party `Binaries\d3d9.dll` wrappers (backup as `.mmproxy.bak`)
2. Remove stray `core.dll` / `mmultiplayer.dll` / `Client.dll` from `Binaries\`
3. Only load plugins from `modules\` via Module Manager
4. Rebuild mod after engine hook changes

### Mod DLL packing / obfuscation

**Policy:** A broken or packed mod should **fail with a status/log line**, not take down `MirrorsEdge.exe`.

**What is guarded (SEH):**

- `d3d9` proxy: `LoadLibrary` of `module_manager.dll`, `MmOnD3D9DeviceCreated` notify
- Module Manager: `LoadLibrary` per plugin, `MMOD_PluginInitialize` / shutdown
- core / engine / module_manager: feature mod `Enable` / `Disable`, gameplay hook install, post-init gameplay/render/input/host presentation callback dispatch
- feature plugins: multiplayer listener/status/UDP background threads; crashes are logged and network work is disabled without killing the game

**If you pack a mod DLL**, preserve required exports (`dumpbin /exports`):

- Plugins: `MMOD_PluginInitialize`, `MMOD_PluginShutdown`
- `module_manager.dll`: undecorated `MmOnD3D9DeviceCreated`
- Proxy path: `Direct3DCreate9` on `d3d9.dll`

Avoid packers that rewrite the PE import table or strip exports. After packing, load from Module Manager → Modules tab; expect `LoadLibrary crashed`, `PluginInitialize crashed`, or a `[memory_fault]` callback/thread entry on failure, not a game exit.

### Ready event timeout

**Symptoms:** Launcher waits 30s on `Local\module_manager_ready`, then continues after 10s fallback.

**Causes:**

- Mod init failed before `Engine::MarkReady()`
- Mod init still running (SDK/menu failure)

**Fix:**

- Keep launcher open; check `[mod]` log lines:
  - `init: InitializeSDK failed` — game patterns outdated
  - `init: Menu::Initialize failed` — presentation hooks not installed
  - `init: game not ready yet` — GNames/GObjects not valid, or **ProbeGlobals throttle race** after core Loaded (see [KI-2026-006](known-issues/KI-2026-006-core-boot-probe-globals.md))
- Wait longer; init worker polls up to 240s hosted / 90s standalone
- **2号机 borderless:** if core shows Loaded but never `init: core ready`, pull latest (ProbeGlobals cache + defer layout until core.dll mapped); check `test-logs/alerts/` for peer machine notes

### Stuck on startup splash but modules are ready

**Symptoms:** The Mirror's Edge splash window is visible and the launcher log already shows `module_manager` / `core ready`, but the game never reaches the main menu. Older harness runs could pass `smoke-split` or `inject-mp` because hooks/core ready was treated as enough.

**Fix / harness check:** `smoke-split` and `inject-mp` now run `Assert-GameBootProgress`, which polls map status and frame changes after hooks/core ready. A static splash before any main-menu/map signal fails as `boot-progress` and exports a hang bundle. See [KI-2026-008](known-issues/KI-2026-008-startup-splash-harness-gap.md).

### `/auto` mode exits non-zero

**Fix:** Start game before or with launcher; wait for main menu / D3D9 init; check proxy deploy log.

---

## module_manager / proxy load

### Overlay not ready / `waiting for d3d9 proxy device`

**Symptoms:** `[mod] [manager] waiting for d3d9 proxy device`, Insert/F10 shows `overlay not ready`.

**Causes:**

1. **`MmOnD3D9DeviceCreated` export missing** — debug log shows `notify_missing_export`; `dumpbin /exports module_manager.dll` shows only `_MmOnD3D9DeviceCreated@4`
2. **Proxy not deployed** — game uses system d3d9 or inject path without proxy device
3. **CreateDevice before manager loaded** — fixed by `MmProxyRetryDeviceNotify` + cached device (ensure current build)
4. **Borderless splash delay** — d3d9 proxy preloads `module_manager.dll` on `Direct3DCreate9` (not only `CreateDevice`) so `module_manager_ready` can signal while the game is still on startup logos; device notify still runs when `CreateDevice` completes.

**Fix:**

1. Rebuild with `runtime/module_manager/module_manager.def` linked in vcxproj
2. Verify export: `MmOnD3D9DeviceCreated = _MmOnD3D9DeviceCreated@4`
3. Close game; run launcher; start game via launcher (not before deploy)
4. Confirm `Binaries\d3d9.dll` is our proxy (~83KB from `dist/d3d9.dll`)

See [module-manager.md](module-manager.md).

### Game auto-exit after inject (split mode)

**Symptoms:** Game closes shortly after module_manager inject; no overlay.

**Cause:** `FindDeviceSafe` / pattern scan when proxy active but device not received — R6025 from bad `GetCreationParameters` (SEH cannot catch).

**Fix:** Ensure proxy device path works (export fix above). When `IsGameProxyD3D9Active()`, presentation code must **not** call `FindDeviceSafe`. Failed approaches: [KI-2026-004](known-issues/KI-2026-004-find-device-proxy-mode.md).

### Game shows "Message" dialog — Remote Desktop detected

**Symptoms:** Game window title is "Message" instead of "Mirror's Edge". Window enumeration shows `Class=#32770 Title="Message"` with child text `"This game cannot run with 3D graphics over a Remote Desktop connection."` `create_device_enter` / `create_device_return` never appear in NDJSON logs. `module_manager` PumpThread only shows heartbeat, `wait_proxy_device` never resolves. `core` PING always times out. **Occurs with and without d3d9 proxy** — vanilla game has the same behavior.

**Cause:** Mirror's Edge calls `GetSystemMetrics(SM_REMOTESESSION)` (0x1000) at startup and displays a fatal error dialog when detecting a Remote Desktop session. The dialog blocks the message pump before `IDirect3D9::CreateDevice` is ever called.

**Attempted bypasses (ALL FAILED):** IAT patching `GetSystemMetrics` + `SystemParametersInfoW` across all modules (15+12 entries), inline hooking (crashes due to non-standard prologue), and prologue-verified inline hook (prologue mismatch — harmless but IAT still insufficient). The game uses `GetProcAddress`/ordinals to bypass all IAT patches. See [KI-2026-011](known-issues/KI-2026-011-rdp-block-creatdevice.md) for full history.

**Fix:** Do not use Remote Desktop to connect to the game machine. Use an alternative remote solution that does not trigger `SM_REMOTESESSION`:
- **Parsec** (free, designed for game streaming)
- **Moonlight + Sunshine** (open-source, based on NVIDIA GameStream)
- **Steam Remote Play**
- **Physical console** (local login)

**Harness:** `tools/mp-real-level-bots.ps1` fail-fast exits when `GetSystemMetrics(SM_REMOTESESSION)!=0` (and aborts if the ME "Message" dialog appears during hooks/core wait) so RDP sessions do not burn ~120s waiting for hooks that never install.

### Module Manager menu works; core not loaded

**Expected:** **core** is **not** auto-injected. Use Module Manager → **Modules** tab → inject **core** manually after main menu (core loads `engine.dll`).

**Inject fails in Modules tab:**

| Status | Cause | Fix |
|--------|-------|-----|
| `Missing MMOD_PluginInitialize export` | Stale or wrong DLL | Rebuild core; verify export with `dumpbin /exports core.dll` |
| `PluginInitialize returned false` | Host API version mismatch (need v2 + `ui`) | Rebuild module_manager + core together |
| `PluginInitialize crashed` | Init worker exception | Open console (`` ` ``) and check `[core]` lines; ensure game is in main menu / level (GNames valid) |
| `LoadLibrary crashed (0x…)` | Bad/packed DLL `DllMain` or loader fault | Module marked failed in Modules tab; game should keep running — check `%TEMP%\mirroredge-mod.log` |
| `init: Failed to find GNames/GObjects` | Pattern scan failed (loading screen) | Wait until main menu, then inject again |

**After successful inject:** Core adds **Engine / World** menu tabs; enable **multiplayer** / **trainer** / **dolly** from the Modules tab. Console (`` ` ``): `status` shows manager + engine JSON.

If selecting the **Engine** tab crashes the game, check for naked UE3 object lookups in tab rendering. Engine tab state must use `MeSdk::Safe::Gui::TryFindTdGameEngine(true)` plus `TryReadEngineMenuState()` rather than `Engine::GetEngine()` directly. See [KI-2026-009](known-issues/KI-2026-009-engine-tab-unsafe-engine-scan.md).

### Console opens but cannot type

**Symptoms:** Pressing `` ` `` opens the Module Manager console, but typed characters do not appear in the input line. Mouse-driven overlay controls may still work.

**Cause:** In proxy mode, early `PeekMessage` / `GetMessage` bootstrap is intentionally skipped to avoid startup splash hangs. Keyboard text therefore needs a delayed input path after overlay hooks are stable; mouse polling alone is not enough for ImGui `InputText`.

**Fix:** Rebuild and redeploy `module_manager.dll`. The current runtime installs message hooks only after overlay/console input blocking starts and also polls keyboard state per ImGui frame, while preserving the KI-2026-003 rule: no hook-time `TranslateMessage`, no IME/raw-input swallowing. See [KI-2026-010](known-issues/KI-2026-010-module-manager-console-keyboard-input.md).

### Alt+Tab freezes game (Module Manager loaded)

**Symptoms:** Without menu, Alt+Tab is fine. With **Insert/F10 menu open**, Alt+Tab away and back hangs the game. Debug log may stop after `WM_ACTIVATEAPP` / `after_activate_focus` with device still `DEVICELOST` (`0x88760808`), no `device_recovered`.

**Root cause (verified 2026-06):** Menu open → ImGui holds extra D3D9 pool objects. On Alt+Tab the device goes `DEVICELOST` while objects are not released until `DEVICENOTRESET` (too late — game `Reset()` blocks). Separately, running `PumpFromMessageThread()` from PeekMessage/GetMessage after hooks causes message/render thread deadlock on focus return.

**Fix (in `presentation.cpp`):**

1. `PumpPreHookBootstrap()` only while `!g_hooksInstalled`; full pump only from EndScene when `D3D_OK`.
2. Present hook: **always** `UpdateStability()` first (match `runtime/engine`).
3. Invalidate ImGui on `DEVICELOST` entry, on `DEVICENOTRESET`, and on unfocus/menu hide.
4. EndScene lost path: bypass overlay; minimal side effects only.

**Verify:** `debug-0f3242.log` shows `imgui_unfocus_hide` or `imgui_lost_invalidate`, then `device_recovered` / `imgui_device_reset`. See [module-manager.md](module-manager.md) Alt+Tab section. Failed approaches: [KI-2026-002](known-issues/KI-2026-002-alt-tab-device-lost.md).

### Chinese IME stuck system-wide after inject / Alt+Tab

**Symptoms:** After injection (or opening Module Manager menu then switching apps), other programs lose mouse/keyboard control intermittently. English input or mouse may recover when refocusing a window; **Chinese IME** stays broken until the game is closed or **Text Input Application** (`TextInputHost.exe`) is restarted from Task Manager.

**Root cause:** `PeekMessage` / `GetMessage` hooks in `module_manager` mishandled input while the overlay menu was open:

1. Keyboard messages were **queued to the render thread** (`QueueImGuiWin32Message`) and passed to `ImGui_ImplWin32_WndProcHandler` off the UI thread — this corrupts global IME (Text Input Application).
2. On Alt+Tab the D3D device goes `DEVICELOST`; `SyncInputBlockWithForeground` only ran on the `D3D_OK` EndScene path, so `blockInput` / IME cleanup could be skipped if `WM_ACTIVATEAPP` was missed.

**Fix (2026-06):** `shared/win_input.h` + `presentation.cpp`:

1. Never swallow `WM_IME_*` or `WM_INPUT`
2. Do not call `TranslateMessage` inside message hooks
3. When `blockInput` / menu open: swallow **mouse only**; keyboard reaches `DispatchMessage` for IME
4. Forward swallowed **mouse** to `ImGui_ImplWin32_WndProcHandler` on the **message thread** before `WM_NULL` (clicks need WndProcHandler; do **not** queue mouse/keyboard to the render thread)
5. `ImmNotifyIME(CPS_CANCEL)` + `ClipCursor(nullptr)` on focus loss / menu close
6. `PollUnfocusInputRelease()` from message hooks **and** `ProcessLostFrameSideEffects` (DEVICELOST path)
7. `WinInput_ShutdownForProcessDetach()` on **every** `DLL_PROCESS_DETACH` (including process terminate — do not gate on `reserved == nullptr`) and on `WM_CLOSE` / `WM_DESTROY` while the game window still has a message pump
8. Clear `io.KeysDown` in `ApplyImGuiInputReset`
9. Legacy monolith inject-only (`legacy/mmultiplayer/engine.cpp`): same pass-through rules in `runtime/engine/engine.cpp`

**Workaround if already stuck:** Close Mirror's Edge, then restart **Text Input Application** (Task Manager → Details → end `TextInputHost.exe`; Windows restarts it automatically). With the launcher open, exiting the game also triggers `InputRestore::RestoreDesktopNow()` (shell focus nudge — same effect as clicking the taskbar).

**Verify:** Open Module Manager (Insert/F10), click tabs/buttons, Alt+Tab to Notepad/WeChat, confirm IME works. Exit game with launcher still open — other apps should accept input immediately without clicking the taskbar. Failed approaches: [KI-2026-003](known-issues/KI-2026-003-chinese-ime-stuck.md).

---

## D3D9 / renderer

### Render-thread crash after d3d9 loads (inject mode)

**Root cause (verified 2026-06):** Calling `TrampolineHook` on `Direct3DCreate9` or probing live D3D device from **inject worker thread** crashes the render thread.

**Correct flow** (split mode: `runtime/module_manager/presentation.cpp`; legacy: `legacy/mmultiplayer/engine.cpp`):

1. `InstallPeekMessageBootstrap` — hook `PeekMessage`/`GetMessage` on main thread path
2. `InstallRendererCapture` — **returns immediately** if `d3d9.dll` already loaded
3. `TryLazyPresentationHook` — installs `EndScene`/`Present` hooks when: 12s since inject, foreground, 45 stable frames

**Do not reintroduce** `FindExistingD3D9Device` or `Direct3DCreate9` hooks from `InitWorker`. Full history: [KI-2026-001](known-issues/KI-2026-001-d3d9-hook-inject-worker.md).

### Menu / overlay never appears

**Symptoms:** Mod ready in logs, no ImGui, Insert/F10 does nothing.

**Causes:**

- Presentation hooks not installed (`Engine::ArePresentationHooksInstalled()` false)
- Game never foreground long enough for lazy hook
- Proxy mode mismatch

**Fix:**

1. Bring game to foreground after inject; wait ~12s+ on main menu
2. Check `[mod]` for `inject: installing message bootstrap`
3. Proxy mode: ensure `MmOnD3D9DeviceCreated` called — see [d3d9proxy.md](d3d9proxy.md)

### Proxy mode: mod not loaded

**Symptoms:** Game runs, no mod, system d3d9 or wrong DLL in Binaries.

**Fix:**

1. Confirm `Binaries\d3d9.dll` is **our** proxy (small file, from `dist/d3d9.dll`)
2. Confirm `modules\module_manager\module_manager.dll` and `modules\core\core.dll` exist under the game `modules\` tree
3. Enable via `.\build.ps1 -DeployProxy` or `deployGraphicsProxy = true`

---

## UE3 SDK (`shared/me_sdk/`)

### Wrong offset, params, or patterns (fix the SDK, not the mod)

**Policy:** If testing on ME 1.0 shows the SDK is wrong, patch `shared/me_sdk/` directly. Avoid mod-specific hacks in `runtime/module_manager/` or `legacy/mmultiplayer/` when the root cause is the dump or global init patterns.

**Symptoms → fix:**

| Symptom | Where to patch |
|---------|----------------|
| `init: Failed to find GNames/GObjects` on main menu / in level | `shared/me_sdk/runtime/patterns_globals.h` — check `engine.sdkError` / `engine.sdkErrorName` on core `GET_STATUS` |
| `sdkError` / `GameImageSizeMismatch` / `GameCodeProbeMismatch` | Wrong game build — ME 1.0 retail/Steam only; see `runtime/game_signature.h` (`sdkImageSize`, `codeProbeFnv` in NDJSON when `MMOD_DEBUG_SESSION` set) |
| Access violation reading/writing a known field (e.g. `MouseSensitivity`, viewport) | `generated/ME_*_classes.hpp` — correct member offset and class size |
| `ProcessEvent` call succeeds but values are wrong | `ME_*_parameters.hpp` — fix param struct for that UFunction |
| Crash or empty name from `FName("...")` before globals init | Call `MeSdk::ProbeGlobals()` / `InitializeGlobals()` first; `FName` no-ops when `GNames` is null |
| `FNameSampleInvalid` / `GNamesArrayInvalid` | Pattern hit but tables not populated (still loading) or bad dump — `MeSdk::ValidateRuntime()` for full gate |
| `FindObject<UFunction>(…)` returns null for a function that exists | Regenerate or add wrapper in `ME_*_functions.cpp` / classes header |

**Verify:** Compare live object layout to the header (ReClass, Cheat Engine, or log the pointer + offset read). Rebuild every DLL that compiles the changed SDK units (`module_manager`, `core`, `engine`, feature mods).

---

## Borderless window / mouse

### Screen flicker when launching via launcher

**Symptoms:** Visible flashing or black frames during startup (loading / main menu), especially with default **borderless** display mode.

**Cause:** Launcher default is borderless at 50% screen scale. The d3d9 proxy and `module_manager` both adjust HWND size and D3D9 backbuffer on startup. A bug in early builds retried `SetWindowPos` + `device->Reset()` on **every Present frame** until layout stabilized, which looks like rapid flicker.

**Fix / workarounds:**

1. Rebuild and redeploy `module_manager.dll` (throttle fix in `borderless_window.cpp`).
2. In the launcher UI, try **Display → Windowed** or **Fullscreen** instead of borderless.
3. Borderless: set window scale to **100%** or enable **match window** resolution to reduce resize/reset churn.
4. Flicker that continues **in-game** after the main menu is stable is a different issue — check Alt+Tab / device loss in [module-manager.md](module-manager.md).

### Mouse range does not match window

**Symptoms:** In borderless/scaled window mode, cursor hits screen edges before reaching game UI edges, or clicks land in the wrong place.

**Cause:** UE3 maps mouse input to `TdEngine.ini` `ResX`/`ResY` and the D3D backbuffer. If the HWND client area is smaller (e.g. 50% scale) but `ResX`/`ResY` or backbuffer stay at 1920×1080, coordinates diverge.

**Fix (built-in):** Borderless mode keeps **window client size = `Scale` × monitor**. Render resolution defaults to the same size (**匹配窗口 / Match window**); optional presets set independent `ResX`/`ResY` for backbuffer and UE3 viewport. Launcher writes matching values on start; `module_manager` resets D3D9 to the render size, syncs `TdEngine.ini`, and calls engine `setres WxH` when the viewport drifts (continues even if `Reset` fails). `ClipCursor` is clamped to the client rect. Re-launch from the launcher after changing scale or render resolution.

**Launcher check:** Re-launch from the launcher after changing display mode, scale, or render resolution. `GET_STATUS` / debug harness can assert window and back-buffer dimensions match the launcher preset.

---

## Mod logs & IPC


### Cross-environment diagnostics (other PCs / no harness)

When a user reports a bug without the AI harness, enable durable logs — see [diagnostics-logging.md](diagnostics-logging.md).

| Enable | Action |
|--------|--------|
| `settings.json` | `"diagnostics": { "enabled": true }` then launch via Module Launcher |
| One-shot | Set `MMOD_DIAGNOSTICS=1` before starting Module Launcher |
| After repro | `.\tools\collect-diagnostics.ps1 -GameRoot "<game root>"` → zip under `%TEMP%\mirroredge-debug\collected\` |

Artifacts: `<gameRoot>/logs/<sessionId>/session.log`, `session.ndjson`, `environment.json`, plus `logs/last-session.json`.

### No `[mod]` lines in launcher

**Causes:**

- `LogServer` not started (launcher bug — should start in `main.cpp`)
- Mod not injected
- Pipe connect failed — mod logs silently if pipe unavailable (`mod_log.cpp`)

**Fix:**

- Inject must succeed first
- Pipe name must match: `\\.\pipe\mirroredge_module_log` (`module_contract.h`)
- Keep launcher window open (log relay stops when launcher exits)

### Control pipe (`mirroredge_module_control`)

**Status:** Implemented in **core** (`runtime/core/mod_ipc.cpp`). **module_manager** has a separate pipe: `mirroredge_module_manager_control` (`runtime/module_manager/mod_ipc.cpp`).

Commands (for harness / MCP / external tools):

**core** (`MMOD_CONTROL_PIPE_NAME`):

- `PING`, `GET_STATUS` (JSON with nested `engine`), `GET_UI_TARGETS`, `SET <ns>.<key> <value>`, `RELOAD_SETTINGS`, `ENSURE_GAMEPLAY_HOOKS`, `ENSURE_MP_HOOKS`, `CONSOLE <cmd>`

**module_manager** (`MMOD_MANAGER_CONTROL_PIPE_NAME`):

- `PING`, `GET_STATUS` (JSON), `LIST_MODULES`, `GET_LOG [n]`, `INJECT <id>`, `UNLOAD <id>`, `MENU_OPEN`, `MENU_CLOSE`, `MENU_TAB <name>`, `CONSOLE_OPEN`, `CONSOLE_CLOSE`, `CONSOLE_EXEC <line>`, `APPLY_WINDOW_LAYOUT`

Mod must call `ModIpc::Pump()` or `ModIpc::ServicePump()` on the init worker (hosted) or standalone init thread. Hosted mode also queues IPC work on the game main thread via `Engine::QueueMainThreadTask`. Pipe accepts one client at a time (`nMaxInstances=1`).

Harness: `.\tools\debug-harness\run.ps1 ui-launcher` (no game), `ci-gate` (L0+L1 pre-commit), `user-flow` (real SendInput + console inject), `run-all-scenarios.ps1` (full 19-scenario regression), or `auto-loop -RebuildOnFail` (retry + failure bundles). See [ai-debug-harness.md](ai-debug-harness.md).

### `inject-mp` / `core_ready` timeout or crash during bootstrap

**Symptoms:** Harness waits on `Local\core_ready`; NDJSON floods `borderless_sync … already_matched`; Event 1000 during core bootstrap; `game_ready_poll_done` with `initComplete=0`.

**Cause (2026-07):** `InitWorkerHosted` queued `CompleteModInitialization` only once (`initQueued`). If the first main-thread run returned early (`game_not_ready`), init never retried. Worker then blocked without `ModIpc::ServicePump`, so core pipe timed out. Per-frame `already_matched` NDJSON spam amplified log I/O.

**Fix:** `runtime/core/main.cpp` — retry queue every 200ms while game ready + `ServicePump` in the wait loop. `runtime/engine/borderless/viewport.cpp` — no per-frame trace on matched viewport.

**Verify:** `.\tools\debug-harness\run.ps1 inject-mp`

### `mp-playthrough-bots` fails at inGameplay / remote players

**Symptoms:** `inGameplay still false`, `currentMap` empty in `GET_STATUS`, or `Expected >= N remote players, got 0` after bots start.

**Cause:** Hosted split mode often has no UE world at main menu, so harness cannot confirm level entry via pipe; bots need the host in a real level on the server. See [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md) — do **not** retry listed failed approaches without new evidence.

**Check:** `connected_at_menu` in `*-interactions.ndjson`; local `multiplayer-server` on 5222; `%TEMP%\core.settings` room matches bots (`playthrough-lobby`).

### Two-machine remote spawn crashes host (`engine.dll` `0xc0000409`)

**Symptoms:** Server shows both clients in the same room/level (for example both `tutorial_p`), then one game exits. Windows Event Viewer reports `MirrorsEdge.exe` BEX, fault module `modules\engine\engine.dll`, exception `0xc0000409`, often at offset `0x000149d4`.

**Cause / fix:** Remote spawn crossed the C ABI through `SpawnCharacterWrapper` and queued a reference to a wrapper-local stack variable. The tick-time spawn write corrupted the stack cookie. Rebuild and deploy `engine.dll` plus feature mods with the stable out-pointer spawn queue fix. See [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md).

**If the crash persists at offset `0x00014a34` immediately after the remote reaches `tutorial_p`:** Default remote `Faith` used transient material names in the legacy spawn table. Multiplayer remaps remote Faith visuals to Kate while the Faith package path is verified, skips missing materials, serializes UDP pose writes, and validates bone-copy buffers. In logs, the fixed build should print `client: spawn remap id=... character=0->1` before spawning a default-Faith remote.

**If `%TEMP%\mirroredge-engine-spawn.log` is missing after the crash:** the host died before `Engine::SpawnCharacter` ran. The TCP listener level-message path must only record the remote level and log `client: remote spawn deferred ...`; remote presentation hooks are installed by host `Set Gameplay`, and tick-time logic owns spawn retries.

### Set Gameplay "does nothing" / drain forever `queue=0 spawned=0`

**Symptoms:** User clicks **Set Gameplay**; game does not freeze, but no bots appear. Launcher may spam `[core] engine: MmodDrainSpawnQueue N queue=0 spawned=0`.

**Cause (verified 2026-07-18):** Activation usually **succeeds** (`activation set live` in `%TEMP%\mirroredge-multiplayer-client.log`). Empty drain means **no remote players queued** (`listSize=0`), not a stuck Set Gameplay path.

**Fix / check:**

1. Read `%TEMP%\mirroredge-multiplayer-client.log` (not only `[core]` launcher lines).
2. Confirm `activation set live` then `QueueSpawnEligible iterating listSize=...`.
3. If `listSize=0`: start `multiplayer-server` + bots in the same room (`playthrough-lobby`); Set Gameplay alone does not spawn bots.
4. Multiplayer tab **Diag** button dumps `hosted/connected/live/hooks/players`.

Full agent checklist: [mp-set-gameplay-runbook.md](mp-set-gameplay-runbook.md). Related: [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md).

### Hosted split (1号机) / Remote (2号机): crash on manual Set Gameplay during level transition

**Symptoms:** On 1号机 (hosted split) or 2号机 (remote client), clicking **Set Gameplay** in the Multiplayer tab causes an immediate crash with `0xc0000409` (stack-cookie fastfail) in `modules\engine\engine.dll`.

**Root cause:** `ApplyManualClientLevelOnMainThread` calls `EnsureClientRemotePlayerPresentation()`, which calls `EnsureClientRenderHook()`. The latter pushes `OnRender` into `renderScene.Callbacks` via `Engine::OnRenderScene()` from an engine-task queue context. Simultaneously, the D3D9 `EndScene` hook fires `DispatchRenderSceneCallbacks()` on the render thread, iterating the same unsynchronized `renderScene.Callbacks` vector. Concurrent `push_back` during iteration corrupts the vector, triggering a GS fastfail.

**Fix:** `EnsureClientRemotePlayerPresentation()` no longer calls `EnsureClientRenderHook()`. `OnRender` is already registered at startup by `InstallClientRuntimeHooks()`; the redundant call from an engine task during level transition is unsafe. See [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md) root cause #12.

### Two-machine room drops to `0 rooms, 0 clients`

**Symptoms:** After the second machine joins, the server logs both clients as `timed out`, deletes the room, and later prints `level message for unknown client id=...` from the same remote address. The decimal unknown id may be the old hex client id converted to decimal.

**Cause / fix:** This is a heartbeat/connection-retention issue, not remote spawn. The client TCP receive path must not hold the socket mutex while blocking in `recv`, and the server timeout must tolerate short level-load/network stalls. Rebuild and redeploy `multiplayer.dll`, restart the updated `multiplayer-server.exe`, and keep the timeout/write-deadline server hardening in place. See [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md).

---

## Gameplay / addons

### Addon enabled but no effect

**Cause:** Gameplay hooks install **lazily** on first `ModManager::SetEnabled` — not at mod init.

**Fix:** Enable addon in Mods tab (Insert/F10 menu); triggers `Engine::EnsureGameplayHooks()`.

### In-game stutter / low FPS with multiplayer loaded (v1.2.5+)

**Symptoms:** Tutorial/level hitches every ~0.2s after bots join; console (`` ` ``) fills with `MmodDrainSpawnQueue` / `spawn queue` / `control pipe` lines.

**Cause (verified 2026-07-19):** EndScene spawn drain and spawn-retry paths were writing console + temp files every tick while the queue stayed non-empty (waiting for PC cache). That stalls the render thread.

**Fix:** Rebuild/redeploy **engine** + **multiplayer** + **core** at **product ≥ 1.2.5**. Close the Module Manager console while playing. See [mp-set-gameplay-runbook.md](mp-set-gameplay-runbook.md) (perf / visual PASS notes). For severe ~0.4 FPS after bots appear, also need **≥ 1.2.6** (ActorTick path — next section).

**Do not:** Leave console open during long bot sessions; re-add per-frame `EngineCoreBridge::Log` / `spawn_queue_trace.txt` on the drain path.

### Severe hitch after bots spawn (~0.4 FPS / 2s tick gaps) (v1.2.6+)

**Symptoms:** Game playable until remotes spawn; then phase ring shows ~2s between `tick.original` entries.

**Cause (verified 2026-07-19):** After `SetHostedGameplayLive`, `ActorTickHook` ran multiplayer pose work for **every** world `AActor` (CopyCallbacks + SEH + mesh probes). `ProcessEventHook` also allocated callback copies when live even with an empty list. `bUpdateSkelWhenNotRendered=true` forced constant remote skeleton updates.

**Fix:** Rebuild/redeploy **engine** + **multiplayer** at **product ≥ 1.2.6**. Remote location/yaw apply once per network tick; ActorTick no longer used for MP poses; ProcessEvent/ActorTick/BonesTick use lock-free empty fast paths; remote meshes no longer force off-screen skeleton updates.

### Hitch / freeze when entering a level (Story / `tutorial_p`) (v1.2.6+)

**Symptoms:** After multiplayer inject, starting New Game / entering a map freezes or hitch-stalls. `%TEMP%\mirroredge-multiplayer-client.log` shows `client: pre level load tutorial_p` and **no** matching `post level load`.

**Cause (verified 2026-07-19):** Multiplayer installed gameplay hooks at plugin init (`installing gameplay hooks at plugin init`), so `LevelLoadHook` wrapped LoadMap. The hook held `spawns.Mutex` for the entire load (blocking EndScene drain), and `OnPreLevelLoad` called `Engine::Despawn` while the world was tearing down.

**Fix:** Rebuild/redeploy **engine** + **multiplayer** at **product ≥ 1.2.6**. Hooks install only from **Set Gameplay** / `FORCE_HOSTED_LIVE` (after you are in-level preferred). Recommended order: enter Story → inject MP → Set Gameplay. Do **not** install gameplay hooks before Story entry (KI-2026-005). Expect client.log: `gameplay hooks deferred until Set Gameplay`.

### Soft-freeze after bots disconnect (TransformBones + Despawn) (KI-2026-012)

**Symptoms:** Game looks frozen after harness/bots leave the room. Process still running; `IsHungAppWindow=true`; `decode-phase.ps1` last entry is `tick.idle`; `client.log` stops at disconnect `flush_client_tasks`. Esc does not help.

**Cause (verified 2026-07-20):** Mutating remotes after live `TransformBones` soft-freezes — `ShutDown()`, raw `bHidden`/Location, and even `TryWriteActorLocation` park. Safe disconnect: null `Actor` (nametags stop), chat "left the room", drop refs; orphan meshes until level unload.

**Fix:** Rebuild/redeploy **multiplayer**. Do not park/ShutDown after bones. Harness stops bots after PASS and fails if hung. See [KI-2026-012](known-issues/KI-2026-012-soft-freeze-after-bot-despawn.md).

**Related:** Soft collision (Multiplayer tab) only XY-separates remotes / nudges local Faith on the **live** pose path. It must never park/ShutDown remotes on leave. Near-distance **Interact** (E / keybind) is TCP chat-only and likewise must not write remote Actors. **World Clamp** (FastTrace/Trace) also only runs on the live pose path — never enable remote `PHYS_Falling` / `bCollideWorld`.

### Tag / interact not working

**Symptoms:** Start Tag does nothing; no `[Tag]` / `[Interact]` chat; Tag death never retags.

**Cause (2026-07-20):** Pre-B0 Go `multiplayer-server` ignored `startTagGameMode` / `tagged` / `canTag`. Client Tag tick used bare `GetPlayerPawn()` when cache cold → death path skipped. Interact needs rebuilt client + server.

**Fix:** Redeploy **multiplayer.dll** + **multiplayer-server.exe** (stop old server first). Manual: Tag/Minigames → Start Tag; stand within Interact Range → **E** / Wave Nearest.

**Technical (authoritative):** [mp-set-gameplay-runbook.md §9](mp-set-gameplay-runbook.md) — TCP message table, UDP 1.3 m retag, interact JSON, settings keys, KI-012 constraints.

**Do not:** write remote Actors on interact; dual UDP pull-reply for Tag; remote AnimTree/`SetMove` or `PHYS_Falling`/`bCollideWorld` (**not on roadmap** — KI-012). B3-lite MovementState UDP trailer is OK (mesh+TransformBones only).

### Remotes clip through walls / float in air

**Symptoms:** Bot/SoftProbe meshes go through geometry or hover above the floor; harness fails `world clamp floor` / `world clamp wall`.

**Cause:** Remotes are `ASkeletalMeshActorSpawnable` with hard `TryWriteActorLocation` — no UE physics. World Clamp uses host-relative geometric snap (floor if too high above Faith; wall if single-step XY &gt; 200). ProcessEvent `Trace` on the live path hung spawn drain — do not retry.

**Fix:** Redeploy **multiplayer.dll**. SoftProbe auto uses `-PhysicsFallDrop` / `-PhysicsWallSlam`. Opt-out: `-SkipPhysicsProbe`. See [mp-set-gameplay-runbook.md §10](mp-set-gameplay-runbook.md).

**Do not:** `SetPhysics(PHYS_Falling)` or `bCollideWorld=true` on remotes; `Actor::Trace`/`FastTrace` from pose Tick (KI-012 / spawn hang).

**Kate nametag-only / buried mesh (2026-07-21):** Floor clamp used `hostZ+2` whenever UDP Z exceeded `hostZ+worldClampUp(80)`. Host TX adds `TargetMeshTranslationZ` (~94) and Kate spawn uses `PrePivot.Z=94` — clamp buried the body in the rooftop while ImGui nametags still projected. Floor clamp now only snaps extreme FallDrop hover (`> hostZ+worldClampUp+94`) down to `hostZ+94`. Remote mesh spawn sets `bUpdateSkelWhenNotRendered=true` so stand-off Kate keeps LocalAtoms alive.

### rem=2 sp=0 forever / IsHung during poll after FORCE_HOSTED_LIVE (2026-07-20)

**Symptoms:** Tutorial is playable (Faith crouch tip on screen) but `GET_STATUS` host stays `(0,0,0)` for a while; bots join (`rem=2`) and never spawn (`sp=0`); `spawn_drain_trace.txt` shows `ENTER … pending=2` with no `SPAWN_OK`; `phaseLastAge` climbs; harness aborts on `IsHungAppWindow` or times out with `spawn ok=0`.

**Causes (verified 2026-07-20) — do NOT retry:**
1. **EndScene WorldInfo GObjects warm** (`TryWarmActiveWorldInfoIncremental` budgets 200–2500 from empty-queue or cold drain) → `IsHungAppWindow` / ~12s ENTER gaps.
2. **Removing Tick `GamePlayers[0]` seed** from `GetPlayerController(false)` → `world=0` / PC cache never fills (tutorial still renders). Keep seed; skip only when `inEndSceneSpawnDrain`.
3. **PC-only spawn gate** (`GetWorld || GetPC` then `SpawnCharacterSafe`) while `PC->WorldInfo` empty → SpawnCharacter re-enters GamePlayers/`GetWorld(true)` and hangs EndScene (~80s `phaseLastAge`).
4. **Peek PC cache clearing on failed `TryIsA`** after Tick GamePlayers → EndScene never sees PC.

**Fix:** Tick GamePlayers seed + EndScene peek-only; require warm `GetWorld(false)` before spawn; eager `PC->WorldInfo` seed when GamePlayers succeeds; no EndScene WorldInfo incremental warm.

**A1/A2 pre-bot host pose (verified 2026-07-21 mesh-continue EXIT=0 + BONUS ~50s):** **TdEngine GObjects warm runs on the game thread** (`WarmTdGameEngineOnGameThread` in TickHook, 800/500ms slices). **Do not** call `TryWarmTdGameEngineIncremental` from EndScene empty-queue idle warm — render-thread GObjects walk concurrent with game tick hung within ~10ms of `idle.cont` (rem=2 sp=0, KI-2026-013). EndScene only `RequestIdlePcSeed` when engine is already warm; `CommitIdleWarmPlayerSeed` (GamePlayers read) runs on the game thread. **Do not commit idle PC seed while `WorldInfo` is null** — log `IDLE_PC_SEED_SOFT`, keep PC cache, retry WorldInfo each Tick (`tick.warm.engine.wait_world`); committing with `world=null` left rem=2/sp=0 and tick.idle freeze (2026-07-21). Seed GamePlayers **once**; never re-enter GamePlayers every tick after soft seed. Harness waits ≤70s for host pose and **refuses to start bots** if pose never arrives. Spawn gate remains `World || PC`. Cache TdEngine only when `GamePlayers` Count is **1..8** (SEH at UEngine+0x2BC). Tick: skip plugin callbacks while engine warm but PC/world cache empty (`tick.await_pc_seed`); 2s quiet after full PC+world seed.

**Synthetic `gameplay` map name (2026-07-21):** `FORCE_HOSTED_LIVE` keeps `client.Level=gameplay` by design (LevelsCompatible with tutorial). **Do not** call `TryUpgradeGameplayLevelName` / StreamingLevels / `GetMapName` from Tick to “fix” the name — soft-froze `rem=2 sp=0` with no `spawn_drain_trace` ENTER. Prefer harness bots `-Level tutorial_p` on lit-visual accept; host may `TryAdoptRemoteGameplayLevel` from peers.

**Seed host pose timing (2026-07-21):** Engine writes `TryGetSeedHostPose` only after the 2s idle-PC quiet window. Multiplayer must prefer that seed whenever present once pawn is missing — gating on `seedAge < 2000` inverted the window (plugin ticks skipped during quiet) and left harness `pos=(0,0,0)` after `IDLE_PC_SEED`.

**Mesh3p first sample during PHYS_Falling (2026-07-20):** After pose resolves, a fall/parkour transition forced `TryGetMesh3pBoneBuffer` before any `lastGoodBones` baseline → Tick stuck at `tick.callbacks`, EndScene drain never runs (`rem=2 sp=0`, no `spawn_drain_trace` ENTER). Multiplayer now baselines bones only when Walking + low speed; parkour refresh only after that.

**Mesh visual V1–V3 (2026-07-21):** Defaults `client.boneSmoothAlpha=0.55`, `boneSmoothIdleAlpha=0.70` (idle remotes stronger EMA; walk uses main alpha). Host slow-walk Mesh3p sample ≥33ms; idle TX throttle ~66ms. First non-zero UDP bones: `HasBoneSmooth` reset + 4-frame boost (`remote bones first live`). Idle/walk remotes snap yaw to packet (pose EMA still smooths XY). Nametag stance stays default off.

**Mesh visual V4 (2026-07-21):** Cache last walk/idle bone pose (`LastGoodBonePose`, seeded from join `defaultBones`). When `MovementState>=2` but peer looks grounded (`|Vz|<80`, horiz speed low), render last-good bones instead of fall limbs (`remote bones grounded override`) — avoids SoftProbe `PhysicsFallDrop` / worldClamp floor crumple.

**Mesh visual V5 (2026-07-21):** Harness takes `live-mesh` screenshot **before** stopping bots (`final` is empty after KI-012 null-only despawn). SoftProbe uses `-PhysicsProbeDelayMs 10000` so FallDrop/WallSlam do not yank the near-camera SoftProbe during softColl/Tag.

**Mesh visual V6 (2026-07-21):** Before `live-mesh`, harness pitches camera down (`Look-TowardMeshStandOff`) so Kate stand-offs enter FOV — host often ends SoftProbe settle looking at the skyline. Prefer SoftProbe-delay-window shot (before FallDrop).

**Mesh visual V7 (2026-07-21):** Host UDP yaw now uses **Controller.Rotation** (look) instead of pawn Rotation (often stuck at 0). Follow stand-offs were along +X while the camera faced the city — empty walkway despite softColl/Tag near host. After rebuild, **deploy** `dist/modules/multiplayer/multiplayer.dll` to game `modules/multiplayer/` (harness does not auto-deploy). Verified SoftProbe Kate mesh + nametag in SoftProbe-delay `live-mesh` shot.

**Mesh visual V9 (2026-07-22):** World-clamp **corridor** prefers pawn **body** yaw (Controller look only if body≈0) so glancing at solar panels does not pull remotes into props. Default `worldClampMaxLateral` 50; harness Cam laterals ±30. UDP Follow yaw still uses look (V7). Deploy `multiplayer.dll` after rebuild.

### bot.ps1 spawns far from the player (v1.2.6+)

**Symptoms:** `client: remote pose applied ... loc=(474,698,300)` (demo orbit) while host is on tutorial rooftop (~-4800,-7900,5854).

**Cause:** Bare `bot.ps1` used to leave Follow off (orbit at ~500,500). UDP Follow could also lock onto another bot's demo pose. Em-dash in double-quoted strings breaks PS 5.1 parse (`Missing closing '}'`).

**Fix:** Use product **≥ 1.2.6** scripts: Follow defaults **on** (`-NoFollow` for demo); prefers highest-|pos| UDP peer; auto-reads `%TEMP%\mirroredge-bot-target.json`. Keep `bot.ps1` ASCII-only in double-quoted strings. Host must be Set Gameplay + in-level so UDP pose is non-zero.

### Multiplayer / client connection issues

**Check:**

- `%TEMP%\core.settings` — core defaults; multiplayer mod uses `%TEMP%\multiplayer.settings` for `client.server`, `client.room`, `client.name`
- Internet reachability to the configured server (default `176.58.101.83:5222`)
- Connection progress should appear in launcher/session logs as `client: initialized`, `client: listener started`, `client: connecting`, `client: joined`, and `client: connected`; structured NDJSON uses `component:"multiplayer"` with `H-CONN`.
- Level sync should show manual `Set Gameplay` / `Set Menu` updates from the Multiplayer tab followed by `client: notify level ... sent=true`; remote level messages that match the local level should log `client: remote spawn deferred ...`, then tick-time spawn logs should follow. The Go server prints initial, changed, unchanged, malformed, and unknown-client level messages. Do **not** force gameplay hook installation or active world/pawn probing from multiplayer for level logs; see [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md).
- After hosted live, synthetic `gameplay` should upgrade to the real map (`client: upgraded gameplay level gameplay -> tutorial_p`). If `GET_STATUS` stays on `gameplay` forever, redeploy multiplayer + server (StreamingLevels/`GetMapName` probe + `levelsMatch` wildcard).

---

## Harness / test-logs git merge

### `test-logs/index.json` conflict after `git pull`

**Symptoms:** Merge conflict markers in `test-logs/index.json` after two test machines pushed harness results; `Push-HarnessTestLog` reports push failed.

**Cause:** Each machine updates only its own `machines.<id>` entry, but both commit the same `index.json` file.

**Fix:**

1. **Automatic (preferred):** Re-run push via harness (`Push-HarnessTestLog`) — it fetches and merges `index.json` + `CHANGELOG.md` before commit; on push failure it retries with accept-both. Ensure `setup.ps1` was run once (git merge driver `test-logs-index`).
2. **Manual:** Merge per [`test-logs/README.md`](../test-logs/README.md) — keep **both** machine keys under `machines`; `updatedAt` = latest `finishedAt`.
3. **Script:** `.\tools\debug-harness\merge-test-logs-index.ps1 -InputPath .ours.json, .theirs.json -OutputPath test-logs\index.json`

`test-logs/CHANGELOG.md` conflicts: keep both blocks newest-first, or let `Merge-HarnessTestLogChangelog` / merge driver handle it.

**Regression:** `verify-harness` runs `Test-HarnessTestLogMerge` (no game required).

---

## Quick diagnostic flow

```
1. ModuleLauncher.exe or .bat (no admin needed)?
2. Launcher log: 已部署 d3d9 代理?
3. modules\module_manager\module_manager.dll present?
4. dumpbin: MmOnD3D9DeviceCreated export undecorated?
5. Game started via launcher (proxy active)?
6. Launcher: module_manager 已由 d3d9 代理加载就绪?
7. [mod] [manager] bootstrap ready?
8. Insert/F10 → Module Manager visible?
9. Modules tab → inject **core** (loads **engine**)?
```

## When to update docs vs code

If you fix a recurring issue:

1. Add or update a row here.
2. Update the relevant architecture/injection/mod doc.
3. If the issue was tried before or has **failed approaches**, create or update a [known-issues](known-issues/README.md) entry per [known-issues-workflow.md](known-issues-workflow.md).

## Related docs

- [module-manager.md](module-manager.md) — proxy load success path
- [injection-and-ipc.md](injection-and-ipc.md) — timing and IPC details
- [d3d9proxy.md](d3d9proxy.md) — proxy-specific issues
- [architecture.md](architecture.md) — core / engine layout
- [mmultiplayer-mod.md](mmultiplayer-mod.md) — archived monolith init timeline
- [build-deploy.md](build-deploy.md) — deploy path issues
