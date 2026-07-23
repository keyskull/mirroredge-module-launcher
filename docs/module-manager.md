# module_manager & Proxy Load

Verified working **2026-07**. Host overlay + plugin registry; **core** auto-loads after proxy presentation hooks see a real render frame (not in Modules tab); third-party mods (`multiplayer`, `trainer`, `dolly`) are managed in-game and can auto-load via `settings.json`.

**Last updated:** 2026-07 — proxy-only launcher (no remote inject / no admin), proxy-mode startup hang fix, Alt+Tab stability, shared ImGui, in-game developer console.

## Overview

```
ModuleLauncher.exe
  → PrepareGameEnvironment() deploys Binaries\d3d9.dll (proxy)
  → Start game
  → d3d9 proxy: CreateDevice → Load module_manager.dll → MmOnD3D9DeviceCreated
  → Launcher waits for Local\module_manager_ready
  → module_manager delays presentation hooks until proxy device settles
  → first render frame after hooks → auto-load core.dll (+ engine.dll); optional mods from settings.json mods.autoLoad
  → User: Insert/F10 → Module Manager → Modules tab (third-party mods only)
```

| Component | Role |
|-----------|------|
| `runtime/module_manager/` | ImGui overlay host, mod registry, D3D9 presentation hooks |
| `runtime/console/` | Developer console UI (auto-loaded by module_manager) |
| `legacy/mmultiplayer/` | **Archived** monolith (not built; use core + engine) |
| `runtime/core/` | Hosted plugin shell (menu, IPC, settings) |
| `runtime/engine/` | Gameplay engine (hooks, SDK, API) |
| `launcher/proxy/d3d9/` | Loads `module_manager` at D3D9 device creation |
| `launcher/injection_flow.cpp` | `RunLauncherFlow()` → `WaitForManagerViaProxy()` |

## Launcher config (`launcher/config.h`)

| Field | Default | Meaning |
|-------|---------|---------|
| `managerDllNames` | `module_manager.dll` | Polled via toolhelp snapshot |
| `managerReadyEventName` | `Local\module_manager_ready` | Post-bootstrap ready signal |
| `injectWaitTimeoutMs` | `180000` | Max wait for proxy load |

## Verified startup workflow

1. Close game and launcher completely.
2. Run `ModuleLauncher.exe` or `ModuleLauncher.bat` (no administrator required).
3. Launcher logs `已部署 d3d9 代理: ...\Binaries\d3d9.dll`.
4. Start game via launcher button (do not start game manually before deploy).
5. Proxy loads `modules\module_manager\module_manager.dll` on `CreateDevice`.
6. Launcher logs `module_manager 已由 d3d9 代理加载就绪`.
7. In-game: **Insert** or **F10** → Module Manager menu.
8. **core** loads automatically after presentation hooks install and the first render frame is observed (`Local\core_ready`; avoids loading core while the game is still on a static splash).
9. **Modules** tab lists third-party mods only — inject/unload `multiplayer`, `trainer`, `dolly`, etc. Auto-load IDs via `settings.json` → `mods.autoLoad`.

## d3d9 proxy → module_manager chain

```
Direct3DCreate9 (proxy export)
  → HookCreateDevice (vtable[16])
  → ProxyCreateDevice
  → StoreLastDevice + NotifyDeviceCreated (async worker thread)
  → LoadModuleManagerMod() from Binaries\..\modules\module_manager\module_manager.dll
  → GetProcAddress(mod, "MmOnD3D9DeviceCreated")
  → Presentation::OnProxyDeviceCreated(device)
```

### Retry when CreateDevice precedes inject

- Proxy caches last device in `g_lastDevice`.
- Export `MmProxyRetryDeviceNotify` from proxy (`d3d9.def`).
- `module_manager` `InitWorker` calls it after startup if proxy is active.
- `LoadModuleManagerMod()` also calls `TryNotifyCachedDevice()` on repeat load.

## Critical fix: export name (2026-06)

**Symptom:** Log `notify_missing_export`, mod message `waiting for d3d9 proxy device`, overlay never ready.

**Cause:** MSVC exported only `_MmOnD3D9DeviceCreated@4` without `module_manager.def`. Proxy `GetProcAddress(mod, "MmOnD3D9DeviceCreated")` returned null.

**Fix:**

1. `runtime/module_manager/module_manager.def`:

   ```
   LIBRARY module_manager
   EXPORTS
       MmOnD3D9DeviceCreated
   ```

2. Link with `<ModuleDefinitionFile>module_manager.def</ModuleDefinitionFile>` in vcxproj.

3. Proxy fallback: also try `_MmOnD3D9DeviceCreated@4`.

Verify with `dumpbin /exports module_manager.dll` — must show:

```
MmOnD3D9DeviceCreated = _MmOnD3D9DeviceCreated@4
```

## Presentation hooks (`runtime/module_manager/presentation.cpp`)

### Proxy path (required in split mode)

- `OnProxyDeviceCreated` sets `g_cachedDevice` + `g_proxyDeviceReceived`.
- `TryLazyInstallHooks` uses cached device; **never** calls `FindDeviceSafe` when `IsGameProxyD3D9Active()`.
- Pattern scan + `GetCreationParameters` on false positives → **R6025** (SEH cannot catch pure virtual).

### Inject-only fallback (no proxy in Binaries)

- Falls back to `FindDeviceSafe` / exe pattern scan (crash-prone; avoid in production).

### Hook install gates

| Gate | Value |
|------|-------|
| Lazy delay | **12s after proxy device notification** (`kProxyHookSettleMs`) when d3d9 proxy delivered device; otherwise 12s (`kLazyHookDelayMs`) or **500ms** when borderless layout enabled |
| Focus | Game window focused or process foreground |
| Render path | **EndScene** (not Present-only); **6** stable frames before ImGui init; **20** before draw (`kStableFramesRequired`) |
| Retry interval | **100ms** with proxy device; 1s inject-only fallback |

**Borderless at startup:** Launcher writes `TdEngine.ini` + `%TEMP%\module_manager.settings.ini` before `CreateProcess`. d3d9 proxy forces windowed backbuffer size at `CreateDevice` and calls `WindowLayout_ApplyToWindow` on the game HWND immediately after device creation (no title bar, scaled + centered). `module_manager` then syncs D3D `Reset` on Present once hooks install.

### Message bootstrap

- In normal proxy mode, `module_manager` skips early `PeekMessage`/`GetMessage` bootstrap. The dedicated pump thread uses the proxy-delivered D3D device to attempt presentation hook install after the proxy settle delay.
- After presentation hooks are stable, opening the menu or console may install the message bootstrap for focus/input only. Do not install it early in proxy mode; see KI-2026-008 and KI-2026-010.
- `InstallBootstrap()` remains for inject-only fallback and hooks **both** `PeekMessage` and `GetMessage` (Mirror's Edge uses `GetMessage` for its main loop — PeekMessage-only bootstrap misses focus events).
- **Before hooks installed:** `PumpPreHookBootstrap()` runs from PeekMessage/GetMessage (throttled ~16 ms). It calls `TryInstallHooks()` + `PumpTasks()` only — **not** full `PumpFromMessageThread()`.
- **After hooks installed:** `PumpFromMessageThread()` runs **only from EndScene** on the render thread when the device is cooperative (`D3D_OK`). It must **not** run from PeekMessage/GetMessage (causes Alt+Tab freeze — render thread blocked while message pump holds focus work). Mod load FSM runs on the dedicated pump thread and is woken by `QueueLoadModule`; `LoadLibrary` / `PluginInitialize` never run from the pipe thread or render path.
- **Overlay draw gate:** `EndScene` calls `RenderOverlay` only when menu, console, or a loaded plugin requests overlay (`WantsActiveOverlayDraw`). ImGui still initializes for `overlayReady`; idle periods (e.g. intro cinematics) skip draw to reduce D3D risk.

### Control pipe (harness / launcher)

Read-only commands (`PING`, `GET_STATUS`, `GET_UI_TARGETS`, `LIST_MODULES`, `GET_LOG`, `APPLY_WINDOW_LAYOUT`) are answered on the **pipe server thread** so boot harness does not wait for the pump queue. Mutating commands (`INJECT`, `MENU_*`, `CONSOLE_*`) still run on the pump thread via `ModIpc::Pump()`.

### Load performance (2026-06)

| Stage | Before | After |
|-------|--------|-------|
| `InitWorker` d3d9 wait | Fixed 8s sleep | **16–32ms** when proxy already loaded d3d9; poll up to 8s only if needed |
| Proxy hook install | 12s lazy delay | **12s after proxy device notification** (`kProxyHookSettleMs`) to avoid splash/pre-frame hangs |
| Hook retry (proxy) | 1s | **100ms** |
| Stable frames before ImGui | 45 | **6** init / **20** draw |
| Pre-hook bootstrap throttle | ~50ms | **8ms** |
| Harness boot nudge | First at 20s, every 20s | **Immediate Enter**, then every **6s**; status poll **400ms** |
| Auto-load bootstrap | +3s delay after hooks | Immediate queue once hooks install; hook poll **8ms** |
| Load FSM | One phase per pump tick | Up to **12** phases per tick (burst); queue wakes pump asynchronously |
| Pending ops pump | Dedicated thread (16ms) | **8ms** wait timeout + wake event |
| `core` hosted init | 3s + 500ms sleeps | No pre-init sleep; game-ready poll **16ms**; init-complete wait **16ms** |
| Launcher process/manager wait | 500ms / 200ms polls | 200ms / 100ms |

### Alt+Tab / device loss (verified 2026-06)

Symptoms fixed: game freeze on Alt+Tab (especially with Module Manager menu open); overlay missing after removing message-thread pump.

| Issue | Root cause | Fix |
|-------|------------|-----|
| Overlay never hooks | `TryInstallHooks()` only in `PumpFromMessageThread`, which was gated on `g_hooksInstalled` in EndScene | `PumpPreHookBootstrap()` on message hooks while `!g_hooksInstalled` |
| Alt+Tab freeze (general) | Full `PumpFromMessageThread()` on PeekMessage/GetMessage after hooks | Move pump to EndScene only; message hooks do focus/input only |
| Alt+Tab freeze (device lost) | EndScene ran overlay / focus sync during `DEVICELOST` | Early `lost_bypass`: call original EndScene only; minimal `ProcessLostFrameSideEffects` |
| Alt+Tab OK without menu, freeze with menu | ImGui D3D objects not released on `DEVICELOST`; only invalidated on `DEVICENOTRESET` | See ImGui device lifecycle below |
| `blockInput` stuck | No `WM_ACTIVATEAPP` deactivate; menu stayed open away from game | `SyncInputBlockWithForeground`: hide menu + invalidate when `!IsOurProcessForeground()` |
| Game input eaten | Blacklist too broad on PeekMessage | `IsGameInputMessage()` whitelist; never null `WM_ACTIVATEAPP` / `WM_SIZE` |

**ImGui device lifecycle** (align with `runtime/engine/engine.cpp`):

1. **`Present` hook always calls `UpdateStability(device)` first** — even when device is lost (engine does the same). Do not skip stability on lost Present.
2. **First `D3DERR_DEVICELOST`:** `ImGui_ImplDX9_InvalidateDeviceObjects()` once per lost episode (`imgui_lost_invalidate`).
3. **`D3DERR_DEVICENOTRESET`:** invalidate every frame until reset (`imgui_pre_reset`) — must happen **before** the game's `IDirect3DDevice9::Reset()`.
4. **`D3D_OK` after NOTRESET:** `ImGui_ImplDX9_CreateDeviceObjects()` only (`imgui_device_reset`).
5. **Focus loss with menu open:** immediate `HostMenu::Hide()`, `ReleaseCapture()`, `InvalidateImGuiDeviceObjectsNow()` — do not defer only via `g_pendingMenuHide` while menu still holds D3D resources.
6. **`WM_ACTIVATEAPP` activate:** if cooperative level is `LOST` or `NOTRESET`, invalidate before game reset path runs.

**EndScene paths:**

```
DEVICELOST / DEVICENOTRESET → ProcessLostFrameSideEffects (menu hide, input reset) → original EndScene only
D3D_OK → PumpFromMessageThread → ProcessImGuiRenderThreadEvents → focus sync → UpdateStability → optional RenderOverlay
```

**Input when menu open:**

- `HostMenu_SetBlockInput(true)` only when menu open **and** foreground **and** `TestCooperativeLevel() == D3D_OK`.
- Mouse state is polled per ImGui frame. Keyboard state is also polled per ImGui frame for ImGui `InputText`, so console/menu text entry does not depend solely on Mirror's Edge producing `WM_CHAR`.
- Message hooks, when installed after overlay open, must not call `TranslateMessage` and must not swallow `WM_IME_*` / `WM_INPUT` (KI-2026-003).
- On Alt+Tab away: clear `blockInput`, hide menu, invalidate ImGui device objects on render or message thread as above.

### Debug log chain (Alt+Tab success)

Without menu: `lost_bypass` → `imgui_pre_reset` → `WM_ACTIVATEAPP` → `device_recovered` → `imgui_device_reset` → `status_hint`.

With menu open: additionally `imgui_unfocus_hide` or `imgui_lost_invalidate` before recovery; must not stop at `after_activate_focus` with `DEVICELOST` and no further EndScene logs.

## module_manager components

| File | Role |
|------|------|
| `main.cpp` | `DllMain` → `InitWorker` (conditional d3d9 wait, bootstrap, proxy retry, 16ms pump thread) |
| `exports.cpp` | `MmOnD3D9DeviceCreated` → `OnProxyDeviceCreated` |
| `presentation.cpp` | `Presentation::` API, overlay render loop |
| `presentation_imgui.cpp` | ImGui init / invalidate / draw |
| `presentation_input.cpp` | Focus, IME, BlockInput |
| `presentation_device.cpp` | D3D9 hooks, EndScene/Present |
| `presentation_bootstrap.cpp` | PeekMessage/GetMessage bootstrap |
| `menu.cpp` | Insert/F10 toggle; thread-safe tab snapshot + SEH around tab callbacks |
| `mod_registry.cpp` | Public `ModRegistry::` API (load/unload requests, snapshots) |
| `mod_registry_discover.cpp` | Scan `modules\`, plugin-info probe, dependency checks |
| `mod_registry_load.cpp` | Load FSM (`AdvanceLoadPhase`), unload, queue |
| `mod_registry_status.cpp` | `FormatStatusList` / `FormatStatusJson` |
| `mod_registry_ui.cpp` | Modules tab ImGui |
| `mod_security.cpp` | Path sandbox, PE x86 check, reserved folder/DLL blocklist |
| `mod_load_safe.cpp` | SEH wrappers for `PluginInitialize` / `PluginShutdown` |
| `plugin_ui_bridge.cpp` | `PluginUiApi` → real ImGui forwarding |
| `host_api.cpp` | Hosted plugin API (`ModHostApi` v4) |
| `console_host.cpp` | Loads `mm-console.dll`, bridges `ManagerConsoleBridge` |
| `mod_log.cpp` | In-memory ring buffer (1000 lines) + log pipe client |

## ImGui (static in `module_manager`)

ImGui sources under `shared/imgui/` compile **into** `module_manager.dll`. Plugins never link or include ImGui directly.

| Item | Path / detail |
|------|----------------|
| Sources | `shared/imgui/` (static in module_manager) |
| Bridge | `runtime/module_manager/plugin_ui_bridge.cpp` |
| Plugin facade | `shared/plugin_ui.h` + `shared/plugin_ui_api.h` |
| Host API | `ModHostApi.ui` (`const PluginUiApi *`) — version **2**; `GetImGuiContext` removed |
| Plugin init | `PluginUi::Bind(host->ui)` in `MMOD_PluginInitialize` |

Plugins include `plugin_ui.h` only (`#define ImGui PluginUi`). Standalone core inject without module_manager does **not** render overlay UI.

## Module inject security (`mod_security.cpp`)

Before `LoadLibrary`, `mod_registry` validates:

| Check | Rejects |
|-------|---------|
| Path sandbox | DLL outside `modules\<id>\` or `..` traversal |
| Module ID | Non `[A-Za-z0-9_-]` folder names |
| Reserved folders | `imgui`, `module_manager`, `engine`, `mm-console` |
| Reserved DLLs | `imgui.dll`, `module_manager.dll`, `d3d9.dll` |
| PE machine | Non x86 (IMAGE_FILE_MACHINE_I386) |
| Plugin export | Missing `MMOD_PluginInitialize` |

`LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)` limits dependency search to the module directory.

## Inject fault tolerance (`mod_load_safe.cpp`, `mod_registry.cpp`)

| Mechanism | Behavior |
|-----------|----------|
| SEH on init | Crash in `PluginInitialize` → log exception code, `FreeLibrary`, status error (game survives) |
| SEH on shutdown | Same for `PluginShutdown`; always `FreeLibrary` after |
| Async inject | `INJECT` / UI queue the load; dedicated pump thread performs `LoadLibrary` + init |
| Callback guards | Gameplay tick/render/input/level and host presentation callbacks run through SEH; crashed callbacks are logged and removed |
| Plugin thread guards | Multiplayer listener/status/UDP threads run through SEH; crashes disable network work and keep the game alive |
| Mutex | Registry + pending ops protected by `std::mutex` |
| Handle map | `HMODULE` → module id registered **before** `PluginInitialize` (plugin logs tagged correctly) |
| Refresh | **Modules → Refresh** preserves loaded module state |

## In-game Logs tab

**Insert/F10 → Module Manager → Logs**

| Feature | Detail |
|---------|--------|
| Buffer | Last 1000 lines in memory (`mod_log.cpp`) |
| Format | `[<sec>s][<source>] message` — source = `manager` or module id (e.g. `core`, `multiplayer`) |
| Plugin logs | `ModHostApi::LogMessage(HMODULE, const char*)` — core `ModLog::Write` forwards via `ModHost::ForwardLog` when hosted |
| Launcher pipe | Still writes to `MMOD_LOG_PIPE_NAME` when launcher log server is running |
| UI | Source filter (All/Manager/core/…), text filter, auto-scroll, Clear, red error lines |

## Host API (`shared/mod_host_api.h`)

```c
struct ModHostApi {
    unsigned version;                    // MMOD_HOST_API_VERSION = 4
    // ... AddTab, OnRenderScene, QueueMainThreadTask ...
    const PluginUiApi *ui;               // plugin UI facade (bind via PluginUi::Bind)
    MMOD_LogMessageFn LogMessage;        // optional; plugins use if non-null
    HMODULE hostModule;
};
```

Plugins must export `MMOD_PluginInitialize` / `MMOD_PluginShutdown`. Core calls `ModHost::SetSelfModule(self)` during init so log forwarding resolves the correct module id.

## IPC (split mode)

| Constant | Value |
|----------|-------|
| `MMOD_MANAGER_READY_EVENT_NAME` | `Local\module_manager_ready` |
| `MMOD_MANAGER_PROXY_LOAD_PATH` | `\..\modules\module_manager\module_manager.dll` |
| `MMOD_LOG_PIPE_NAME` | Shared with core / feature mods (`mirroredge_module_log`) |

## Deploy layout

```
<gameRoot>/
  Binaries/
    MirrorsEdge.exe
    d3d9.dll                    ← proxy (split mode)
  modules/
    module_manager/
      module_manager.dll
    core/
      core.dll
    engine/
      engine.dll
    multiplayer/
      multiplayer.dll
    trainer/
      trainer.dll
    dolly/
      dolly.dll
  ModuleLauncher.exe
  ModuleLauncher.bat
```

Build and deploy: `.\build.ps1` (proxy deployed to `Binaries\` by default when deploy runs).

## Verified success log chain

Debug session log (`debug-0f3242.log` instrumentation):

```
direct3d_create9
→ create_device_ok
→ load_manager
→ notify_call
→ proxy_device (OnProxyDeviceCreated)
→ lazy_gate
→ proxy_device (TryLazyInstallHooks)
→ hooks_installed
→ EndScene hook_enter
→ EnsureImGui before_dx9_init
```

## Do not regress

- Do not call `FindDeviceSafe` when Binaries `d3d9.dll` is the proxy.
- Do not hook `Direct3DCreate9` or scan device memory from inject worker thread.
- Do not remove `module_manager.def` — breaks proxy device notify.
- Render overlay in **EndScene**; Present-only path does not draw on this game build.
- Do not load plugins without `MMOD_PluginInitialize` or outside `modules\<id>\` sandbox.
- **Do not** call full `PumpFromMessageThread()` from PeekMessage/GetMessage after `g_hooksInstalled` — Alt+Tab freeze.
- **Do not** skip `UpdateStability()` in Present when device is lost — breaks ImGui invalidate before `Reset()`.
- **Do not** run overlay / `NotifyFocusTransition` / ImGui batch processing on EndScene `DEVICELOST` path.
- **Do not** defer menu hide on focus loss when menu is open — invalidate ImGui D3D objects immediately.

## Related docs

- [d3d9proxy.md](d3d9proxy.md) — proxy implementation details
- [injection-and-ipc.md](injection-and-ipc.md) — `RunLauncherFlow`
- [troubleshooting.md](troubleshooting.md) — overlay / export failures
- [adding-a-mod.md](adding-a-mod.md) — hosted plugin checklist
