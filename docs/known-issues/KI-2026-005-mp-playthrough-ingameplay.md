# KI-2026-005: mp-playthrough-bots cannot detect gameplay / enter level (hosted split)

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-005 |
| **状态** | `mitigated` |
| **首次记录** | 2026-06-30 |
| **最后验证** | 2026-07-23 |
| **区域** | module_manager / harness / multiplayer |
| **标签** | `playthrough`, `harness`, `ingameplay`, `currentMap`, `thread-safety`, `crash` |

## 症状（Symptoms）

- Harness scenario `mp-playthrough-bots` fails after multiplayer connects at menu:
  - `Timed out waiting to enter gameplay (inGameplay still false)`, or
  - `Expected >= 2 remote players, got 0` (bots spawned but host never in a real level), or
  - **`multiplayer loaded timeout (last: (no status))`** — game process disappears during multiplayer injection (crash).
- **Deep crash:** game process dies silently when multiplayer listener thread calls `SyncCurrentLevelFromWorld()` → `Engine::GetWorld(false)` from a non-game-thread, triggering UE3 access violation.
- **Remote spawn crash:** two-machine manual `Set Gameplay` reaches matching `tutorial_p` levels, then host exits with WER `BEX`, fault module `modules\engine\engine.dll`, exception `0xc0000409`, fault offset `0x000149d4` / `0x00014a34` (`___report_gsfailure` fast fail).
- **Two-machine heartbeat timeout:** after the second machine joins, the server can log both clients as `timed out`, delete the room (`0 rooms, 0 clients`), then later print `level message for unknown client id=...` from the same remote address.
- `GET_STATUS` / `Expand-CoreHarnessStatus` often reports `gameReady: true` but `currentMap: ""`, `gameHwnd: 0`, `inGameplay: false`, `gameplayHooks: false` while visually on main menu or loading screen.
- Intro skip logs show `map_pending` / empty `map` for all poll rounds.

## 根因（Root cause，须已验证）

**Partially verified (2026-06-30):**

1. **Map polling:** `core` `GetCurrentMapName()` returns empty when `Engine::GetWorld(false)` is null — common during main menu / pre-world boot in hosted split mode, so harness cannot confirm `tdmainmenu` or level load via pipe alone.
2. **Bot spawn:** Follower bots require the host client to be in the same room **and** level on the server. **Verified (2026-06-30):** host was joining default room `lobby` because harness only wrote `%TEMP%\core.settings`; multiplayer plugin loads `%TEMP%\multiplayer.settings` at DLL init — room mismatch → `mpRemotePlayers` 0.
3. **Fixed (separate):** Multiplayer TCP listener only started when Multiplayer ImGui tab rendered — fixed by calling `StartClientListenerIfNeeded()` from `ClientPlugin::Initialize()` (`mods/multiplayer/client.cpp`).

**Not yet verified:** Whether blind Enter/Enter-only menu navigation can reliably start campaign on all ME builds from harness without map feedback.

4. **Thread-safety crash (2026-07-04):** `ClientListener` / hosted status threads call `UpdateHarnessSnapshot()` → `SyncCurrentLevelFromWorld()` → `Engine::GetWorld(false)` after connecting to the local server. `GetWorld` is not thread-safe (UE3); calling it from multiplayer background threads at the right timing causes access violations. **Fixed:** `SyncCurrentLevelFromWorld` now skips on multiplayer background threads, and listener/status/UDP threads run through SEH guards so future faults are logged instead of killing the game.
5. **Level logs missing after entering a map (2026-07-04):** Registering `Engine::OnPostLevelLoad` callbacks is not enough in hosted split mode; the underlying gameplay hooks still install lazily. Forcing those hooks from multiplayer is unsafe during level transition; prefer a main-thread, SEH-guarded map probe that never runs on listener/status threads.
6. **Spawn queue stack corruption (2026-07-06):** `Engine::SpawnCharacter` queued `ASkeletalMeshActorSpawnable *&`. Through the C ABI, `SpawnCharacterWrapper` passed a local `actor` variable by reference, returned, and later `TickHook` wrote the spawned actor into that expired stack slot. This corrupts the stack cookie and reports as `engine.dll` `0xc0000409` at `___report_gsfailure`. The queued spawn request must store the caller's stable out pointer (`ASkeletalMeshActorSpawnable **`), reject current-thread stack out pointers, and the feature adapter must pass `&spawned` directly.
7. **Heartbeat timeout after second client joins (2026-07-06):** The client TCP receive path held `g_tcpSocketMutex` while blocking in `recv`, so status/heartbeat sends could be delayed behind the listener. The server also used a 5-second timeout and synchronous TCP writes with no deadline; a short loading/network stall after the second client joined could remove both clients, while stale TCP connections continued sending messages that logged as unknown-client ids. Release the client socket mutex before blocking receive; use a longer server timeout, TCP write deadlines, idempotent room removal, and close timed-out TCP connections.
8. **Faith remote visual instability (2026-07-06):** With the server timeout fixed, host crash still reproduced immediately after the remote client reached `tutorial_p` (`engine.dll` `0xc0000409` offset `0x00014a34`). The reproducing client had the default `Faith` character. The legacy Faith spawn entry uses `CH_TKY_Crim_Fixer` plus `Transient.MaterialInstanceConstant_69..78`; those transient material names are not stable across level/client processes and can destabilize spawn/material setup. Multiplayer now remaps remote Faith visuals to Kate for spawn/bone transform, skips missing materials instead of calling `SetMaterial(nullptr)`, fixes UDP packet writes to take the exclusive player lock, and validates bone transform buffers before copying.
9. **Pre-spawn crash before engine spawn logging (2026-07-06):** A later two-machine run crashed the host after the remote reached `tutorial_p`, but `%TEMP%\mirroredge-engine-spawn.log` was absent and `%TEMP%\mirroredge-multiplayer-client.log` stopped immediately after remote level sync. That means the crash happened before `Engine::SpawnCharacter` and before the engine spawn queue could log. Do not trigger remote presentation/spawn work from the TCP listener's level-message path; install remote presentation hooks from the host's manual `Set Gameplay` path and let the game tick own spawn retries.
    42|10. **Manual Set Gameplay races with level-load hooks (2026-07-06) — SUPERSEDED by #11:** `ApplyManualClientLevel("gameplay")` queues `ApplyManualClientLevelOnMainThread("gameplay")` via `QueueClientEngineTask`. If the game engine is already loading a real map (e.g. `tutorial_p` from campaign progression), `OnPreLevelLoad` / `OnPostLevelLoad` fire and perform all multiplayer setup (hooks, activation, spawn queue) before the queued task executes. The queued task then redundantly calls `EnsureClientRemotePlayerPresentation()`, `QueueActivateHostedGameplay()` (resetting `g_activationRetries` to 0 via `RequestGameplayActivation`), and `QueueSpawnEligibleRemotePlayers()` — corrupting the activation state. Fix: `ApplyManualClientLevelOnMainThread` now checks whether `client.Level` was already updated by level-load hooks to a real map name; if so, it skips the redundant init since `OnPostLevelLoad` already handled it.
       |
       |**Note (2026-07-06):** The guard in #10 does not trigger because level-load hooks (`OnPreLevelLoad`/`OnPostLevelLoad`) are registered via `EnsureClientRuntimeHooks()` (called from `MultiplayerTab()`) but do **not** fire for the `gameplay → tutorial_p` campaign transition. See #11 for the actual root cause.
    43|11. **OnBonesTick corrupts Kate mesh with Faith default bone data (2026-07-06):** After bot spawn in a real level, `OnBonesTick` fires for the bot's skeletal mesh. When `p->ToTime == 0` (bot with no real UDP pose data), the else-branch calls `TransformBones(Kate, bones, p->LastPacket.Bones)`, applying the 75-atom Faith default bone array to a Kate mesh. Inside `TransformBones`, the Kate case executes `memcpy(dest + 39, src + 45, 63 * sizeof(FBoneAtom))` — reading **108 unique source entries from a 75-entry array**. The 33 entries beyond `src[74]` read garbage from the `Player` struct's heap memory beyond `PACKET.Bones`, corrupting Kate's skeletal mesh with invalid bone transforms. This destabilizes the render pipeline and eventually triggers a stack-cookie fastfail (`0xc0000409`) in `engine.dll` when a GS-protected render function returns with a corrupted stack. Fix: `OnBonesTick` now skips `TransformBones` entirely when `p->ToTime == 0` (no real UDP pose data); spawned actors render in their default rest pose instead. Also hardened `ValidateBoneTransformBuffers` to validate the source read region for each character case.
12. **EnsureClientRenderHook in EnsureClientRemotePlayerPresentation causes concurrent renderScene.Callbacks corruption (2026-07-06):** During manual `Set Gameplay`, `ApplyManualClientLevelOnMainThread` (executed from `QueueClientEngineTask` on the game thread) calls `EnsureClientRemotePlayerPresentation()`, which calls `EnsureClientRenderHook()`. `EnsureClientRenderHook()` pushes `OnRender` into `renderScene.Callbacks` via `Engine::OnRenderScene()`. Meanwhile, the D3D9 `EndScene` hook fires `DispatchRenderSceneCallbacks()` on the render thread, iterating the same un-synchronized `renderScene.Callbacks` vector. Concurrent `push_back` during iteration corrupts the vector, causing `0xc0000409` stack-cookie fastfail in `engine.dll`. This affects both hosted-split (machine 1) where `host->OnRenderScene` dispatches to core.dll's render loop, and remote (machine 2) where `DispatchRenderSceneCallbacks` runs from `EndSceneHook`. **Fix:** Remove `EnsureClientRenderHook()` call from `EnsureClientRemotePlayerPresentation()` — `OnRender` is already registered at startup by `InstallClientRuntimeHooks()`, and calling it again from an engine task during level transition is both redundant and unsafe.

## Verified fix (2026-07-06)

- `EnsureClientRemotePlayerPresentation()` no longer calls `EnsureClientRenderHook()`.
- All other hooks (tick, actor tick, bones tick), gameplay activation, spawn queue, and engine-level `BonesTickHook` remain operational.
- Verified on both 1号机 (hosted split) and 2号机 (remote client): no crash on manual Set Gameplay; map name updates; remote players visible.

| Change | Verification |
|--------|----------------|
| `StartClientListenerIfNeeded()` in `ClientPlugin::Initialize()` | `mp-playthrough-bots` reaches `connected_at_menu` without opening Multiplayer tab |
| Harness `Expand-CoreHarnessStatus` + `Test-MultiplayerHarnessConnected` | No false-positive `mpConnected`; no `currentMap` property errors |
| Harness inject fallbacks (`INJECT`, `CONSOLE_EXEC`, UI scroll) | `mp-gui-test`, `user-flow`, `user-full-session` PASS |
| `user-full-session`: `session\|pass` before teardown | Visual `session_pass` capture succeeds |
| Playthrough: spawn bots at `tdmainmenu` before level entry; gentle Enter / Enter+Escape; `GET_STATUS` merges `multiplayer.clientMap` / `inGameplay` | Harness sees 2 remotes at menu (2026-06-30, **re-verified 2026-07-02 1号机**) |
| Harness writes `%TEMP%\multiplayer.settings` before `INJECT multiplayer` (same room as bots) | `mpRemotePlayers: 2` at menu (2026-06-30) |
| Enter-only campaign start + `SyncCurrentLevelFromWorld` in `UpdateHarnessSnapshot` + `CONSOLE open` fallback | Pending harness re-run |
| `SyncCurrentLevelFromWorld` guards multiplayer background threads (skip `GetWorld` on listener/status threads) | `smoke-split` PASS (2026-07-04); `mp-gui-test` PASS with no `hosted_status` memory fault (2026-07-04) |
| Async plugin loading + callback/thread SEH guards | Build PASS + `smoke-split` PASS (2026-07-04) |
| Multiplayer connection logging restored (`MpDebugLog` + `ModLog`) | `mp-gui-test` shows `client: connecting`, `client: connected`, and `component:"multiplayer"` / `H-CONN` events (2026-07-04) |
| Multiplayer tab exposes explicit `Set Gameplay` / `Set Menu` controls; `Set Gameplay` calls `Engine::RequestGameplayActivation()` (API v3) so pawn/map probes and `SetHostedGameplayLive` stay in engine.dll | Pending in-game verification |
| Spawn queue stores stable out pointers (`ASkeletalMeshActorSpawnable **`); C ABI wrapper forwards the caller out slot; engine rejects stack out pointers; feature adapter no longer queues a wrapper-local stack reference | `.\build.ps1 -SkipServer` PASS and deploys `modules\engine\engine.dll` + `modules\multiplayer\multiplayer.dll` (2026-07-06); two-machine spawn verification pending |
| Client TCP receive no longer holds the socket mutex during blocking `recv`; server heartbeat timeout is 45s, TCP writes have deadlines, timed-out TCP connections are closed, and disconnect cleanup is idempotent | `go test ./...` PASS in `mods/multiplayer/server` (2026-07-06); full two-machine retest pending |
| Multiplayer remote Faith visual remaps to Kate; remote bone transform uses the visual character; UDP packet application uses exclusive player lock; `TransformBones` validates source/destination buffers; missing materials are skipped | `.\build.ps1` PASS and deploys `modules\engine\engine.dll` + `modules\multiplayer\multiplayer.dll` (2026-07-06); manual two-machine retest pending |
| Remote spawn crash diagnostics: host LocalDumps enabled; multiplayer client writes `%TEMP%\mirroredge-multiplayer-client.log`; engine spawn stages write `%TEMP%\mirroredge-engine-spawn.log` | `.\build.ps1` PASS and deploys diagnostic DLLs (2026-07-06); use next crash dump/logs to identify exact failing spawn stage |
| Remote level messages no longer queue spawn/presentation work from the TCP listener; host manual `Set Gameplay` installs remote presentation hooks early, and spawn retries stay on the game tick | `.\build.ps1` PASS and deploys `modules\engine\engine.dll` + `modules\multiplayer\multiplayer.dll` (2026-07-06); expect `client: remote spawn deferred ...` before any tick-time spawn logs |
| **Manual Set Gameplay vs level-load race:** `ApplyManualClientLevelOnMainThread` skips redundant init when `client.Level` was already updated to a real map name by `OnPreLevelLoad`/`OnPostLevelLoad` — prevents activation state corruption and delayed crash during tick-time bone transforms | `.\build.ps1` PASS (2026-07-06); guard does **not** trigger because level-load hooks don't fire for this transition; superseded by Fix #11 below |
| **Bone transform skips bots with no UDP data:** `OnBonesTick` skips `TransformBones` when `p->ToTime == 0` — prevents source buffer over-read (Faith 75-atom → Kate 108-atom read) that writes garbage bone data to Kate mesh, corrupting the render pipeline and causing `0xc0000409` fastfail in `engine.dll` | `.\build.ps1` PASS and deploys `modules\multiplayer\multiplayer.dll` (2026-07-06); expect no crash on 1号机 manual Set Gameplay → tutorial_p; bot actors render in rest pose |

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-06-30 | `SET client.server` / `SET client.room` on core pipe **after** intro boot wait | Pipe timeout | Game busy during cinematics; superseded by pre-intro `RELOAD_SETTINGS` from `%TEMP%\core.settings` |
| 2026-06-30 | `Test-MultiplayerHarnessConnected` fallback: TCP 5222 + multiplayer module loaded | False pass | `connected_at_menu` with `map=""`; not actually connected to room gameplay |
| 2026-06-30 | `Wait-GameMainMenuReady` with `MaxSkipRounds` assume `tdmainmenu` when map empty | Unstable | Assumes menu without world; Enter does not start level |
| 2026-06-30 | Inject core **after** intro (no core during skip) | Timeout | `Wait-GameMainMenuReady` 90s; core pipe timeouts during poll |
| 2026-06-30 | `Invoke-RealUserEnterLevelFromMenu` only (25 rounds Enter+Escape) | Fail | `inGameplay` never true |
| 2026-06-30 | `Invoke-GameStartFromMenu` blind Enter+Escape + assume `levelMap=gameplay` | Bots 0 remotes | Host not in real level; screenshot still menu-like luminance |
| 2026-06-30 | `Invoke-GameStartFromMenu -EnterOnly` + `Wait-MmultiplayerInLevel` 150s | Fail | `inGameplay still false`; game window sometimes missing handle during load |
| 2026-06-30 | Hang guard during `Wait-MmultiplayerInLevel` without `-SkipHangCheck` | Fail | `IsHungAppWindow` during UE3 level load aborts scenario |
| 2026-06-30 | Spawn bots after fake `levelMap=gameplay` before host in level | Bots 0 remotes | Host still at `tdmainmenu`; level mismatch on server |
| 2026-06-30 | Menu level entry with Enter+Escape rounds (8+) after bots spawn | Fail | Escape cancels load dialogs; `clientMap` stays `tdmainmenu` |
| 2026-07-04 | Eager `SyncCurrentLevelFromWorld()` / remote-player refresh when Multiplayer loads at main menu | Crash | Main menu / pre-gameplay world can be unstable; do not call `Engine::GetWorld(false)` from plugin load/connect refresh paths |
| 2026-07-04 | Guard eager refresh with `Engine::CanSafelyUsePlayerPawn()` | Crash | `CanSafelyUsePlayerPawn()` calls `GetPlayerController(true)` / `GetPlayerPawn(true)` internally, so it is not a safe main-menu load guard |
| 2026-07-04 | Multiplayer listener calls `Engine::EnsureGameplayHooks()` so `LevelLoadHook` fires for level sync | Crash entering level; server sees no level log | Forcing gameplay hook install from multiplayer re-enters sensitive UE3 level-load hooks; do not auto-install gameplay hooks just to obtain map changes |
| 2026-07-04 | Active world/pawn probe from multiplayer (`GetWorld`, `TryFindActiveWorldInfo`, `GetPlayerPawn`) | Access violation or no UE3 objects (`world=0`, `pawn=0`) | Feature plugin does not have a reliable UE3 object context at menu/load timing; avoid automatic probing from multiplayer.dll |
| 2026-07-05 | `ApplyManualClientLevel("gameplay")` installs hooks / spawns remotes synchronously from ImGui `MultiplayerTab` (render path) | Crash in map after Set Gameplay | Do not register `OnTick` / `OnActorTick` / `OnRenderScene` or spawn actors from ImGui render callbacks; queue engine work via `QueueClientEngineTask` + `QueueActivateHostedGameplay` |
| 2026-07-05 | `TryActivateHostedGameplay` calls unguarded `CanSafelyUsePlayerPawn()` on main thread after manual Set Gameplay | Crash in map after Set Gameplay (still) | Wrap pawn probe + activation in `PluginSehGuard`; for manual Set Gameplay skip pawn probe after 3 faults/retries and activate hosted gameplay anyway |
| 2026-07-05 | Force `SetHostedGameplayLive(true)` after 30 activation retries while pawn probe still failing | Crash ~3s after Set Gameplay; server sees level change then TCP reset | Never force hosted gameplay live without `IsGameplayReadySafe()` or valid map probe; `TickHook` must not call unguarded `GetPlayerPawn(true)` for spawn/command queues |
| 2026-07-13 | RDP bypass: IAT patching `GetSystemMetrics` (EXE only) | Partial (~30%) | SecuROM duplicates IAT descriptor; game uses `GetProcAddress` to bypass |
| 2026-07-13 | RDP bypass: IAT patching all modules via `EnumProcessModules` (15 entries) | No effect (0/5) | Game uses `GetProcAddress`/ordinals to call `GetSystemMetrics`, bypassing all IAT entries |
| 2026-07-13 | RDP bypass: inline hook `GetSystemMetrics` (VirtualAlloc trampoline + JMP) | Crash | System `GetSystemMetrics` prologue is thunk `6A 10 68 xx` — not position-independent |
| 2026-07-13 | RDP bypass: inline hook `GetSystemMetrics` with prologue validation (`8B FF 55 8B EC` check) | No effect (5/5 RDP-ERR) | Prologue mismatch (`6A106828D9`), inline hook correctly skipped; IAT-only still bypassed |
| 2026-07-13 | RDP bypass: IAT `GetSystemMetrics` + `SystemParametersInfoA` (wrong variant) | No effect | Game imports `SystemParametersInfoW` (Unicode), not `A` |
| 2026-07-13 | RDP bypass: IAT `GetSystemMetrics` + `SystemParametersInfoW` (both, all modules) | No effect (0/5) | 15+12 entries patched, still detected — `GetProcAddress` bypass confirmed |
| 2026-07-13 | `FORCE_HOSTED_LIVE` / `DRAIN_SPAWNS` IPC commands for manual spawn | FAILED | Core pipe unreachable — core never initialized because CreateDevice never called (RDP block) |
| 2026-07-13 | `CONSOLE open tutorial_p` from harness to load level | Crash / timeout | Level load hooks not installed; game thread unstable without proper D3D init |
| 2026-07-13 | Per-lambda SEH guard in `FlushClientEngineTasks` | FAILED | Task queue never flushed — engine pump never reached game tick (RDP → no D3D → no rendering → no tick) |
| 2026-07-13 | Spawn drain via `EndScene` hook calling `MMOD_EngineDrainSpawnQueue` | FAILED | EndScene never fires — CreateDevice was never called (RDP block) |
| 2026-07-13 | `Engine::SpawnCharacter` with `TryFindActiveWorldInfo` for player-controller-aware world | Crash (exit 0xFFFFFFFF) | World not ready during hosted gameplay activation path |
| 2026-07-18 | Treat launcher `MmodDrainSpawnQueue queue=0` as Set Gameplay hang | Misdiagnosis | Activation already `set live`; empty queue = `listSize=0` (no remotes). Read `%TEMP%\mirroredge-multiplayer-client.log` first |
| 2026-07-18 | `GetWorld(true)` / `GetPlayerPawn(true)` on activation, EndScene, or first TickHook body | Freeze / crash (`GetPC.iterate.end`) | Full GObjects walk; use cache-only + `MeSdk::Safe::Gui::TryFindTdGameEngine` chain |
| 2026-07-19 | One-shot `TryFindTdGameEngine(true)` from EndScene `MMOD_EngineDrainSpawnQueue` | Hang at `drain.warm.tdengine`; EndScene stops | Same GObjects-walk class; use `TryWarmTdGameEngineIncremental` only; `refresh=false` is cache-only |
| 2026-07-19 | Hosted-mode TickHook spawn drain (with EndScene drain) | Hang at `spawn.char.begin` | UE3 Spawn re-enters PeekMessage; hosted Tick must defer to EndScene |
| 2026-07-19 | `TryWarmTdGameEngineIncremental` from TickHook | Hang at `tick.warm.slice` | Warm EndScene only |
| 2026-07-19 | `TryFindTdPlayerController(false)` inside EndScene warm | Minutes stuck at `drain.warm.slice` | Empty PC cache still `ForEachGlobalObject`; seed only via `GetPlayerController(false)` |
| 2026-07-19 | Warm-before-spawn / gate drain on warm | Hung warm starves spawn retries | Spawn first; warm only for remaining queue |
| 2026-07-19 | VirtualQuery (`BoundedTArrayCount`/`IsReadableMemory`) per GObjects slot in warm | Multi-minute EndScene slices | Raw buffer + SEH SuperField IsA |
| 2026-07-19 | Expect remote movement without host UDP (no pawn / menu FORCE_HOSTED_LIVE) | Bots spawn but never `remote pose applied` | Server pull-model; host must heartbeat — fixed in `OnLocalPoseNetworkTick` |
| 2026-07-19 | `ENSURE_GAMEPLAY_HOOKS` then Enter Story / New Game | White frame; EndScene stall | Install gameplay hooks only after `START_NEW_GAME` load settles |
| 2026-07-19 | `START_GAME <map>` / bare `MMOD_GAME_MAP=` | ProcessEvent returns OK but no `tutorial_p` | Use `START_NEW_GAME` → `StartNewGameWithTutorial` |
| 2026-07-19 | `bot.ps1` `pos = 500 + botId*100` | Astronomical coords; Actor lost; harness `spawnedPlayers=0` | Offset by Character slot only |
| 2026-07-19 | Per-UDP `QueueClientEngineTask(ApplyInitialRemotePlayerPose)` | Main-thread flood / console spam | One-shot initial pose only; ticks apply continuous pose |
| 2026-07-19 | `FindWindow` exact title / UTF8 BOM pipes | `INJECT_KEY` no-op; `ERR unknown` | PID EnumWindows + ASCII pipe bytes |
| 2026-07-19 | Empty `currentMap` ⇒ not in level | False negative in tutorial | Seed WorldInfo from PC; also use `clientMap` / screenshot |
| 2026-07-19 | TCP `character` msg always despawn Actor | `spawn ok` then `spawnedPlayers=0` | Only despawn/respawn when character **changes** |
| 2026-07-19 | Treat `cam-*.png` nametag/chat as mesh proof | False visual pass | Need visible skeletal mesh; zero-bone bots + Follow-at-camera hid models |
| 2026-07-19 | `TransformBones` whenever `ToTime>0` | Invisible collapsed mesh | Harness bots send all-zero compressed bones; require `HasRemoteBoneMotion` |
| 2026-07-19 | Multiplayer `TryEnsureGameplayHooksAtPluginInit` (hooks before Story) | Hitch / freeze on `pre level load tutorial_p`; no `post level load` | Defer hooks to Set Gameplay; do not wrap LoadMap at plugin init |
| 2026-07-19 | `LevelLoadHook` hold `spawns.Mutex` across entire LoadMap | Render freeze during level entry (EndScene drain blocked) | Clear queue under short lock; release mutex before `CallLevelLoadOriginalSafe` |
| 2026-07-19 | `Engine::Despawn` remote actors in `OnPreLevelLoad` | Stall / crash during world tear-down | Null `Actor` pointers only; respawn after PostLevelLoad |
| 2026-07-19 | MP `OnActorTick` location for every world `AActor` | ~0.4 FPS after bots spawn | Apply poses once per network tick (`ApplyRemotePlayerWorldPoses`) |
| 2026-07-19 | Bare `bot.ps1` without Follow / lock first UDP peer at ~500,500 | Bots far from host | Follow default on; prefer highest-|pos| peer; `-NoFollow` for demo |
| 2026-07-19 | Em-dash in `bot.ps1` double-quoted strings | PS 5.1 parse error | ASCII only in double-quoted strings |

## 相关

- **源码：** `runtime/core/mod_ipc.cpp` (`GetCurrentMapName`), `mods/multiplayer/client.cpp`, `tools/debug-harness/lib/DebugHarness.psm1` (`Test-MmultiplayerPlaythroughWithBots`, `Expand-CoreHarnessStatus`)
- **文档：** [mp-set-gameplay-runbook.md](../mp-set-gameplay-runbook.md), [troubleshooting.md](../troubleshooting.md), [ai-debug-harness.md](../ai-debug-harness.md)
- **Harness：** `mp-playthrough-bots`, `tools/mp-real-level-bots.ps1`

## 变更日志

| 日期 | 作者 | 说明 |
|------|------|------|
| 2026-07-23 | playthrough+LAN | **Test-MmultiplayerPlaythroughWithBots** also delegates to `mp-real-level-bots` (scenario + function). LAN client soak automates inject/`START_NEW_GAME`/`FORCE_HOSTED_LIVE`. motionPass requires `udp seq stream`. |
| 2026-07-23 | playthrough path | **Harness path fix:** `tools/debug-harness/scenarios/mp-playthrough-bots.ps1` now delegates to `tools/mp-real-level-bots.ps1` (`START_NEW_GAME` → hooks after map → `FORCE_HOSTED_LIVE` → Follow bots). Status → `mitigated`. Two-machine LAN soak script: `tools/mp-lan-dual-soak.ps1` (manual client inject still required). Do not reintroduce Enter-only / pre-level `ENSURE_GAMEPLAY_HOOKS`. |
| 2026-07-19 | visual bots | **Harness PASS (log)**: spawn ok×2 + `set material` + pose after Loading no longer blanks `GetPlayerController(false)`. Bot Follow still starts far from host — near-camera mesh not yet solidly proven in screenshots. |
| 2026-07-19 | cold warm speedup | Removed `TryFindTdPlayerController` from warm; SEH SuperField IsA; spawn-before-warm. Verified Steam: `queue→spawn ok` ~7s for 2 bots. |
| 2026-07-19 | host UDP heartbeat | `OnLocalPoseNetworkTick` sends empty packets when live but no pawn so pull-model server relays bot poses. Verified `remote pose applied`×2. |
| 2026-07-19 | real-level harness path | `START_NEW_GAME` + delayed hooks enters `tutorial_p` (screenshot). Fixed `INJECT_KEY` substr off-by-one; botId coord bug; UDP pose queue flood. Open: seed local PC/pawn cache so host pos≠0 and Follow bots reach camera. |
| 2026-06-30 | harness fix session | 创建记录；记录连接/注入修复与进关失败尝试 |
| 2026-07-02 | harness 1号机 | AttachCore defers EnsureGameplayHooks; client TCP timeouts + hosted harness snapshot poll; playthrough inject/connection harness fixes — **connected_at_menu + 2 remotes OK**; level entry still fails (KI open) |
| 2026-07-02 | harness fix | `ENSURE_GAMEPLAY_HOOKS` / `ENSURE_MP_HOOKS` core pipe + playthrough pre-level hook wait; `HarnessUi` keeps previous-frame targets for `GET_UI_TARGETS` race; borderless scenario waits for `core_ready` + `APPLY_WINDOW_LAYOUT` nudge |
| 2026-07-04 | deep crash fix | `SyncCurrentLevelFromWorld` skips `GetWorld` on multiplayer background threads — prevents UE3 access violation from listener/status threads calling engine functions non-thread-safe |
| 2026-07-04 | isolation hardening | Plugin load queue is async-only; gameplay/render/input callbacks remove crashed handlers; multiplayer listener/status/UDP threads log SEH faults and disable network work |
| 2026-07-04 | logging fix | Restored multiplayer `MpDebugLog` and mirrored key client connection events to `ModLog` / `session.log` |
| 2026-07-04 | main-menu crash regression | Removed eager gameplay/world refresh from Multiplayer load/connect paths; defer world/pawn access to explicit level/tick paths only |
| 2026-07-04 | level sync after menu connect | Multiplayer now registers level-load callbacks during plugin initialize so entering a level after connecting at main menu can notify the server without eager World/Pawn scanning |
| 2026-07-04 | level hook install/logging | Reverted unsafe multiplayer-triggered gameplay hook install after in-game crash; replaced level sync with queued main-thread SEH probe and kept server level diagnostics |
| 2026-07-06 | spawn queue crash fix | Fixed cross-DLL `SpawnCharacter` queue lifetime bug that wrote into `SpawnCharacterWrapper`'s expired local stack slot; build/deploy PASS, manual two-machine retest pending |
| 2026-07-06 | heartbeat timeout fix | Fixed two-machine timeout after second client joins: client receive no longer blocks heartbeat sends; server uses 45s timeout, TCP write deadlines, idempotent disconnect cleanup, and closes timed-out TCP connections |
| 2026-07-06 | Faith remote visual mitigation | Default Faith remote spawn now uses Kate visual assets in multiplayer; material nulls are skipped, player packet writes are serialized, and bone copy bounds are validated |
| 2026-07-06 | spawn crash diagnostics | Added durable host-side spawn stage logs and multiplayer client logs; enabled WER LocalDumps on the test host after `0x00014d54` fastfail reproduced |
| 2026-07-06 | pre-spawn deferral | Remote level and character messages no longer queue spawn work from the TCP listener; host `Set Gameplay` installs presentation hooks and tick-time logic owns spawn retries |
| 2026-07-06 | Set Gameplay race fix | `ApplyManualClientLevelOnMainThread` skips redundant init when level-load hooks already updated `client.Level` to a real map — prevents activation reset/corruption and delayed tick-time crash after manual Set Gameplay during campaign level load (guard does NOT trigger — hooks don't fire; superseded by next entry) |
| 2026-07-06 | Bone transform fix | `OnBonesTick` skips `TransformBones` for bots with no real UDP data (`p->ToTime == 0`). The Kate case in `TransformBones` reads `src[45..107]` from a 75-atom Faith bone array, overflowing into garbage heap memory and corrupting Kate's skeletal mesh. This caused `0xc0000409` fastfail in `engine.dll`.
|| 2026-07-06 | Render hook crash fix | Removed `EnsureClientRenderHook()` from `EnsureClientRemotePlayerPresentation()` — concurrent `push_back` into unsynchronized `renderScene.Callbacks` vector during D3D9 render thread iteration caused `0xc0000409` fastfail on both machines. `OnRender` is already registered at startup; the redundant call during level transition is unsafe. Verified fix on both 1号机 and 2号机 — no crash, map name updates, remote players visible.

| 2026-07-13 | RDP regression batch | ~20 automated bot-spawn test runs all **FAILED** with RDP block. Game window titled "Message", RDP detection dialog before CreateDevice. Root cause: [KI-2026-011](KI-2026-011-rdp-block-creatdevice.md). Do NOT retry spawn/engine fixes from this batch without verifying machine is NOT on RDP.
| 2026-07-13 | Engine GS cookie investigation | Subagent found **no definitive stack buffer overflow** in engine sources. All buffers bounded. Closest suspect: sprintf_s template in DurableSpawnLogf (engine_hooks_gameplay.cpp:69). Recommend explicit snprintf. GS crash likely from RDP-induced early exit, not genuine overflow.
| 2026-07-13 | d3d9 CreateDevice hook confirmed correct | Subagent verified: vtable index, calling convention, argument forwarding all correct. ScheduleManagerPreload() does NOT deadlock. Root cause: game never calls CreateDevice due to RDP detection — see [KI-2026-011](KI-2026-011-rdp-block-creatdevice.md).
| 2026-07-13 | Game window enumeration | Confirmed Class=#32770 Title="Message" with child text "This game cannot run with 3D graphics over a Remote Desktop connection." Standard Windows message box blocks thread message pump, preventing D3D init. See [KI-2026-011](KI-2026-011-rdp-block-creatdevice.md). |
| 2026-07-18 | Set Gameplay false alarm | Manual Set Gameplay activation chain verified OK in `%TEMP%\mirroredge-multiplayer-client.log` (`activation set live`). User "stuck" + `MmodDrainSpawnQueue queue=0` was **`listSize=0` (no remotes)**, not activation hang. Do not re-debug Set Gameplay without reading client.log. Runbook: [mp-set-gameplay-runbook.md](../mp-set-gameplay-runbook.md). |
| 2026-07-18 | GObjects warm on Set Gameplay / Tick | `GetWorld(true)` / `GetPlayerPawn(true)` on activation, EndScene, or first TickHook body caused freeze/crash (`GetPC.iterate.end`). Prefer cache-only + `MeSdk::Safe::Gui::TryFindTdGameEngine` → GamePlayers chain. Do not retry full GObjects warm on Set Gameplay. |
| 2026-07-19 | Level-entry hitch | Stopped MP plugin-init `EnsureGameplayHooks` (defer to Set Gameplay). `LevelLoadHook` no longer holds `spawns.Mutex` across LoadMap. `OnPreLevelLoad` nulls remote actors instead of `Despawn`. Client log had `pre level load tutorial_p` with no `post level load`. |
| 2026-07-19 | Post-bot hitch (~0.4 FPS) | MP poses left `OnActorTick` (O(world actors)); PE/Actor/Bones atomic empty fast-path; no force `bUpdateSkelWhenNotRendered`; BonesTick skips when no remote bone motion. |
| 2026-07-19 | Product **v1.2.6** | Bundles level-entry + post-bot hitch + `bot.ps1` Follow-default / high-|pos| UDP / ASCII-only strings. Docs: CHANGELOG, troubleshooting, mp-set-gameplay-runbook. |