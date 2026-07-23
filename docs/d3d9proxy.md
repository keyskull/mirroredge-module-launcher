# d3d9 Graphics Proxy

## Purpose

`launcher/proxy/d3d9` builds `dist/d3d9.dll`, a **DLL hijack proxy** placed in `Binaries\d3d9.dll`. The game loads it before the system Direct3D 9 library; at device creation the proxy loads **module_manager.dll** (default). No launcher remote injection.

See [module-manager.md](module-manager.md) for the full startup workflow.

## How it works

```
MirrorsEdge.exe
  → loads Binaries\d3d9.dll          (our proxy)
    → loads %SystemRoot%\System32\d3d9.dll   (real D3D9)
    → Direct3DCreate9 (exported)
      → HookCreateDevice on IDirect3D9 vtable[16]
        → ProxyCreateDevice
          → real CreateDevice
          → StoreLastDevice + NotifyDeviceCreated (async worker)
          → LoadModuleManagerMod()   (split mode default)
          → GetProcAddress("MmOnD3D9DeviceCreated")
          → Presentation::OnProxyDeviceCreated (module_manager)
          → [optional] MmProxyRetryDeviceNotify if manager loads later
```

### Key functions (`launcher/proxy/d3d9/d3d9.cpp`)

| Function | Role |
|----------|------|
| `GetRealD3D9()` | Lazy-load system `d3d9.dll`, resolve `Direct3DCreate9` |
| `Direct3DCreate9` | Exported entry; forwards to real DLL, calls `HookCreateDevice` |
| `HookCreateDevice` | Patches `IDirect3D9` vtable index 16 (`CreateDevice`) |
| `ProxyCreateDevice` | Calls real `CreateDevice`, then `StoreLastDevice` + `NotifyDeviceCreated` |
| `LoadModuleManagerMod()` | `LoadLibraryW` on `MMOD_MANAGER_PROXY_LOAD_PATH` (split mode) |
| `LoadMmultiplayerMod()` | Legacy direct monolith load (archived) |
| `NotifyDeviceCreated` | Async worker; `GetProcAddress` + call `MmOnD3D9DeviceCreated` |
| `ResolveDeviceNotify` | Tries `MmOnD3D9DeviceCreated` then `_MmOnD3D9DeviceCreated@4` |
| `TryNotifyCachedDevice` | Re-notify when manager loads after CreateDevice |
| `MmProxyRetryDeviceNotify` | Exported; called from module_manager InitWorker |

### Mod load paths

From `shared/module_contract.h`:

```c
#define MMOD_MANAGER_PROXY_LOAD_PATH L"\\..\\modules\\module_manager\\module_manager.dll"
#define MMOD_PROXY_LOAD_PATH         L"\\..\\modules\\core\\core.dll"
```

Proxy resolves: `GetModuleFileName(d3d9.dll or exe)` → strip filename → append path.

Deployed layout (split mode):

```
<gameRoot>/
  Binaries/
    MirrorsEdge.exe
    d3d9.dll              ← proxy
  modules/module_manager/
    module_manager.dll    ← loaded by proxy at CreateDevice
  modules/core/
    core.dll              ← injected in-game from Module Manager (not by proxy)
  modules/engine/
    engine.dll            ← loaded by core
```

### Mod exports

**module_manager** (`runtime/module_manager/exports.cpp` + **`module_manager.def`**):

| Export | Required | Notes |
|--------|----------|-------|
| `MmOnD3D9DeviceCreated` | **Yes** | Must be undecorated in export table — link with `.def` file |

Without `module_manager.def`, MSVC exports only `_MmOnD3D9DeviceCreated@4` and proxy `GetProcAddress` fails → overlay stuck on `waiting for d3d9 proxy device`.

**engine** (`runtime/engine/exports.cpp`) — used when core is proxy-loaded or legacy monolith mode:

| Export | Used by proxy | Handler |
|--------|---------------|---------|
| `MmOnD3D9DeviceCreated` | Optional (core hosted path uses module_manager) | `Engine::OnProxyDeviceCreated` |
| `MmOnDirect3D9Created` | No | `Engine::HookDirect3D9Interface` |

Archived monolith: `legacy/mmultiplayer/exports.cpp`.

### Proxy detection in mod

`Engine::IsModD3D9ProxyActive()` — checks `rendererManagedByProxy` or whether loaded `d3d9.dll` is the small proxy module (<512KB) vs system d3d9.

## Build output

| Setting | Value |
|---------|-------|
| Project | `launcher/proxy/d3d9/d3d9proxy.vcxproj` |
| Platform | Win32 |
| `OutDir` | `$(SolutionDir)dist\` |
| `TargetName` | `d3d9` → `dist/d3d9.dll` |
| Includes | `$(SolutionDir)shared` for `module_contract.h` |
| Link | `d3d9.def` — exports `Direct3DCreate9`, `MmProxyRetryDeviceNotify` |

## Enabling proxy mode

Three ways (pick one):

1. **Build/deploy:** `.\build.ps1 -DeployProxy` or `deployProxy: true` in `deploy.config.json`
2. **Launcher config:** `splitInjectionTest = true` (default) or `deployGraphicsProxy = true` — `PrepareGameEnvironment()` copies proxy at runtime
3. **Manual:** copy `dist/d3d9.dll` to `Binaries\d3d9.dll`

## Inject-only mode (legacy)

When proxy is **not** deployed, the launcher used remote injection (removed). Default is **proxy + module_manager** only.

## Launcher proxy resolution

`Paths::ResolveGraphicsProxyDll()` searches:

1. `<launcherDir>\dist\d3d9.dll`
2. `<launcherDir>\d3d9.dll`

## Launcher integration

| Aspect | Behavior |
|--------|----------|
| Host load | `LoadLibraryW(module_manager.dll)` from proxy on `CreateDevice` |
| Plugin load | In-process `LoadLibraryW` from Module Manager registry |
| Binaries `d3d9.dll` | Our proxy deployed by launcher / `build.ps1` |
| Admin required | No — manifest `asInvoker`, no remote inject |
| Conflicts | Only one `Binaries\d3d9.dll`; previous file backed up as `.mmproxy.bak` |

## Conflicts and backups

| File | Meaning |
|------|---------|
| `d3d9.dll.mmproxy.bak` | Previous `Binaries\d3d9.dll` backed up by launcher or build script |
| `d3d9.dll.dxvk.bak` | Legacy backup name from older deploy logic (may exist on user installs) |

To restore original graphics wrapper: stop game, delete proxy `Binaries\d3d9.dll`, rename `.mmproxy.bak` back to `d3d9.dll`.

## Related docs

- [module-manager.md](module-manager.md) — split injection, verified workflow, export fix
- [injection-and-ipc.md](injection-and-ipc.md) — inject timing when not using proxy
- [mmultiplayer-mod.md](mmultiplayer-mod.md) — renderer hook safety rules
- [troubleshooting.md](troubleshooting.md) — crash after inject/proxy issues
