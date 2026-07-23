# Changelog

All notable changes to **Mirror's Edge Module Launcher** are documented here.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).  
Version numbers follow [Semantic Versioning](https://semver.org/).  
Authoritative version: [`version.json`](version.json) (synced to `shared/product_version.h` at build).

## [Unreleased]

## [1.2.13] - 2026-07-23

### Added

- **Fix PhysX local + official download** — resolve order: complete `physx\` pack → NVIDIA PhysX install → game `Binaries`; if none complete, download/launch official PhysX 9.23.1019 System Software, then Fix again.

### Changed

- **KI-2026-010** status → mitigated (console keyboard fix pending manual confirm); **KI-2026-008** index → resolved.

## [1.2.12] - 2026-07-23

### Added

- **Launcher Patch / modules sync** - Update tab: detect modules vs game, update modules, Patch (`modules\` + `d3d9`), Fix PhysX (overlay from `physx\` with `.mmphysx.bak`). Launch prep now syncs the full modules tree.
- **`physx/` pack convention** - optional overlay directory documented in `physx/README.md` (DLLs not shipped in-repo).

### Changed

- **Release zip** - runtime-only pack (no `.pdb`/`.exp`/`.lib`, no `ModuleLauncher.bat` / alias exe). `physx\` included only when it contains DLLs.
- **Build/deploy** - no longer creates `mirroredge-module-launcher.exe` or `ModuleLauncher.bat`; strips MSVC side-products from `dist/`.

### Fixed

- **Modules sync Bad Image** - do not `LoadLibrary` non-DLL build artifacts (e.g. `core.exp` → `0xc000012f`).

## [1.2.11] - 2026-07-23

### Added

- **UDP Seq + reorder** — pose packets may carry a 2-byte `Seq` trailer (692 B); receivers buffer up to 4 out-of-order packets before From/To apply. Legacy 676–690 peers still work. Server opaque-forwards `676–692` (no dual push+pull).
- **Host TX rate cap** — `client.hostPoseTxMaxHz` (default 60; 0 = unlimited). Idle floor ~15 Hz; parkour / movement-state changes bypass. Multiplayer UI slider.
- **LAN dual soak script** — `tools/mp-lan-dual-soak.ps1` (`-Role host|client`) for 1号机/2号机 coordination; see [`docs/test-environments.md`](docs/test-environments.md).

### Fixed

- **KI-2026-005 playthrough harness** — `mp-playthrough-bots` delegates to the verified `mp-real-level-bots` `START_NEW_GAME` path (no Enter-only / pre-level hooks).

### Changed

- **CI Release** — tag `v1.2.11` builds the auto-update zip on Actions (DXSDK vendor + UTF-8 `release.yml`).

## [1.2.10] - 2026-07-23

### Added

- **Launcher config sync status** — Display tab compares UI settings to `TdEngine.ini` / `module_manager.settings.ini` and offers **Apply config**; Launch tab shows whether `Binaries\d3d9.dll` and `modules\module_manager\module_manager.dll` are present (refreshes on edit, tab change, and a 2s timer).

### Fixed

- **`settings.json` location** — launcher always reads/writes `settings.json` next to `ModuleLauncher.exe` (no longer redirects saves to a separate game-root copy when browsing for a path).
- **Skip intro movies** — clearing only `-StartupMovies` left stock `StartupMovies=StartupMovie` active; now clears the positive key and renames `TdGame\Movies\StartupMovie.bik` to `.mmskip` (restores on uncheck).

## [1.2.9] - 2026-07-23

### Added

- **Launcher feature tabs** — Win32 tab control groups settings into Launch / Display / Update pages; status, language, log, and launch/close buttons stay always visible.

## [1.2.8] - 2026-07-23

### Added

- **Launcher Chinese / English UI** — top-right language combo switches all launcher chrome and log strings instantly; persists as `launcher.uiLanguage` (`zh` / `en`) in `settings.json`. Default follows Windows UI language when unset.

## [1.2.7] - 2026-07-23

### Added

- **Launcher GitHub auto-update** — startup / **检查更新** queries Latest Release; **立即升级** downloads `mirroredge-module-launcher-<semver>-win32.zip`, extracts, replaces the install directory via a post-exit bat, and relaunches. Settings: `launcher.skipUpdateCheck`, `launcher.dismissedUpdateVersion`. Pack: `tools/pack-release.ps1`; CI: `.github/workflows/release.yml` on tag `v*`. See [`docs/build-deploy.md`](docs/build-deploy.md).

## [1.2.6] - 2026-07-19

### Fixed

- **Pre-bot host pose cold start** — TdEngine incremental warm now runs as soon as gameplay hooks are ready (not only after `FORCE_HOSTED_LIVE`); quiet window 2s→750ms; warm slice 800→1100/500ms (still not the banned 2000/200). Harness nudges Space/Shift/W while waiting. Cuts typical ~50s waits that occasionally hit the 70s fail line.
- **UDP pose never applied (posed=0)** — Go server UDP recv buffer must exceed packet size (Windows `WSAEMSGSIZE`); buffer 2048. Harness auto-deploy prefers `dist/.../multiplayer-server.exe` over a stale `mods/.../server` copy.
- **Level-entry hitch** — multiplayer no longer installs gameplay hooks at plugin init (wraps LoadMap / white-frame). Hooks install from **Set Gameplay**; `LevelLoadHook` does not hold `spawns.Mutex` across LoadMap; `OnPreLevelLoad` nulls remote actors instead of `Despawn`.
- **Post-bot hitch (~0.4 FPS)** — remote location/yaw applied once per network tick (not `OnActorTick` for every world actor); ProcessEvent/ActorTick/BonesTick use lock-free empty fast-paths; remote meshes no longer force `bUpdateSkelWhenNotRendered`.
- **bot.ps1 far from player** — Follow defaults **on** (use `-NoFollow` for demo orbit); UDP follow prefers highest-|pos| host; auto-reads `%TEMP%\mirroredge-bot-target.json`; PS 5.1 ASCII-only strings (no em-dash in double-quotes).
- **Soft-freeze after bots disconnect (KI-2026-012)** — peer leave nulls Actor + chat; no `ShutDown` / park writes after `TransformBones` (even `TryWriteActorLocation` hung). Orphans until level unload. Harness post-bot hung gate. See [`docs/known-issues/KI-2026-012-soft-freeze-after-bot-despawn.md`](docs/known-issues/KI-2026-012-soft-freeze-after-bot-despawn.md).
- **`currentMap` stuck on `gameplay`** — after Set Gameplay / FORCE, host upgrades synthetic `gameplay` to the real StreamingLevels/`GetMapName` (e.g. `tutorial_p`); server treats `gameplay` as compatible with concrete maps for UDP/interact.
- **Host pose cold until bots** — empty-queue EndScene may WorldInfo-warm only (never idle `TryWarmTdGameEngineIncremental`).
- **UDP motion idle cost** — skip Mesh3p sample / throttle sends when nearly still; keep lastGoodBones so remotes do not collapse.
- **Default remote rest** — join seeds Faith `defaultBones` and enables TransformBones immediately (Kate/Miller remap before first UDP bones).

### Added

- **Mesh visual V10–V20** — walk bone EMA; MEBC v2 clips; bot phase-align + crossfade; live-UDP stance xfade + hysteresis; soft corridor; stale UDP bone settle; **adaptive interp delay** (V18–V19 per-remote EMA + jitter; **V20** faded velocity coast + peer-EMA stale horizon + asymmetric delay EMA); TransformBones only (no AnimTree / remote PHYS / TdPawn).
- **Harness auto-deploy** — `mp-real-level-bots.ps1` copies `dist/modules/{engine,multiplayer}` (+ server) into Steam GameRoot before launch; fails closed if `module_manager` missing.
- **Soft collision (fake push)** — Multiplayer tab checkbox; XY-separates remotes from local pawn on the live pose path and lightly nudges Faith. No UE physics; does not mutate remotes on disconnect (KI-2026-012). Settings: `client.softCollision` / `Radius` / `Strength`. Auto: `mp-real-level-bots.ps1` SoftProbe + `soft collision engaged` log (use `-SkipSoftCollisionProbe` to opt out).
- **Near-distance interact (B1)** — Press **E** (or Interact Keybind) near a remote to send TCP `interact` (`wave`); chat toast only — no remote Actor writes (KI-2026-012). Settings: `client.interactKeybind` / `interactMaxMeters`.
- **Tag server (B0)** — Go server handles `startTagGameMode` / `endGameMode` / `tagged` / `canTag` / proximity retag from UDP pose; client Tag tick uses `ResolveLocalPlayerPawn` (no bare `GetPlayerPawn`); `announce` messages show in chat.
- **SoftProbe Tag/Interact auto** — `mp-real-level-bots.ps1` SoftProbe uses `bot.ps1 -StartTag -SendInteract`; interact targets nearest UDP peer (≤~3.5 m); motionPass asserts `tag mode live`/`tagged id=` and `interact recv|sent` (same opt-out as soft collision: `-SkipSoftCollisionProbe`). Verified PASS 2026-07-20 evening (`motion phase1-8 + softColl + Tag + Interact`).
- **Host physics telemetry** — `phys transition` / `fall start` / `fall land` / `wall hit` from local Faith `TryReadPawnPose` (observe only).
- **Remote world clamp** — Host-relative geometric floor/wall snap before writing remote locations (`client.worldClamp*`). SoftProbe `-PhysicsFallDrop` / `-PhysicsWallSlam`; motionPass needs `world clamp floor` + `world clamp wall` (opt-out `-SkipPhysicsProbe`). Does **not** enable remote PHYS / bCollideWorld. **Failed:** ProcessEvent `Actor::Trace` on live pose path (spawn drain hang).
- **Pose smooth** — Snap From→To jumps beyond `poseSnapUu` (default 350); EMA Location/Yaw write (`client.poseSmooth` / `poseSmoothAlpha`) to cut soft-coll + UDP jitter. Does not open AnimTree.
- **True-motion TX (B3-lite)** — UDP packet 690: `MovementState` + `Physics` after Velocity. Host forces Mesh3p sample + full-rate send when `MovementState >= Falling` or state changes (`client: move state from=…`). Remotes store state for logs; still `TransformBones` only — **no** remote `SetMove`/AnimTree (KI-012).

### Changed

- Recommended play order: enter Story → inject multiplayer → Set Gameplay → start bots. See [`docs/troubleshooting.md`](docs/troubleshooting.md) and [`docs/mp-set-gameplay-runbook.md`](docs/mp-set-gameplay-runbook.md).
- Motion phases 1–8 (host bone dump/cycle, velocity, push-relay, live bone stream) documented in the Set Gameplay runbook.
- **Tag / Interact technical notes** — runbook §9 (TCP message table, UDP 1.3 m retag, interact JSON, settings, KI-012 constraints); troubleshooting cross-link.
- **Removed from roadmap** — full remote AnimTree/`SetMove`, remote `PHYS_Falling`/`bCollideWorld`, and `client.remoteTdPawn` (deleted; see KI-2026-013). Mesh puppets only.
## [1.2.5] - 2026-07-19

### Fixed

- **EndScene stutter** — pending spawn drain no longer floods the in-game console / `CreateFile` every tick (`MmodDrainSpawnQueue` empty-queue early-out; rate-limited queue/retry logs; removed per-call `spawn_queue_trace` + `QueueMainThreadTask` console spam).
- **MP bot visibility** — Cam1/Cam5 skeletal meshes at distinct near-camera stand-offs on `tutorial_p` (Follow ignores zero host UDP; bot `-Level gameplay` + StartX/Y/Z; EndScene skips `GamePlayers` PC seed while draining).

### Changed

- Spawn diagnostics: file/console traces only on progress or low-rate samples (see [`docs/mp-set-gameplay-runbook.md`](docs/mp-set-gameplay-runbook.md)).

## [1.2.4] - 2026-07-01

### Changed

- **Load FSM** — `LoadLibrary` / `PluginInitialize` run **without** holding `g_mutex` so `GET_STATUS` / `GET_LOG` stay responsive during plugin load.
- **Harness** — `mod-deps` tests core auto-load guard + multiplayer with core present; `multiplayer-functional` / `mp-gui-test` use `Invoke-EnsureCoreLoaded` (no manual `INJECT core`).

### Fixed

- Harness `mod-deps` / `multiplayer-functional` updated for core auto-load (no longer expect inject-without-core).
- `mp-gui-test` waits for Multiplayer tab registration; pipe inject instead of flaky UI-only path.

## [1.2.3] - 2026-07-01

### Changed

- **Control pipe** — `PING` / `GET_STATUS` / `GET_UI_TARGETS` / `LIST_MODULES` / `GET_LOG` handled on the pipe thread (no pump-queue wait); pump thread starts before pipe accept.
- **Overlay draw** — skip ImGui `EndScene` draw when menu, console, and plugins are all idle (avoids drawing during intro cinematics); ImGui still initializes for `overlayReady`.
- **Harness pre-intro** — `Invoke-SafePreIntroPluginTeardown` uses full unload path with **2.5s** per-plugin settle + **2s** post settle; boot accepts `imgui initialized` log when pipe `overlayReady` lags.

## [1.2.2] - 2026-07-01

### Changed

- **Boot wait compression** — proxy settle **300ms**, ImGui **6**/draw **20** stable frames, pre-hook bootstrap **8ms**, hook retry **100ms**; harness immediate Enter nudge + **6s** repeat (stops once hooks seen), **400ms** status poll, **NDJSON log fast-path** when pipe returns `ERR` ([`docs/module-manager.md`](docs/module-manager.md)).

## [1.2.1] - 2026-07-01

### Changed

- **Load performance (round 2)** — `QueueLoadModule` drains load FSM immediately (4× burst); proxy hook path uses **500ms** settle + **200ms** retry (skips 12s lazy delay); pump **8ms** / burst **12** phases; core hosted init polls at **16ms** (no fixed pre-sleep); ImGui init after **10** stable frames, draw after **30** ([`docs/module-manager.md`](docs/module-manager.md)).

### Fixed

- **Overlay readiness** — zero-delay proxy hook could leave `hooksInstalled=true` but `overlayReady=false`; proxy settle + early `EnsureImGui` restores reliable overlay init.
- **Harness `mod-manager-config`** — Modules inject UI: inject buttons register via `RecordRect` (off-screen rows in `BeginChild`); harness opens Modules tab and scrolls before polling (no premature `Test-ModMenuTabSuite` MinHits check).

## [1.2.0] - 2026-06-29

### Added

- **Cross-environment diagnostics** — `diagnostics.enabled` in `settings.json`, `MMOD_DIAGNOSTICS=1`, durable `logs/<session>/session.log` + `session.ndjson`, `tools/collect-diagnostics.ps1` zip bundle ([docs/diagnostics-logging.md](docs/diagnostics-logging.md)).
- **Product versioning** — `version.json`, `CHANGELOG.md`, launcher title shows version, deploy copies `VERSION.json`.
- **Load performance** — conditional d3d9 wait (proxy path), burst mod load FSM (up to 8 phases/tick), faster launcher/core polls, pump thread 16ms.

### Changed

- Feature mod id/path unified on **`multiplayer`** (removed `mp-client` harness aliases and legacy naming).
- Harness **`mod-manager-config`**: L2 pass = boot phase; menu phase failures are WARN only.
- `build.ps1` cleans `modules/` tree before build and on deploy.
- Docs: load performance notes in `docs/module-manager.md`.

### Fixed

- Multiplayer / trainer **unload crashes** — synchronous tab removal, interruptible connect, listener join, `ClearFeaturePluginCallbacks` on plugin unload (API v2).
- Trainer re-inject crash after unload — stale engine callbacks cleared when any plugin unloads.
- Harness pre-intro teardown can unload trainer/multiplayer/dolly when unload path is stable.

## [1.1.0] - 2026-06

### Added

- **module_manager** overlay host loaded via d3d9 proxy (no remote inject / no admin).
- **core** + **engine** hosted plugins; feature mods (`multiplayer`, `trainer`, `dolly`) inject from Module Manager UI.
- `settings.json` at game root (`launcher.gameRoot`, `mods.autoLoad`, display/borderless).
- AI debug harness + NDJSON session logs (`docs/ai-debug-harness.md`).
- Known-issues registry workflow (`docs/known-issues/`).

### Changed

- Default deploy: `ModuleLauncher.exe` + `Binaries\d3d9.dll` proxy; launcher waits for `module_manager_ready`.
- Monolith `mmultiplayer.dll` moved to `legacy/mmultiplayer/` (not in solution).

## [1.0.0] - 2026-05

### Added

- Initial **Module Launcher** with d3d9 graphics proxy and in-game mod menu.
- Win32/x86-only support for Mirror's Edge 1.0 (Steam/retail).
- Mod log relay via named pipe to launcher UI.
