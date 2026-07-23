# mmultiplayer Mod Internals (archived)

> **Archived:** sources live under [`legacy/mmultiplayer/`](../legacy/mmultiplayer/). Built product is `core.dll` + `engine.dll` — see [architecture.md](architecture.md).

## Entry point (`legacy/mmultiplayer/main.cpp`)

### `DllMain` (DLL_PROCESS_ATTACH)

- Only runs if host is `MirrorsEdge.exe`
- Spawns `InitWorker` thread (once)

### `InitWorker` sequence

1. `ModLog::Initialize()` + log `"inject: mmultiplayer loaded"`
2. `Engine::SetDeferredInitCallback(CompleteModInitialization)`
3. `Sleep(8000)`
4. Poll until `d3d9.dll` loaded
5. `Engine::InstallPeekMessageBootstrap()` — hooks `PeekMessage`/`GetMessage`
6. `Engine::InstallRendererCapture()` — **no-op if d3d9 already present** (avoids render-thread crash)
7. Poll `Engine::IsGameReadyForModInit()` (GNames/GObjects patterns)
8. `Sleep(3000)`
9. `Engine::QueueMainThreadTask(CompleteModInitialization)`
10. Wait up to 90s for `initComplete`

### `CompleteModInitialization`

1. `Engine::BeginInitialization()`
2. `Settings::Load()` from `%TEMP%\mmultiplayer.settings`
3. `ModManager::Register` — `Client`, `Trainer`, `Dolly`
4. `Engine::InitializeSDK()` — calls `MeSdk::InitializeGlobals()` in `shared/me_sdk/runtime/init.cpp` (GNames/GObjects only; **not** full gameplay hooks yet)
5. `Menu::Initialize()` — ImGui render callback + default tabs
6. `ModManager::Initialize()` — enables saved addons from settings
7. `Engine::MarkReady()` — sets `modReady`, signals ready event, starts `ModIpc` control pipe

Gameplay hooks install **lazily** when user enables an addon: `ModManager::SetEnabled` → `Engine::EnsureGameplayHooks()`.

## Engine (`legacy/mmultiplayer/engine.cpp`)

### Renderer path (inject mode)

```
InstallPeekMessageBootstrap → PumpMessageBootstrap → TryLazyPresentationHook
```

Requirements for lazy hook: 12s since inject, game foreground, 45 stable present frames.

Hooks D3D9 device vtable:

- `EndScene` — overlay + deferred init pump
- `Present` — stability tracking

ImGui is owned by **module_manager** (`imgui_impl_dx9` + `imgui_impl_win32`). mmultiplayer draws via `plugin_ui.h` / `ModHostApi.ui` when loaded as a hosted plugin.

### Critical D3D9 safety rule

After `d3d9.dll` is loaded in the game process:

- **Do NOT** call `TrampolineHook` on `Direct3DCreate9` from the inject worker
- **Do NOT** call `FindExistingD3D9Device` from the inject worker
- Both crash the live render thread

When d3d9 is present, `InstallRendererCapture` returns immediately. Use `InstallPeekMessageBootstrap` first, then `TryLazyPresentationHook` on the main thread.

Proxy path: `Engine::OnProxyDeviceCreated` installs presentation hooks immediately (`rendererManagedByProxy = true`).

### Gameplay hooks (`InstallGameplayHooksInternal`)

Located via byte patterns in `shared/me_sdk/patterns/hooks.h` (`Pattern::FindPattern`):

- `LoadLibraryA` (re-hook after `menl_hooks.dll`)
- `ProcessEvent`, `LevelLoad`, `PreDeath`/`PostDeath`
- `ActorTick`, `BonesTick`, `ProjectionTick`, `Tick`

### Engine API (`engine.h`)

Cached accessors: `GetEngine`, `GetPlayerController`, `GetPlayerPawn`, `ExecuteCommand`, event registration callbacks.

## Menu (`legacy/mmultiplayer/menu.cpp`)

- Toggle: `VK_INSERT`, `VK_F10`, or `showKeybind` from settings
- `Menu::Show()` requires `Engine::ArePresentationHooksInstalled()`
- `Engine::BlockInput(true)` when menu open
- Built-in tabs: **Engine** (includes Debug / player HUD toggle), **World**
- Status hint when menu closed: `"mmultiplayer loaded"` (standalone only)

## Shared API (`shared/mmultiplayer_api.h`)

mmultiplayer core exports `MMOD_GetMmultiplayerApi()` for feature mods. Inject **core** before `multiplayer`, `trainer`, or `dolly`.

Each plugin exports `MMOD_GetPluginInfo()` — see `shared/mod_plugin_info.h`.

## Feature mods

| Mod | Path | Role |
|-----|------|------|
| multiplayer | `mods/multiplayer/` | Multiplayer + Tag (single Multiplayer tab) |
| trainer | `mods/trainer/` | Trainer tools |
| dolly | `mods/dolly/` | Camera dolly |

Deploy under `modules/<id>/<id>.dll`. Settings: `%TEMP%/<id>.settings`.

## Hook infrastructure (`shared/hook.h`)

- `TrampolineHook` / `TrampolineHookNoSuspend` — 5-byte JMP (both avoid process-wide `SuspendThread` by default; use `WithSuspendedThreads` when a patch must run with all other threads frozen)
- `ImportHook` — IAT patching
- `WithSuspendedThreads` — suspend all threads during patch (prefer sparingly; force-kill mid-suspend can wedge the game)
- `RELATIVE_ADDR` — RIP-relative address resolution

## d3d9 proxy detection

`Engine::IsModD3D9ProxyActive()` — proxy DLL is small (<512KB) vs system d3d9.

## Init timeline (inject mode)

```
T+0s    DllMain → InitWorker
T+8s    Wait for d3d9.dll
T+~     InstallPeekMessageBootstrap
T+~     Poll GNames/GObjects valid
T+~     QueueMainThreadTask(CompleteModInitialization)
T+~     SDK init, Menu, MarkReady → SetEvent(Local\mmultiplayer_ready)
T+12s+  TryLazyPresentationHook (foreground + stable frames)
T+~     EndSceneHook: ImGui overlay
T+user  ModManager::SetEnabled → EnsureGameplayHooks
```

## Settings

JSON file: `%TEMP%\mmultiplayer.settings` via `Settings::GetSetting` / `SetSetting`

Includes: menu keybind, enabled mods, client server/room/name, interpolation options, etc.

Default internet server: `176.58.101.83` (override via `client.server` in settings).

### Tag mode (Games tab)

When connected with other players in the same room, open **Games → Tag** to start tag mode on the server. Options include distance overlay, cooldown overlay, and configurable tag cooldown (1–60 seconds).
