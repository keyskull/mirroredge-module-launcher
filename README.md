# Mirror's Edge Module Launcher

Standalone Windows launcher for Mirror's Edge (32-bit) modding. Deploys a **d3d9** graphics proxy that loads **module_manager**; **core** (+ **engine**) and feature mods load in-game from the Module Manager UI.

**Current release:** see [`version.json`](version.json) and [`CHANGELOG.md`](CHANGELOG.md). GitHub Releases: auto-update from the launcher; UI **中文/English** (top-right combo, launcher.uiLanguage); pack with [`tools/pack-release.ps1`](tools/pack-release.ps1). See [`docs/build-deploy.md`](docs/build-deploy.md) § Release / auto-update.

## Features

- **Launch game** from the launcher window
- **GitHub auto-update** — checks Latest Release; one-click download + apply
- **d3d9 proxy deploy** — no administrator / no remote DLL injection
- **Live status UI** with step-by-step log (manual close only)
- **Mod log relay** — in-game mod messages stream to the launcher via named pipe
- **Auto mode** (`/auto`) for scripting
- **Modular build** — launcher core plus independent module projects under `runtime/`

## Requirements

- Windows 10+
- Mirror's Edge (32-bit) installed
- Visual Studio 2022 with **Desktop development with C++**
- [DirectX SDK (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=6812) — required for `engine.dll` (d3dx9)
- Go 1.21+ — optional, builds `multiplayer-server.exe` (skip with `-SkipServer`)
- **No administrator rights** required at runtime

## Build

```powershell
.\build.ps1
```

Builds launcher + mods to `dist/`, then **automatically deploys** to the game root directory.

| Script | Behavior |
|--------|----------|
| `.\build.ps1` | Build + deploy (default) |
| `.\deploy.ps1` | Same as `build.ps1` |
| `.\build.ps1 -NoDeploy` | Build only, no copy to game |
| `.\build.ps1 -DeployProxy` | Also copy `d3d9.dll` into `Binaries\` |
| `.\build.ps1 -SkipServer` | Skip Go server build |

### Deploy target

Copy `deploy.config.json.example` → `deploy.config.json` and set `deployPath`:

```json
{
  "deployPath": "F:\\EA Games\\Mirrors Edge",
  "deployProxy": false
}
```

Or set environment variable `ME_DEPLOY_PATH`. If neither is set, defaults to the parent folder of this repo (`..\`).

Deployed layout:

```
F:\EA Games\Mirrors Edge\
  ModuleLauncher.bat
  ModuleLauncher.exe
  mirroredge-module-launcher.exe
  modules\core\core.dll
  modules\engine\engine.dll
  modules\module_manager\module_manager.dll
  modules\multiplayer\multiplayer.dll
  Binaries\d3d9.dll
  Binaries\MirrorsEdge.exe   (game exe, unchanged)
```

Legacy explicit flag (still supported): `.\build.ps1 -Deploy`

Output:

| File | Description |
|------|-------------|
| `dist/ModuleLauncher.exe` | Primary launcher executable |
| `dist/mirroredge-module-launcher.exe` | Alias copy |
| `dist/modules/core/core.dll` | Core plugin (menu, IPC, settings) |
| `dist/modules/engine/engine.dll` | Gameplay / D3D9 hooks |
| `dist/modules/module_manager/module_manager.dll` | Overlay host (proxy-loaded) |
| `dist/modules/multiplayer/multiplayer.dll` | Multiplayer feature mod |
| `dist/modules/multiplayer/multiplayer-server.exe` | Standalone Go server (optional) |
| `dist/d3d9.dll` | Graphics proxy (deployed to `Binaries\`) |

## Usage

1. Run `.\build.ps1` (builds and deploys automatically).
2. **Double-click `ModuleLauncher.bat`** (or `ModuleLauncher.exe`) in the game folder — no UAC prompt in default proxy mode.
3. Click **启动游戏** or start the game manually.
4. Wait until launcher reports `module_manager 已由 d3d9 代理加载就绪`.
5. In-game, press **Insert** or **F10** → Module Manager → **Modules** → inject **core** (loads `engine.dll` automatically).
6. Enable **multiplayer**, **trainer**, or **dolly** from the Modules tab as needed.
7. Keep the launcher open for `[mod]` logs; click **关闭** when finished.

### Mod log relay

The launcher starts a named pipe server at `\\.\pipe\mirroredge_module_log` (see `shared/module_contract.h`). After injection, the mod sends UTF-8 log lines through this pipe; the launcher UI prefixes them with `[mod]`.

### Module Manager (in-game)

After **core** is loaded, open **Module Manager** with **Insert** or **F10**. Tabs:

| Tab | Purpose |
|-----|---------|
| **Modules** | Inject core; enable multiplayer / trainer / dolly |
| **Engine / World** | Engine tweaks (core menu) |
| **Multiplayer** | Internet connection, chat, player sync |
| **Trainer** | Cheats and hotkeys (when enabled) |
| **Dolly** | Cinematic camera (when enabled) |

Press **`` ` ``** (grave) for the developer console (`inject core`, `status`, `modules`, …).

The launcher window is for injection status and logs only.

### Auto mode (scripts)

```powershell
Start-Process ".\ModuleLauncher.exe" -ArgumentList "/auto" -Wait
```

Exit code `0` = success.

## d3d9 proxy

The launcher deploys `dist/d3d9.dll` to `Binaries\` before launch. The proxy loads `module_manager.dll` when D3D9 initializes. See [docs/d3d9proxy.md](docs/d3d9proxy.md).

## Module DLL layout

Hosted plugins live under the game root:

| Path | Role |
|------|------|
| `modules/module_manager/module_manager.dll` | Overlay host (loaded by proxy) |
| `modules/core/core.dll` | Default plugin — inject first from Modules tab |
| `modules/engine/engine.dll` | Loaded automatically by core |
| `modules/multiplayer/multiplayer.dll` | Multiplayer feature mod |
| `modules/trainer/trainer.dll` | Trainer |
| `modules/dolly/dolly.dll` | Camera dolly |

Constants in `shared/module_contract.h`; deploy paths wired in `launcher/config.h`. ImGui is static-linked into module_manager — plugins draw via `ModHostApi.ui` / `plugin_ui.h`.

## Configuration

Edit `launcher/config.h` or `shared/module_contract.h` for cross-process names:

- `gameProcessName` / `gameExecutable`
- `managerReadyEventName` — `Local\module_manager_ready`
- `moduleLogPipeName` — named pipe for mod → launcher log relay
- Core settings: `%TEMP%\core.settings` + `modules/core.config.json`

## Project layout

```
shared/
  module_contract.h       Launcher <-> mod IPC names (single source of truth)

launcher/                 Core launcher + d3d9 proxy
  proxy/d3d9/             Graphics proxy → dist/d3d9.dll

runtime/                  Built-in in-game DLL sources → dist/modules/
  module_manager/         Overlay host
  console/                Developer console (mm-console.dll)
  core/                   Core plugin (+ core.config.json)
  engine/                 Gameplay engine

mods/                     External feature plugins
  multiplayer/            Multiplayer client + Go server
  trainer/ / dolly/       Trainer and camera dolly

legacy/
  mmultiplayer/           Archived monolith (not built)

build.ps1                 Builds launcher + all modules, stages dist/
ModuleLauncher.sln        Visual Studio solution (all targets)
dist/                     Build output
```

## Adding a new mod

See [docs/adding-a-mod.md](docs/adding-a-mod.md). Feature mods link `shared/` only and resolve `MMOD_GetMmultiplayerApi` from `core.dll`.

## Technical documentation (AI / contributors)

Structured architecture and implementation docs live in [`docs/`](docs/README.md). AI agents should start from [`AGENTS.md`](AGENTS.md).

## License

MIT — see [LICENSE](LICENSE).
