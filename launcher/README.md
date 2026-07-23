# Launcher

Windows desktop app that deploys mod files, optionally installs the d3d9 proxy into `Binaries\`, starts Mirror's Edge, and waits for `module_manager.dll` to signal ready.

| Area | Path |
|------|------|
| Entry / flow | `main.cpp`, `injection_flow.cpp` |
| Deploy paths | `paths.cpp`, `game_launch.cpp` |
| Settings | `config.cpp`, `launcher_settings.cpp`, `game_config.cpp` |
| UI | `ui/status_dialog.cpp` |
| Log relay | `log_server.cpp` |
| **D3D9 proxy** | `proxy/d3d9/` → `dist/d3d9.dll` (see [`docs/d3d9proxy.md`](../docs/d3d9proxy.md)) |

In-game modules live under [`runtime/`](../runtime/README.md).
