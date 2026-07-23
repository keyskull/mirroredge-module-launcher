# AI Agent Guide — mirroredge-module-launcher

This repository maintains **structured technical documentation** under [`docs/`](docs/README.md) for AI agents and contributors.

## Before making changes

1. Read [`docs/README.md`](docs/README.md) for the documentation index.
2. Match the topic to the right doc:
   - Architecture / file map → [`docs/architecture.md`](docs/architecture.md)
   - Injection, timing, IPC → [`docs/injection-and-ipc.md`](docs/injection-and-ipc.md)
   - **module_manager / split injection** → [`docs/module-manager.md`](docs/module-manager.md)
   - core / engine internals → [`docs/architecture.md`](docs/architecture.md); archived monolith → [`docs/mmultiplayer-mod.md`](docs/mmultiplayer-mod.md)
   - d3d9 graphics proxy → [`docs/d3d9proxy.md`](docs/d3d9proxy.md)
   - Adding a new mod → [`docs/adding-a-mod.md`](docs/adding-a-mod.md)
   - Build & deploy → [`docs/build-deploy.md`](docs/build-deploy.md)
   - Troubleshooting → [`docs/troubleshooting.md`](docs/troubleshooting.md)
   - **Known issues registry (防重复修复)** → [`docs/known-issues-workflow.md`](docs/known-issues-workflow.md), index [`docs/known-issues/README.md`](docs/known-issues/README.md)
  - **AI debug harness / MCP** → [`docs/ai-debug-harness.md`](docs/ai-debug-harness.md)
  - **MP Set Gameplay / bot visibility runbook (必读)** → [`docs/mp-set-gameplay-runbook.md`](docs/mp-set-gameplay-runbook.md)（v1.2.6 关键 8 门禁优先；§9 Tag/Interact B0/B1 协议）
  - **Cross-environment diagnostics logs** → [`docs/diagnostics-logging.md`](docs/diagnostics-logging.md)
  - **UE3 SDK internals (types, Safe layer, init, patterns)** → [`docs/me-sdk.md`](docs/me-sdk.md) (中文技术文档)
  - **SDK verification (纯指令 + runtime + optional Ghidra)** → [`docs/sdk-verification.md`](docs/sdk-verification.md)
  - **Multi-machine harness results / test-logs merge** → [`test-logs/README.md`](test-logs/README.md), [`docs/test-environments.md`](docs/test-environments.md)
3. User-facing setup and usage → root [`README.md`](README.md)

## Repository layout

| Path | Role |
|------|------|
| `launcher/` | `ModuleLauncher.exe`, deploy/UI, log relay; **`launcher/proxy/d3d9/`** builds `dist/d3d9.dll` |
| `runtime/` | All in-game DLL sources → `dist/modules/` (see [`runtime/README.md`](runtime/README.md)) |
| `shared/` | IPC contracts, SDK (`me_sdk/`), hooks, ImGui, plugin UI facade |
| `legacy/` | Archived monolith (`legacy/mmultiplayer/`, not in solution) |
| `mods/` | External feature plugins only (`multiplayer`, `trainer`, `dolly`) |

## Before fixing a bug (防重复修复)

1. Read [`docs/known-issues-workflow.md`](docs/known-issues-workflow.md) and search [`docs/known-issues/`](docs/known-issues/README.md) + [`docs/troubleshooting.md`](docs/troubleshooting.md).
2. Check **Failed approaches — do NOT retry** in any matching KI entry; do not re-attempt those fixes without new evidence (document in the KI changelog).
3. After a non-trivial fix: update or create a KI entry (copy [`docs/known-issues/_template.md`](docs/known-issues/_template.md)), cross-link troubleshooting, optionally `memory_save_atom` (`scope: engineering`, `topic: known-issue/KI-…`).

## Critical constraints (do not violate)

- **Win32 / x86 only** — Mirror's Edge is 32-bit.
- **Default core plugin** is `core.dll` (+ `engine.dll`; not `Client.dll`).
- **Default mode** is **d3d9 proxy + module_manager**: launcher deploys `Binaries\d3d9.dll`, proxy loads `module_manager.dll` at CreateDevice; **core** loaded in-game from Module Manager → Modules tab. No launcher remote injection.
- **No administrator rights** — manifest `asInvoker`, no `CreateRemoteThread` from launcher.
- **module_manager export**: `MmOnD3D9DeviceCreated` must be undecorated (`module_manager.def`) or proxy device notify fails.
- **Injection timing** — launcher waits for `Local\module_manager_ready` or toolhelp module snapshot; there is **no** remote inject worker and **no** `game_probe.cpp` memory scan.
- **D3D9 hook safety**: after `d3d9.dll` loads, never hook `Direct3DCreate9` or scan live D3D device from the inject worker thread. Split mode: use proxy device via `OnProxyDeviceCreated`; never `FindDeviceSafe` when proxy active. Legacy mmultiplayer: `PeekMessage` bootstrap + lazy hooks (`legacy/mmultiplayer/engine.cpp` or `runtime/engine/engine.cpp`).
- **Shared IPC names** live in `shared/module_contract.h` — single source of truth for launcher, mod, and proxy.
- **Core settings** use `%TEMP%\core.settings` (user overrides). Launcher + mod auto-load use game-root **`settings.json`** (`launcher.*`, `mods.autoLoad`). Legacy `%TEMP%\mirroredge-launcher.settings` and `modules/core.config.json` `loader.autoModules` migrate on first read.

## UE3 SDK fixes (`shared/me_sdk/`)

When testing shows bad SDK data — wrong member offset, incorrect `ProcessEvent` params struct, or `InitializeGlobals()` pattern failure on a valid ME 1.0 build — **fix the SDK in `shared/me_sdk/` directly** (see `generated/` for dump, `runtime/` for init/patterns). Do not add mod-specific workarounds in `runtime/` when the root cause is the dump or `runtime/init.cpp` patterns.

1. Confirm against the ME 1.0 binary (ReClass, Cheat Engine, or in-game observation).
2. Patch the relevant `ME_*_classes.hpp`, `ME_*_parameters.hpp`, or `init.cpp` / `pattern.cpp`.
3. Rebuild every DLL that links the changed SDK units.
4. Run `.\tools\sdk-verify\generate-static-asserts.ps1` to regenerate `sdk_verify_generated.h` if class sizes changed.
5. Note non-obvious fixes in [`docs/troubleshooting.md`](docs/troubleshooting.md) or [`docs/adding-a-mod.md`](docs/adding-a-mod.md).

## After significant changes

Update the relevant `docs/*.md` file(s) so future AI sessions stay accurate.
