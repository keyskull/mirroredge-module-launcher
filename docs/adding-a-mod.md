# Adding a New Mod

Step-by-step guide for adding a Win32 DLL mod to this launcher. The launcher core stays generic — only config and contract headers change.

## Prerequisites

- Visual Studio 2022, Desktop C++ workload
- Win32 (x86) target — Mirror's Edge is 32-bit
- Understand [architecture.md](architecture.md) and [injection-and-ipc.md](injection-and-ipc.md)

## Checklist

### 1. Create mod project

```
runtime/<modname>/
  <modname>.vcxproj
  main.cpp              # DllMain entry
  ... your sources ...
```

Reference implementation: `runtime/core/` + `runtime/engine/`. Archived monolith: `legacy/mmultiplayer/`.

**vcxproj essentials:**

```xml
<PropertyGroup>
  <ConfigurationType>DynamicLibrary</ConfigurationType>
  <PlatformToolset>v143</PlatformToolset>
  <OutDir>$(SolutionDir)dist\modules\<modname>\</OutDir>
  <TargetName><modname></TargetName>   <!-- e.g. mymod → mymod.dll -->
</PropertyGroup>
<ClCompile>
  <AdditionalIncludeDirectories>$(SolutionDir)shared;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
</ClCompile>
```

Platform must be **Win32**, not x64.

### 2. Add to solution

Add the `.vcxproj` to `ModuleLauncher.sln`. `build.ps1` builds the full solution — no separate MSBuild step needed unless you add custom pre/post steps.

### 3. Define IPC contract

Either extend `shared/module_contract.h` or create `shared/<modname>_contract.h` included by launcher and mod:

```c
#define MYMOD_DLL_FILENAME L"mymod.dll"
#define MYMOD_READY_EVENT_NAME L"Local\\mymod_ready"
#define MYMOD_LOG_PIPE_NAME L"\\\\.\\pipe\\mirroredge_mymod_log"
#define MYMOD_DEPLOY_SUBDIR L"modules\\mymod"
```

Keep names unique across mods to avoid cross-talk.

### 4. Implement mod entry (`DllMain`)

Minimum pattern (from `core/main.cpp`):

```cpp
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH && GetModuleHandle(L"MirrorsEdge.exe")) {
        DisableThreadLibraryCalls(module);
        // Spawn init worker thread — never block DllMain
        CreateThread(nullptr, 0, InitWorker, nullptr, 0, nullptr);
    }
    return TRUE;
}
```

**Rules:**

- Only attach when host is `MirrorsEdge.exe`
- Defer heavy init to a worker thread
- Signal ready via named event when init completes
- Optional: connect to log pipe (`ModLog` pattern in core)

### 5. Signal ready event

Launcher waits on `readyEventName` after injection:

```cpp
HANDLE evt = CreateEventW(nullptr, TRUE, FALSE, MYMOD_READY_EVENT_NAME);
SetEvent(evt);
// Keep handle open or launcher may miss signal on race — mmultiplayer keeps event in Engine::MarkReady
```

If your mod has no ready signal, set `readyEventName` to empty in launcher config and rely on `readyFallbackSleepMs`.

### 6. Optional: log relay

Copy pattern from `core/mod_log.cpp`:

- Connect to `MMOD_LOG_PIPE_NAME` with `CreateFileW(..., OPEN_EXISTING)`
- Write UTF-8 lines terminated with `\n`
- Launcher `LogServer` relays to UI as `[mod]`

### 7. Optional: hosted plugin UI (recommended)

When loaded by **module_manager**, draw overlay UI through the host API — do **not** link ImGui or `#include "imgui/imgui.h"`.

1. Export `MMOD_PluginInitialize` / `MMOD_PluginShutdown` (see [module-manager.md](module-manager.md)).
2. In init: `PluginUi::Bind(host->ui)` — requires `ModHostApi` version **2**.
3. Include `shared/plugin_ui.h` only; use `ImGui::` calls as usual (`#define ImGui PluginUi`).
4. Register tabs via `host->AddTab` / `host->OnRenderScene`.

Reference: `mods/multiplayer/main.cpp`, `shared/feature_plugin_host.cpp`.

For feature plugins that depend on core, also link:

| Shared unit | Role |
|-------------|------|
| `shared/feature_plugin_bootstrap.h` | `ResolveMmultiplayerApi`, `AttachCore`, `AttachHost`, `DetachCore` |
| `shared/mod_plugin_settings.cpp` | JSON settings at `%TEMP%/<MMOD_MOD_ID>.settings` (needs `version.h` + `$(ProjectDir)` on include path) |
| `shared/feature_mod_log.cpp` | Log pipe + `FeaturePluginHost::ForwardLog` |
| `shared/menu_shim.h` | `Menu::` / `ModHost::` helpers |

Standalone inject without module_manager will not render plugin overlay UI.

### 8. Optional: d3d9 proxy export

If the mod receives D3D9 device from the proxy, export:

```cpp
extern "C" __declspec(dllexport) void __stdcall MmOnD3D9DeviceCreated(IDirect3DDevice9 *device);
```

**Important:** Add a `.def` file so the export name is undecorated:

```
LIBRARY mymod
EXPORTS
    MmOnD3D9DeviceCreated
```

Link with `<ModuleDefinitionFile>mymod.def</ModuleDefinitionFile>`. Without this, MSVC may export only `_MmOnD3D9DeviceCreated@4` and proxy `GetProcAddress` fails.

Reference: `runtime/module_manager/module_manager.def`.

Proxy discovers via `GetProcAddress` (with `_MmOnD3D9DeviceCreated@4` fallback). Update `MMOD_*_PROXY_LOAD_PATH` if mod DLL path differs.

### 9. Update launcher config

Edit `launcher/config.h`:

```cpp
std::wstring readyEventName = MYMOD_READY_EVENT_NAME;
std::wstring moduleLogPipeName = MYMOD_LOG_PIPE_NAME;

std::vector<std::wstring> moduleDllNames = {MYMOD_DLL_FILENAME};
std::vector<std::wstring> moduleSearchSubdirs = {
    MYMOD_DEPLOY_SUBDIR,
    L"modules",
    L"dist\\modules\\mymod",
    L"dist",
    L"."
};
```

Rebuild launcher after config changes (compile-time singleton).

### 10. Update deploy script (if needed)

Default `build.ps1` deploy copies each mod under `modules\<modname>\`. For a new mod, add a line to the `$moduleSpecs` array in `build.ps1`.

### 11. Verify

1. `.\build.ps1 -NoDeploy` — confirm `dist/modules/<modname>/<modname>.dll`
2. Deploy DLL to game `modules/<modname>/`
3. Run `ModuleLauncher.bat` as admin
4. Watch launcher log for inject success and `[mod]` lines
5. Confirm ready event or fallback sleep completes

## core / engine as reference

Archived monolith: `legacy/mmultiplayer/`.

| Feature | File(s) |
|---------|---------|
| Entry + init worker | `main.cpp` |
| Ready event | `engine.cpp` → `MarkReady()` |
| Log pipe client | `mod_log.cpp` |
| Control pipe server | `mod_ipc.cpp` (mod-side only; launcher not wired) |
| Proxy export | `exports.cpp`, `api_export.cpp` |
| Shared gameplay API | `MMOD_GetMmultiplayerApi` in `api_export.cpp` |
| Plugin metadata | `mod_info.cpp` → `MMOD_GetPluginInfo` |
| D3D9 / gameplay hooks | `engine.cpp`, `shared/hook.cpp` |
| ImGui menu | `menu.cpp` via `plugin_ui.h` + `ModHost::Attach` |
| Settings JSON | `core/settings.cpp` → `%TEMP%\core.settings` + `modules/core.config.json` |

You do not need addons, overlay UI, or UE3 SDK hooks for a minimal mod — only `DllMain`, optional ready event, and optional log pipe.

## Optional: Mirror's Edge UE3 SDK (`shared/me_sdk/`)

Use this when the mod needs to read game objects or call UnrealScript/native functions via `ProcessEvent`. The SDK is **Mirror's Edge 1.0–specific** and lives under `shared/me_sdk/` (not inside any single mod project).

| Piece | Path | Role |
|-------|------|------|
| Umbrella header | `shared/me_sdk/me_sdk.h` | All UE3 classes/structs for ME |
| Globals init | `shared/me_sdk/runtime/init.h`, `init.cpp` | `ProbeGlobals`, `AreGlobalsReady`, `InitializeGlobals`, `ValidateRuntime`; `SdkError` in `sdk_errors.h` |
| Safe access | `shared/me_sdk/runtime/safe_access.h`, `safe_gui.h`, `safe_gameplay.h` | Bounds-checked `TArray`, `UObject`/`ProcessEvent` SEH wrappers; GUI snapshots; tick helpers (`TryReadPawnPose`, …) |
| Game build gate | `shared/me_sdk/runtime/game_signature.h` | ME 1.0 `SizeOfImage` bounds + optional code-probe FNV |
| Global patterns | `shared/me_sdk/runtime/patterns_globals.h` | GNames/GObjects byte signatures (shared by init + readiness probe) |
| Gameplay hook patterns | `shared/me_sdk/patterns/hooks.h` | ProcessEvent, LevelLoad, tick hooks (engine) |
| TdGame patterns | `shared/me_sdk/patterns/tdgame.h` | Trainer / dolly feature-mod signatures |
| Math / map constants | `shared/me_sdk/util/math.h`, `util/constants.h` | FVector distance, rotator helpers, map/game-mode strings |
| Pattern scan | `shared/me_sdk/runtime/pattern.h`, `pattern.cpp` | Byte signature search in game modules |
| Trampoline hooks | `shared/hook.h`, `hook.cpp` | JMP/IAT hooks (engine, module_manager) |

**Include path:** add `$(SolutionDir)shared` to `AdditionalIncludeDirectories` (see checklist step 1).

**Minimal vcxproj compile units** (viewport sync / object lookup only):

```xml
<ClCompile Include="..\..\shared\me_sdk\runtime\init.cpp" />
<ClCompile Include="..\..\shared\me_sdk\runtime\safe_access.cpp" />
<ClCompile Include="..\..\shared\me_sdk\runtime\safe_gui.cpp" />
<ClCompile Include="..\..\shared\me_sdk\runtime\safe_gameplay.cpp" />
<ClCompile Include="..\..\shared\me_sdk\runtime\pattern.cpp" />
<ClCompile Include="..\..\shared\me_sdk\util\ME_Basic.cpp" />
<ClCompile Include="..\..\shared\me_sdk\generated\ME_Core_functions.cpp" />
```

Add more `ME_*_functions.cpp` files only for modules whose `ProcessEvent` wrappers you call (engine links the full set).

**Init at runtime** (each DLL has its own static `GObjects`/`GNames` pointers; all point at the same game memory after scan):

```cpp
#include "me_sdk/runtime/init.h"
#include "me_sdk/me_sdk.h"

if (!MeSdk::InitializeGlobals()) {
    // patterns outdated or game not ready
    return;
}

using namespace Classes;
auto *engine = UObject::FindObject<UTdGameEngine>("TdGameEngine Transient.TdGameEngine");
```

Reference: `runtime/engine/engine.cpp` (`Engine::InitializeSDK`), `runtime/module_manager/borderless_engine_sync.cpp` (`EnsureSdk`).

**Do not** depend on `runtime/core/` or `legacy/mmultiplayer/` from another mod — use `GetProcAddress(L"core.dll", "MMOD_GetMmultiplayerApi")` and link `shared/` only. See `mods/multiplayer/main.cpp`.

### Fixing SDK errors found during testing

The dump is ME 1.0–specific and can be wrong (offsets, param structs, patterns). **If testing proves the SDK is wrong, fix `shared/me_sdk/` — do not work around it in mod code.**

| Symptom | Likely fix location |
|---------|---------------------|
| `InitializeGlobals()` fails on main menu / in level | `runtime/patterns_globals.h` / `runtime/init.cpp` — GNames/GObjects signatures |
| Crash or no effect when reading/writing a UObject field | `ME_*_classes.hpp` — member offset / inheritance size |
| `ProcessEvent` returns garbage or silently wrong values | `ME_*_parameters.hpp` — param struct layout for that UFunction |
| Missing UFunction wrapper | Add or regenerate the matching `ME_*_functions.cpp` entry |

Verify against the ME 1.0 exe before patching. Rebuild all mods that link the changed SDK files. See [troubleshooting.md](troubleshooting.md) § UE3 SDK.

## What launcher does NOT need

- No mod-specific C++ in `launcher/` beyond `config.h`
- No changes to `inject.cpp` for standard `LoadLibraryW` injection
- No changes to injection timing unless your mod needs different gates (then edit `injection_flow.cpp` — affects all mods)

## Naming checklist

| Item | Convention |
|------|------------|
| DLL filename | `<modname>.dll` |
| Ready event | `Local\<modname>_ready` |
| Log pipe | `\\.\pipe\mirroredge_<modname>_log` |
| Deploy dir | `modules\<modname>\` |
| Contract macros | `<MODPREFIX>_DLL_FILENAME`, etc. |

## Related docs

- [architecture.md](architecture.md) — project layout
- [module-manager.md](module-manager.md) — hosted plugin model (split injection)
- [d3d9proxy.md](d3d9proxy.md) — optional early load via graphics proxy
- [build-deploy.md](build-deploy.md) — build and deploy pipeline
- [troubleshooting.md](troubleshooting.md) — common failures
