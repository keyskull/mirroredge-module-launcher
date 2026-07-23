# Architecture

## Overview

Three-layer design for injecting DLL mods into **Mirror's Edge (32-bit)**:

```
shared/module_contract.h     ← IPC names, DLL names, deploy paths (single source of truth)
launcher/                    ← ModuleLauncher.exe, d3d9 proxy, deploy, UI, log relay
  proxy/d3d9/                ← d3d9.dll → Binaries
runtime/                     ← all in-game DLL sources (→ dist/modules/)
  module_manager/            ← module_manager.dll (overlay host)
  console/                   ← mm-console.dll (developer console)
  core/                      ← core.dll
  engine/                    ← engine.dll
mods/                        ← external feature plugins
  multiplayer/ / trainer/ / dolly/
legacy/
  mmultiplayer/              ← archived monolith (not built)
shared/
  imgui/                     ← Dear ImGui sources (static-linked into module_manager)
  plugin_ui.h / plugin_ui_api.h  ← hosted plugin UI facade (no imgui in plugins)
  hook.h / hook.cpp          ← trampoline + IAT hooks (module_manager + engine)
  me_sdk/                    ← Mirror's Edge UE3 SDK (ME 1.0)
    me_sdk.h                 ← umbrella include
    runtime/                 ← init, pattern scan, GNames/GObjects signatures
    patterns/                ← gameplay / TdGame hook signatures
    util/                    ← ME_Basic, math, constants
    generated/               ← UE3 dump (ME_* classes, structs, ProcessEvent wrappers)
  mod_host_api.h             ← hosted plugin contract (LogMessage, tabs, render)
  module_contract.h          ← IPC names, deploy paths
```

**Solution:** `ModuleLauncher.sln` — all targets **Win32 / x86**.

## Mod-loading strategy (proxy-only)

| Step | Component | Action |
|------|-----------|--------|
| Deploy | Launcher | `PrepareGameEnvironment()` copies `Binaries\d3d9.dll` proxy |
| Host load | d3d9 proxy | `CreateDevice` → `LoadLibrary(module_manager.dll)` |
| Plugin load | module_manager | User selects mod in Modules tab → in-process `LoadLibrary` |
| Wait | Launcher | `RunLauncherFlow()` polls module + `Local\module_manager_ready` |

See [module-manager.md](module-manager.md) for the verified workflow.

## Launcher components

| File | Key symbols | Role |
|------|-------------|------|
| `main.cpp` | `wWinMain`, `LauncherWorker` | Log server, UI, worker thread; `/auto` CLI |
| `config.h` | `LauncherConfig::Get()` | Compile-time defaults |
| `paths.cpp` | `ResolveManagerDll`, `GetGameBinariesDirectory` | Path resolution (saved/auto-detect + launcher-relative fallback) |
| `game_path.cpp` | `GamePath::ResolveGameRoot`, `AutoDetectGameRoot` | Steam/EA/common-path detection; `<gameRoot>\settings.json` (`launcher.gameRoot`) |
| `launcher_settings.cpp` | `LauncherSettings::LoadDisplaySettings` | Reads/writes `settings.json` (`launcher.*`) via `shared/deploy_settings.cpp` |
| `game_config.cpp` | `GameConfig::ApplyDisplaySettings` | Sync `TdEngine.ini` + `module_manager.settings.ini` on launch. Borderless: window from `Scale` × monitor; render from preset or match-window (`RenderMatchWindow`) |
| `game_launch.cpp` | `PrepareGameEnvironment`, `LaunchGameExecutable` | Proxy deploy; apply display; `-nomoviestartup`; start game |
| `config_integrity_bypass.cpp` | `ConfigIntegrityBypass::BeginWatching`, `TryApplyToProcess` | In-memory bypass for `Default*.ini` integrity checks in `MirrorsEdge.exe` |
| `injection_flow.cpp` | `RunLauncherFlow`, `WaitForManagerViaProxy` | Wait for proxy-loaded manager |
| `process_util.cpp` | `FindProcessByName`, `HasLoadedModuleByPid` | Process/module introspection (no elevation) |
| `log_server.cpp` | `LogServer::Start` | Named-pipe server for mod logs |
| `ui/status_dialog.cpp` | `StatusDialog::*` | Win32 status window |

Launcher core is **mod-agnostic** — only `config.h` defaults reference `module_manager` / `core`.

## module_manager (`runtime/module_manager/`)

| File | Role |
|------|------|
| `main.cpp` | `DllMain` → `InitWorker`; calls `MmProxyRetryDeviceNotify` after bootstrap |
| `exports.cpp` | `MmOnD3D9DeviceCreated` (requires `module_manager.def` for undecorated export) |
| `presentation.cpp` | `Presentation::` API, overlay render loop |
| `presentation_imgui.cpp` | ImGui init, invalidate, draw |
| `presentation_input.cpp` | Focus, IME, BlockInput, message hooks |
| `presentation_device.cpp` | D3D9 device scan, vtable hooks, EndScene/Present |
| `presentation_bootstrap.cpp` | PeekMessage/GetMessage bootstrap hooks |
| `menu.cpp` | Module Manager UI (Insert/F10) |
| `mod_registry.cpp` | Discover/inject hosted plugins (security + SEH) |
| `mod_security.cpp` | Inject path validation |
| `mod_load_safe.cpp` | SEH wrappers for `PluginInitialize` / `PluginShutdown` |
| `plugin_ui_bridge.cpp` | Plugin UI facade → ImGui |
| `mod_log.cpp` / `console_host.cpp` + `runtime/console/` | In-memory ring buffer (1000 lines) + log pipe client; Source-style developer console overlay |
| `host_api.cpp` | Plugin host API (ModHostApi v4) |
| `version.h` / `version_export.cpp` | Runtime version (`MMOD_GetRuntimeVersion`) |

Full details: [module-manager.md](module-manager.md).

## Runtime component versions

Each in-game host DLL exports `MMOD_GetRuntimeVersion` (`shared/runtime_version.h`):

| DLL | `version.h` | Current |
|-----|-------------|---------|
| `module_manager` | `runtime/module_manager/version.h` | 1.1.0 |
| `core` | `runtime/core/version.h` | 1.1.0 |
| `engine` | `runtime/engine/version.h` | 1.1.0 |
| `mm-console` | `runtime/console/version.h` | 1.0.0 |

Shared helpers:

| Path | Role |
|------|------|
| `shared/runtime_module_client.*` | Unified sibling `LoadLibrary` + `GetProcAddress` for runtime DLLs |
| `shared/runtime_status_json.h` | `AppendJsonEngineStatus()` for `GET_STATUS` |
| `shared/runtime_version_query.*` | Query `MMOD_GetRuntimeVersion` from loaded runtime DLLs |

`engine.dll` exports `MMOD_EngineFormatStatusJson` and `MMOD_Borderless*`.

### Responsibility split (function vs UI)

| DLL | Service / logic | UI |
|-----|-----------------|-----|
| **module_manager** | `presentation.cpp` (D3D9 hooks), `mod_registry_load.cpp` (inject FSM), `mod_ipc.cpp`, `mod_security.cpp`, `host_api.cpp`, `console_host.cpp` (bridge only) | `menu_ui.cpp` (shell), `mod_registry_ui.cpp`, ImGui static in manager |
| **mm-console** | `console_cmd.cpp` command parse/exec via `ManagerConsoleBridge` | `console_ui.cpp` `MMOD_ConsoleRender*` ImGui overlay |
| **core** | `loader/`, `settings/`, `mod_ipc/`, `modhost/`, engine bridge | `menu.cpp` shell, `menu/engine_tab.cpp`, `menu/world_tab.cpp` |
| **engine** | `engine.cpp` (SDK API), `engine_hooks_d3d9.cpp`, `engine_hooks_bootstrap.cpp`, `engine_hooks_gameplay.cpp`, `borderless/` | — |

**Host API v4** (`shared/mod_host_api.h`): plugins register `OnPresentationTick` / `OnPresentationInputSync`; overlay graphics invalidate/create via host.

**Borderless:** Win32 + D3D reset in `runtime/engine/borderless/window.cpp`; UE3 viewport sync in `borderless/viewport.cpp`. Core registers the host via `EngineModuleClient::InstallBorderlessHost`. Display mode and resolution are configured in the launcher only (`launcher_settings.cpp`, `game_config.cpp`). Manager `GET_STATUS` reads window fields from `engine.dll` (`MMOD_BorderlessAppendStatus` via `shared/engine_module_client.cpp`).

## core + engine (`runtime/core/`, `runtime/engine/`)

Split replacement for monolithic `mmultiplayer.dll`:

| DLL | Path | Role |
|-----|------|------|
| **core** | `runtime/core/` | Hosted plugin entry (`MMOD_PluginInitialize`), menu (Engine/World), IPC, settings, auto-load config, forwards `MMOD_GetMmultiplayerApi` |
| **engine** | `runtime/engine/` | Gameplay/D3D9 hooks, `MMOD_GetMmultiplayerApi` implementation (uses `shared/me_sdk/`) |

`core` loads `modules/engine/engine.dll` on init and bridges host/settings callbacks via `shared/engine_core_bridge.h`.

| File | Role |
|------|------|
| `runtime/core/main.cpp` | Init worker, loads engine DLL |
| `runtime/core/menu.cpp` | Engine + World tabs |
| `runtime/core/mod_ipc.cpp` | Control pipe |
| `runtime/core/api_forward.cpp` | Re-exports `MMOD_GetMmultiplayerApi` to feature mods |
| `runtime/core/loader.cpp` | Auto-load modules from config |
| `runtime/engine/engine.cpp` | `Engine::` SDK helpers, init, mod-ready IPC |
| `runtime/engine/engine_hooks_d3d9.cpp` | D3D9 device scan, EndScene/Present hooks |
| `runtime/engine/engine_hooks_bootstrap.cpp` | PeekMessage/GetMessage, WndProc, input/focus |
| `runtime/engine/engine_hooks_gameplay.cpp` | ProcessEvent / Tick / LevelLoad gameplay hooks + install |
| `runtime/engine/engine_presentation_state.cpp` | D3D/window hook state (`EnginePresentationInternal`) |
| `runtime/engine/engine_state.cpp` | Gameplay hook state (`EngineInternal`) |
| `runtime/engine/api_export.cpp` | `MmultiplayerApi` vtable |
| `runtime/engine/borderless/window.cpp` | Borderless window layout tick (hosted presentation callbacks) |
| `runtime/engine/borderless/viewport.cpp` | UE3 viewport / mouse-look sync |
| `runtime/engine/borderless/export.cpp` | `MMOD_Borderless*` exports |
| `runtime/engine/debug_trace.cpp` | NDJSON agent debug (`component: engine`) |
| `runtime/engine/status_export.cpp` | `MMOD_EngineFormatStatusJson` |

## Feature mods (require core)

Loaded from Module Manager **Modules** tab after `core.dll` (which loads `engine.dll`). Resolve core API via `GetProcAddress(..., "MMOD_GetMmultiplayerApi")` on `core.dll`; use `shared/mp_engine_adapter.h` + `ui_harness_plugin.h`.

| Mod | Path | Role |
|-----|------|------|
| **multiplayer** | `mods/multiplayer/` | Multiplayer networking UI + Go `multiplayer-server.exe` |
| **trainer** | `mods/trainer/` | Trainer / fly / god mode |
| **dolly** | `mods/dolly/` | Camera dolly / markers |

Legacy in-tree copies under `legacy/mmultiplayer/addons/` are **not built**.

Feature plugins (`multiplayer`, `trainer`, `dolly`) link shared `mod_plugin_settings.cpp`, `feature_mod_log.cpp`, and `menu_shim.h`; settings path is derived from `MMOD_MOD_ID` in each mod's `version.h` (`%TEMP%/<id>.settings`).

## Shared plugin API

| File | Role |
|------|------|
| `mod_plugin_info.h` | `MMOD_GetPluginInfo`, semver, dependency manifest |
| `mmultiplayer_api.h` | C ABI exported by **core** (`MMOD_GetMmultiplayerApi` → engine) |
| `mp_engine_adapter.h` | Typed C++ facade over `MmultiplayerApi` |
| `feature_plugin_host.cpp` | ModHostApi tab/render forwarding for feature DLLs |
| `feature_plugin_bootstrap.h` | Resolve core API, bind shim, host attach helpers |
| `mod_plugin_settings.h/.cpp` | JSON settings → `%TEMP%/<MMOD_MOD_ID>.settings` |
| `feature_mod_log.cpp` / `mod_log.h` | Log pipe + host forward (feature plugins) |
| `menu_shim.h` | `Menu::` / `ModHost::` aliases over `FeaturePluginHost` |
| `json.h` | nlohmann JSON (single copy under `shared/`) |
| `plugin_ui.h` | Hosted ImGui facade (no imgui link in plugins) |

## d3d9proxy

See [d3d9proxy.md](d3d9proxy.md) for full proxy flow, enable/disable, and conflict handling.

`launcher/proxy/d3d9/d3d9.cpp` forwards to system `d3d9.dll`, hooks `IDirect3D9::CreateDevice`, loads `module_manager.dll` via `GetProcAddress(..., "MmOnD3D9DeviceCreated")`. Also exports `MmProxyRetryDeviceNotify` for late-loaded manager.

## Path resolution

Resolution order (`launcher/game_path.cpp` + `launcher/paths.cpp`):

1. **Saved override** — `<gameRoot>\settings.json` → `launcher.gameRoot` (set via launcher **浏览...**; migrates legacy `%TEMP%\mirroredge-launcher.settings`)
2. **Environment** — `ME_GAME_PATH` or `ME_DEPLOY_PATH` (game root)
3. **Auto-detect** — Steam `libraryfolders.vdf` / app manifest `17410`, EA/Origin common folders, Documents sibling `EA Games\...`
4. **Launcher-relative** — walk up to 6 parent directories from launcher exe for `Binaries\MirrorsEdge.exe`

Supports launcher in **game root** or any ancestor of the game binaries. UI shows the resolved game root; manual browse accepts game root or `Binaries` folder.

Module DLL deploy targets:

- `<gameRoot>\settings.json` — launcher UI prefs + `mods.autoLoad` (see schema below)
- `<gameRoot>\modules\module_manager\module_manager.dll`
- `<gameRoot>\modules\core\core.dll`
- `<gameRoot>\modules\engine\engine.dll`
- `<gameRoot>\modules\core.config.json` (legacy; `loader.autoModules` migrates to `settings.json`)
- `<gameRoot>\modules\multiplayer\multiplayer.dll` (+ `multiplayer-server.exe`)
- `<gameRoot>\modules\trainer\trainer.dll`
- `<gameRoot>\modules\dolly\dolly.dll`

Search order for manager DLL uses `managerSearchSubdirs` in `launcher/config.h`.

### `settings.json` (launcher + mod auto-load)

Lives at `<gameRoot>\settings.json` (or next to `ModuleLauncher.exe` before a game root is saved). Mirrors every launcher UI option:

| Field | Type | Launcher UI | Notes |
|-------|------|-------------|-------|
| `launcher.gameRoot` | string | 游戏路径 | Absolute path to game root (`Binaries\` parent) |
| `launcher.skipConfigIntegrityCheck` | bool | 跳过 Default*.ini 完整性检测 | Default `true`; `false` = do not patch integrity checks |
| `launcher.display.mode` | string | 显示模式 | `windowed` \| `fullscreen` \| `borderless` |
| `launcher.display.resX` / `resY` | int | 渲染分辨率 | Used when `renderMatchWindow` is false |
| `launcher.display.scale` | float | 窗口大小 % | Borderless window scale (0.25–1.0) |
| `launcher.display.renderMatchWindow` | bool | 分辨率预设「匹配窗口」 | Borderless only |
| `launcher.display.skipStartupMovies` | bool | 跳过片头 | Adds `-nomoviestartup` on launch |
| `mods.autoLoad` | string[] | — | Third-party mod IDs loaded after core bootstrap |

Environment overrides still apply: `MMOD_DISABLE_CONFIG_BYPASS=1` forces integrity bypass off; `MMOD_FORCE_CONFIG_BYPASS=1` forces it on.

## Naming conventions

| Prefix / pattern | Meaning |
|------------------|---------|
| `MMOD_*` | Shared contract macros in `module_contract.h` |
| `MmOnD3D9DeviceCreated` | `extern "C" __declspec(dllexport) __stdcall` proxy callback |
| `Local\` event prefix | Session-local Windows event |
| `\\.\pipe\` | Named pipe convention |

## Platform constraints

- Win32 (x86) only — use `TH32CS_SNAPMODULE32` for module enumeration
- No administrator rights required (`UACExecutionLevel=asInvoker`, no remote inject)
- DirectX SDK June 2010 required for engine / ImGui D3D9 path (d3dx9)

## Adding a new mod

See [adding-a-mod.md](adding-a-mod.md) for the full checklist.

Brief steps:

1. Create `runtime/<name>/` with `.vcxproj`, `OutDir = dist\modules\<name>\`
2. Add project to `ModuleLauncher.sln`
3. Register mod in `module_manager` mod registry (see [adding-a-mod.md](adding-a-mod.md))
4. Extend or fork `shared/module_contract.h` for IPC names
5. No launcher C++ changes required beyond config
