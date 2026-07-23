# External feature mods

Optional plugins loaded from Module Manager → **Modules** tab after **core** is injected.

Built-in runtime (overlay host, core, engine, console) lives in [`runtime/`](../runtime/README.md).

## Active mods (in `ModuleLauncher.sln`)

| Folder | Deploy output | Role |
|--------|---------------|------|
| [`multiplayer/`](multiplayer/) | `multiplayer.dll` (+ optional `multiplayer-server.exe`) | Multiplayer networking UI |
| [`trainer/`](trainer/) | `trainer.dll` | Trainer / fly / god mode |
| [`dolly/`](dolly/) | `dolly.dll` | Camera dolly / markers |

## Layout

- **Do not** add duplicate copies of `runtime/` sources here (`module_manager`, `core`, `engine`, etc.).
- Legacy monolith sources: [`legacy/mmultiplayer/`](../legacy/mmultiplayer/) (not built).
- Stale duplicate trees under `mods/` (old `module_manager`, `mm-core`, `mp-*`, etc.) were removed; only feature mods remain.

## Dependencies

Feature mods resolve gameplay API via `GetProcAddress(core.dll, "MMOD_GetMmultiplayerApi")` and link headers under `shared/` only.
