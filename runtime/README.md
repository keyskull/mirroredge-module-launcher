# Runtime (in-game modules)

Built-in DLLs injected or loaded inside **Mirror's Edge**. Built output goes to `dist/modules/<id>/` and is deployed to `<gameRoot>/modules/`.

| Folder | Deploy output | Role |
|--------|---------------|------|
| `module_manager/` | `module_manager.dll` | Overlay host, plugin registry (loaded by d3d9 proxy) |
| `console/` | `mm-console.dll` | Developer console UI (auto-loaded by module_manager) |
| `core/` | `core.dll` | Plugin shell, menu, IPC, settings, auto-load |
| `engine/` | `engine.dll` | Gameplay hooks, API (auto-loaded by core) |

External feature plugins: [`mods/`](../mods/README.md) (`multiplayer`, `trainer`, `dolly`).

Shared contracts and SDK: `shared/` (repo root). Launcher + proxy: `launcher/`.

Game config: `<gameRoot>/modules/core.config.json` (see `core/core.config.json` template).
