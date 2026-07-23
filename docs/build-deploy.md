# Build & Deploy

## Quick commands

| Command | Behavior |
|---------|----------|
| `.\build.ps1` | Build + deploy (default) |
| `.\deploy.ps1` | Same as `build.ps1` |
| `.\build.ps1 -NoDeploy` | Build only |
| `.\build.ps1 -DeployProxy` | Also copy `d3d9.dll` into `Binaries\` |
| `.\build.ps1 -SkipServer` | Skip Go server build |
| `.\build.ps1 -Debug` | Debug configuration |

## Requirements

- Windows 10+
- Visual Studio 2022 — Desktop development with C++
- DirectX SDK (June 2010) — engine uses d3dx9; **or** run `.\scripts\setup-dxsdk.ps1` for a repo-local copy under `third_party/dxsdk/` (no admin). `Directory.Build.props` picks vendor copy first, then legacy Program Files install. CI `release.yml` runs `setup-dxsdk.ps1` before build. `build.ps1` also auto-runs it if `d3dx9.h`/`d3dx9.lib` are missing.
- Go 1.21+ — optional, builds `multiplayer-server.exe`
- Administrator at runtime **not** required

## Versioning

| File | Role |
|------|------|
| [`version.json`](../version.json) | **Source of truth** — product semver + release date |
| [`CHANGELOG.md`](../CHANGELOG.md) | Human-readable release notes ([Keep a Changelog](https://keepachangelog.com/)) |
| `shared/product_version.h` | Generated at build from `version.json` (`tools/Sync-ProductVersion.ps1`) |
| `runtime/*/version.h`, `mods/*/version.h` | Per-DLL versions exported via `MMOD_GetRuntimeVersion` |

Bump flow:

1. Edit `version.json` and add a section to `CHANGELOG.md`.
2. Run `.\build.ps1` (syncs `product_version.h`, copies `VERSION.json` + `CHANGELOG.md` to `dist/` and deploy root).
3. Bump individual `version.h` files when a component changes independently.

Launcher window title and in-game overlay show **product** version (`MMOD_PRODUCT_VERSION` env, set when launching the game).

### Release / auto-update

ModuleLauncher checks GitHub **Latest Release** on startup and via **检查更新**:

- API: `https://api.github.com/repos/keyskull/mirroredge-module-launcher/releases/latest`
- Required asset: `mirroredge-module-launcher-<semver>-win32.zip`
- Tag: `v<semver>` (example `v1.2.7`)
- Zip root matches deploy root (`ModuleLauncher.exe`, `modules\`, `d3d9.dll`, …)
- User clicks **立即升级** → download → extract → exit → bat replaces files → relaunch
- Settings: `launcher.skipUpdateCheck`, `launcher.dismissedUpdateVersion`, `launcher.uiLanguage` (`zh`/`en`) in `settings.json`
- Launcher UI tabs: Launch (path/options + d3d9/module_manager deploy status), Display (mode/resolution/scale + config sync status / Apply config), Update (check/upgrade); status, language, log, and action buttons stay always visible
- Trust model: GitHub Release channel only (no code signing in v1)

Pack locally after build:

```powershell
.\build.ps1 -NoDeploy
.\tools\pack-release.ps1
```

Publish: push tag `v*` (workflow [`.github/workflows/release.yml`](../.github/workflows/release.yml) builds, packs, uploads the zip).

## `build.ps1` pipeline

1. **Resolve deploy target** (`Resolve-DeploySettings`):
   - `deploy.config.json` → `deployPath` or `gameBinaries`
   - Env: `ME_DEPLOY_PATH` / `ME_GAME_BINARIES`
   - Default: parent of repo (`..\`)
2. **Optional Go build** — `mods/multiplayer/server` → `dist/modules/multiplayer/multiplayer-server.exe`
3. **Sync product version** — `tools/Sync-ProductVersion.ps1` → `shared/product_version.h`
4. **Clean `dist/modules/`** — remove stale legacy plugin folders before MSBuild
5. **MSBuild** — `ModuleLauncher.sln` / Platform=x86 / Release|Debug
5. **Verify outputs:**
   - `dist/ModuleLauncher.exe`
   - `dist/modules/module_manager/module_manager.dll`
   - `dist/modules/core/core.dll`
   - `dist/modules/engine/engine.dll`
   - `dist/d3d9.dll`
6. **Copy metadata:** `dist/VERSION.json`, `dist/CHANGELOG.md`, `dist/settings.json`
7. **Alias:** `dist/mirroredge-module-launcher.exe`
8. **Deploy** (`Invoke-Deploy`) unless `-NoDeploy` — wipes `<deployPath>/modules/` and `<deployPath>/dist/modules/` first, then copies current build outputs only (no leftover `mp-*` / legacy folders). If `ModuleLauncher.exe` is still locked, deployment warns and continues copying module DLLs.

## Deploy layout

```
<deployPath>/
  ModuleLauncher.exe
  mirroredge-module-launcher.exe
  ModuleLauncher.bat              # Starts ModuleLauncher.exe (no elevation)
  modules/module_manager/module_manager.dll
  modules/core/core.dll
  modules/engine/engine.dll
  dist/modules/module_manager/module_manager.dll   # mirror copy
  dist/modules/core/core.dll                 # mirror copy
  dist/modules/engine/engine.dll             # mirror copy
  dist/d3d9.dll
  multiplayer-server.exe       # if Go built (under modules/multiplayer or game root)
```

### Proxy deploy

`-DeployProxy` or `deployProxy: true` copies `d3d9.dll` into resolved `Binaries\`.

**Proxy deploy:** `PrepareGameEnvironment()` always copies proxy at launcher runtime; `build.ps1` also deploys to `Binaries\` when `-DeployProxy` or `deploy.config.json` `deployProxy` is set.

### Inject-only deploy

- Renames existing `Binaries\d3d9.dll` → `d3d9.dll.mmproxy.bak`
- Removes stale `Client.dll`, launcher exes from `Binaries\`

## `deploy.config.json`

Copy from `deploy.config.json.example`:

```json
{
  "deployPath": "F:\\EA Games\\Mirrors Edge",
  "deployProxy": false
}
```

## Build outputs

| File | Description |
|------|-------------|
| `dist/ModuleLauncher.exe` | Primary launcher |
| `dist/mirroredge-module-launcher.exe` | Alias copy |
| `dist/modules/module_manager/module_manager.dll` | Overlay host (split mode) |
| `dist/modules/core/core.dll` | Core plugin shell |
| `dist/modules/engine/engine.dll` | Gameplay engine |
| `dist/modules/multiplayer/multiplayer-server.exe` | Multiplayer Go server (optional; `-SkipServer` default) |
| `dist/d3d9.dll` | Graphics proxy |

## Solution structure

`ModuleLauncher.sln` projects:

- `ModuleLauncher` → `dist/ModuleLauncher.exe`
- `launcher/proxy/d3d9` → `dist/d3d9.dll`
- `runtime/module_manager` → `dist/modules/module_manager/module_manager.dll`
- `runtime/core` → `dist/modules/core/core.dll`
- `runtime/engine` → `dist/modules/engine/engine.dll`
- `mods/multiplayer` → `dist/modules/multiplayer/multiplayer.dll`
- `mods/trainer` → `dist/modules/trainer/trainer.dll`
- `mods/dolly` → `dist/modules/dolly/dolly.dll`

Archived (not in solution): `legacy/mmultiplayer/`

All **Win32 / x86**. Mod project `OutDir` should be `$(SolutionDir)dist\modules\<modname>\`.

## Runtime entry

Users run `ModuleLauncher.bat` or `ModuleLauncher.exe` in the game folder. CLI auto mode:

```powershell
Start-Process ".\ModuleLauncher.exe" -ArgumentList "/auto" -Wait
```

Exit code `0` = success.
