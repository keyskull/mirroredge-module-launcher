# Injection Flow & IPC

## Launcher startup sequence

`launcher/main.cpp`:

1. `LogServer::Start()` — background named-pipe server
2. `StatusDialog::Create()` + message loop
3. Worker thread `LauncherWorker`:
   - `PrepareGameEnvironment()` — deploy `module_manager.dll` deps and `Binaries\d3d9.dll` proxy
   - `RunLauncherFlow()` — wait for game + d3d9 proxy to load `module_manager.dll`

No remote injection, no `SE_DEBUG`, no administrator rights required.

## Proxy load flow (default)

```
ModuleLauncher.exe
  → PrepareGameEnvironment() deploys Binaries\d3d9.dll
  → User starts MirrorsEdge.exe
  → d3d9 proxy: IDirect3D9::CreateDevice
       → LoadLibrary(module_manager.dll)
       → MmOnD3D9DeviceCreated(device)
  → Launcher: WaitForManagerViaProxy()
       → poll module_manager.dll via CreateToolhelp32Snapshot
       → or Local\module_manager_ready event
  → User: Insert/F10 → Module Manager → Modules → inject core.dll (loads engine.dll in-process)
```

`launcher/injection_flow.cpp` → `RunLauncherFlow`:

1. `WaitForManagerViaProxy()` — poll until `module_manager.dll` loaded or ready event signaled (180s timeout)
2. `WaitForReadyEvent(Local\module_manager_ready)` — up to 30s, or 10s fallback sleep
3. User injects **core** and feature mods (e.g. **multiplayer**) from in-game Module Manager → Modules tab

See [module-manager.md](module-manager.md) for verified workflow and export requirements.

## Module detection (no OpenProcess)

Launcher detects loaded modules with `CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)` — works for same-user processes without elevation.

Ready fallback: `OpenEventW` on `Local\module_manager_ready` (signaled by `module_manager` InitWorker).

## Auto mode (`/auto`)

`RunLauncherFlow()` runs once; exit code `0` when manager is ready.

## IPC: named pipe (mod → launcher logs)

| Constant | Value |
|----------|-------|
| `MMOD_LOG_PIPE_NAME` | `\\.\pipe\mirroredge_module_log` |

- **Server:** `launcher/log_server.cpp` — inbound pipe, line-delimited UTF-8
- **Client:** mod log writers — `WriteFile` with `\n`-terminated lines
- **UI:** prefixed with `[mod]` via `StatusDialog::AppendModLog`

## IPC: ready event (module_manager → launcher)

| Constant | Value |
|----------|-------|
| `MMOD_MANAGER_READY_EVENT_NAME` | `Local\module_manager_ready` |

- **Signaler:** `module_manager` InitWorker after bootstrap
- **Waiter:** `RunLauncherFlow()` in `injection_flow.cpp`

| Constant | Value |
|----------|-------|
| `MMOD_READY_EVENT_NAME` | `Local\core_ready` |

- **Signaler:** `Engine::MarkReady()` when core finishes init (in-game load path)

## IPC: d3d9 proxy callback

| Export | Caller | Handler |
|--------|--------|---------|
| `MmOnD3D9DeviceCreated` | `launcher/proxy/d3d9/d3d9.cpp` | `Presentation::OnProxyDeviceCreated` (module_manager) |
| `MmProxyRetryDeviceNotify` | `module_manager` InitWorker | Re-notifies cached D3D9 device from proxy |

Proxy load paths (`shared/module_contract.h`):

| Macro | Path (relative to `Binaries\`) |
|-------|--------------------------------|
| `MMOD_MANAGER_PROXY_LOAD_PATH` | `\..\modules\module_manager\module_manager.dll` |

**core** and feature mods load from Module Manager registry (`mod_registry.cpp`), not from the proxy.

## IPC: control pipe (mod server, launcher not wired)

| Constant | Value |
|----------|-------|
| `MMOD_CONTROL_PIPE_NAME` | `\\.\pipe\mirroredge_module_control` |

- **Server:** `runtime/core/mod_ipc.cpp` — duplex pipe, commands queued to main thread via `ModIpc::Pump()`
- **Launcher:** not implemented yet

## Launcher internal UI threading

`StatusDialog` uses `WM_APP + *` custom messages for thread-safe log append from worker and pipe threads.

## Configuration reference

### `launcher/config.h`

| Field | Default | Meaning |
|-------|---------|---------|
| `managerReadyEventName` | `Local\module_manager_ready` | Post-bootstrap ready signal |
| `injectWaitTimeoutMs` | 180000 | Max wait for game + proxy load |
| `injectReportIntervalMs` | 5000 | Status log interval |
| `readyEventWaitMs` | 30000 | Ready event wait |
| `readyFallbackSleepMs` | 10000 | Sleep when event not found |

Config is compile-time only — no runtime JSON for launcher.

### UAC / manifest

- `ModuleLauncher.vcxproj`: `UACExecutionLevel` = `asInvoker`
- `ModuleLauncher.bat`: starts exe directly (no `-Verb RunAs`)
