# AI Debug Harness

Runtime debug framework for AI-assisted troubleshooting of mirroredge-module-launcher.

## Components

| Piece | Path | Role |
|-------|------|------|
| NDJSON log | `%TEMP%\mirroredge-debug\<session>.ndjson` | Structured probe output from runtime modules / launcher proxy |
| Session manifest | `%TEMP%\mirroredge-debug\last-session.json` | Latest session id + log path |
| Harness | `tools/debug-harness/` | PowerShell scenarios + assertions |
| MCP server | `tools/mcp-debug-server/` | Cursor tools for log/control/scenarios |

## One-time setup

**New test machine** (after `deploy.config.json` with `testMachine`):

```powershell
.\tools\debug-harness\setup-test-machine.ps1
```

**Existing machine** (MCP / merge driver / hooks only):

```powershell
.\tools\debug-harness\setup.ps1
```

Both run `npm install`, configure local git (`merge.test-logs-index`, `core.hooksPath=.githooks`), and write `.cursor/mcp.json`. Reload Cursor afterward. Full multi-machine checklist: [test-environments.md](test-environments.md).

## Environment variables

| Variable | Set by | Purpose |
|----------|--------|---------|
| `MMOD_DEBUG_SESSION` | Harness / MCP | Session id in NDJSON |
| `MMOD_DEBUG_LOG` | Harness (optional) | Override log file path |
| `MMOD_SESSION_LOG` | Harness / launcher diagnostics | Human-readable `session.log` path |
| `MMOD_DIAGNOSTICS` | User / `settings.json` | Set `1` to enable cross-environment logging (see [diagnostics-logging.md](diagnostics-logging.md)) |
| `MMOD_DEBUG_SKIP_VISUAL` | Harness (optional) | Set `1` to skip PrintWindow captures (pipe/state checks still run) |

Set these **before** starting `ModuleLauncher.exe` so the launcher and game child process inherit them.

## Temp cleanup (screenshots / logs)

`tools/debug-harness/tools/clear-harness-temp.ps1` prunes `%TEMP%\mirroredge-debug`, `mirroredge-reflections`, `mirroredge-freeze`, plus stale `*.bak` / `mp-real-level-bots-run-*.log`. Defaults: retain **2 days**, keep newest **40** `real-level-bots` shots and **20** reflection dirs, always drop empty session folders.

Runs automatically at the start of `tools/debug-harness/run.ps1` and `tools/mp-real-level-bots.ps1`. Manual:

```powershell
.\tools\debug-harness\tools\clear-harness-temp.ps1
.\tools\debug-harness\tools\clear-harness-temp.ps1 -RetainDays 1 -KeepNewestShots 20
```

## Result format

Every scenario run outputs a single JSON result line to stdout, machine-parseable:

```
harness-result: {"scenario":"verify-harness","layer":"L0","pass":true,"durationMs":814}
```

Parse with: `... | Select-String '^harness-result: ' | ForEach-Object { $_.Line -replace '^harness-result: ','' } | ConvertFrom-Json`

Fields: `scenario`, `layer`, `pass`, `durationMs` (milliseconds), `error` (present only on failure).

## Scenario layers

| Layer | Meaning | Examples |
|-------|---------|----------|
| **L0** | Self-test; no external deps | `verify-harness` |
| **L1** | Launcher-only; no game required | `ui-launcher` |
| **L2** | Full game session via control pipe (automation hook) | `smoke-split`, `inject-mp`, `mod-full` |
| **L3** | Full game session via `SendInput` (user fidelity) | `user-flow` |

## Game scenario launch path

`Start-SplitInjectionSession` (used by `smoke-split`, `inject-mp`, `ui-module-manager`, …):

1. `Initialize-DebugSession` — sets `MMOD_DEBUG_SESSION` / `MMOD_DEBUG_LOG`, writes manifest
2. `Invoke-DebugBuild` — `build.ps1 -NoDeploy` + `Deploy-ModuleDlls` (d3d9 proxy, module_manager, …)
3. `ModuleLauncher.exe /auto` with debug env — `PrepareGameEnvironment()` + `ConfigIntegrityBypass::BeginWatching()`
4. Harness clicks **Launch Game** — `LaunchGameExecutable()` (config integrity bypass applied to game PID)
5. Waits for game window, `Local\module_manager_ready`, control pipe `PING`

During waits the harness polls **MirrorsEdge.exe**, **ModuleLauncher.exe**, and **desktop `#32770` message boxes** (`Get-HarnessBlockingDialogs` / `Assert-NoBlockingGameDialogs`) for modals such as `Default*.ini corrupt`, SecuROM disc checks, UE3 fatals, **MSVC CRT abort**, and launcher path/deploy errors. The background **hang watchdog** uses the same scanner after its grace period. Failures include process id, source (`process` vs `desktop`), window class, title, body text, and remediation hints — not a generic timeout.

**Limits:** unrelated desktop message boxes (other apps) are ignored unless text matches a known error rule or references Mirror's Edge / ModuleLauncher. Dialog text must be readable via Win32 `EnumChildWindows`. WER crash (Event 1000) is detected separately via `Assert-NoRecentMirrorEdgeCrash`.

Do **not** start `MirrorsEdge.exe` directly from harness; proxy deploy and config-integrity bypass rely on the launcher process.

## Scenarios

### L0 — Self-test

| Scenario | What it checks |
|----------|----------------|
| `verify-harness` | Harness toolchain: log tail, sequence assert, session manifest round-trip, **visual primitives** (bitmap stats + delta) |

### L1 — Launcher UI (no game)

| Scenario | What it checks |
|----------|----------------|
| `ui-launcher` | Launcher Win32 dialog: controls exist, Close enabled, WM_CLOSE exits |

### L2 — Game session via control pipe (automation hook)

These scenarios drive the game through named-pipe commands (`MENU_OPEN`, `INJECT`, `SET_MOD`, `GET_STATUS`).
They are fast and deterministic but do not test the real user input path.

| Scenario | What it checks |
|----------|----------------|
| `smoke-split` | Build+deploy, launcher `/auto` → Launch Game (config integrity bypass), module_manager hooks, **boot progress** (does not stop at static splash) |
| `borderless-window` | Writes `Scale` + matching `ResX`/`ResY` to settings + `TdEngine.ini`, Win32 assert borderless window |
| `inject-mp` | smoke-split + core auto-load wait + `engine.modReady` + **boot progress** |
| `mod-full` | Launcher UI + manager UI + core auto-load + pipe/status/viewport checks |
| `mp-functional` | core: SDK + presentation + **gameplayHooks**; `LIST_MODS`/`SET_MOD` **deprecated** in split mode; `RELOAD_SETTINGS` |
| `mp-playthrough-bots` | Local `multiplayer-server`, core + multiplayer inject, intro blind skip, menu→level navigation, follower bots, WASD movement; NDJSON **interaction log** (`*-interactions.ndjson`). **Known gap (2026-06):** `inGameplay` / `currentMap` often empty at main menu in hosted split mode — see [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md). |
| `mp-gui-test` | core + multiplayer ImGui: inject, Modules tab, Multiplayer settings tab (`GET_UI_TARGETS`), menu close |
| `multiplayer-functional` | Inject core + multiplayer; client suite |
| `trainer-functional` | Inject core + trainer |
| `mod-manager-config` | **L2 pass = boot phase:** `LIST_MODULES`, core `SET`/`RELOAD_SETTINGS`, Modules inject UI (`manager/inject/multiplayer`), trainer inject/unload + settings, multiplayer settings UI; `Invoke-SafePreIntroPluginTeardown` unloads trainer/multiplayer/dolly before intro when safe. **Menu phase (optional):** Engine tab at tdmainmenu — WARN on failure if multiplayer still loaded, else boot-only pass counts (`bootPass` / `menuPass`). |
| `alt-tab-menu` | **KI-2026-002:** menu open → Alt+Tab away/back → `device_recovered` or responsive render |
| `ime-roundtrip` | **KI-2026-003:** menu open → Alt+Tab to Notepad → type sample (`KI3`) → manager pipe alive |
| `dolly-functional` | Inject core + dolly |
| `ui-module-manager` | Module Manager overlay via pipe: menu/tabs/console (+ **PrintWindow** pixel asserts when visual enabled) |
| `visual-test` | Primitives self-test + full overlay visual regression (`menu_open` / `console_open` deltas vs closed) |
| `ui-test` | `ui-launcher` + `ui-module-manager` (*use `-LauncherOnly` for L1-only) |

### L3 — Game session via SendInput (user fidelity)

These scenarios use `keybd_event` / `SendInput` to simulate real keyboard and mouse actions.
They verify the same code paths a human player exercises.

| Scenario | What it checks |
|----------|----------------|
| `user-flow` | Main menu smoke: Insert menu, grave console inject, WASD, focus round-trip (game left running) |
| `user-full-session` | Full L3 lifecycle: real mouse launcher launch → intro skip → console inject → menu UI → optional level → Alt+F4 + launcher close |

### Meta / gate

| Scenario | Layer | What it checks |
|----------|-------|----------------|
| `auto-loop` | — | Retry loop over scenarios; failure bundles; optional `-RebuildOnFail` |
| `ci-gate` | — | L0+L1 only: no-game gate suitable for pre-commit / CI |

```powershell
.\tools\debug-harness\run.ps1 verify-harness
.\tools\debug-harness\run.ps1 ui-launcher
.\tools\debug-harness\run.ps1 ui-test -LauncherOnly
.\tools\debug-harness\run.ps1 ui-test
.\tools\debug-harness\run.ps1 smoke-split
.\tools\debug-harness\run.ps1 borderless-window
.\tools\debug-harness\run.ps1 inject-mp
.\tools\debug-harness\run.ps1 mp-functional
.\tools\debug-harness\run.ps1 mp-playthrough-bots
.\tools\debug-harness\run.ps1 mp-gui-test
.\tools\debug-harness\run.ps1 trainer-functional
.\tools\debug-harness\run.ps1 mod-manager-config
.\tools\debug-harness\run.ps1 visual-test
.\tools\debug-harness\run.ps1 user-flow
.\tools\debug-harness\run.ps1 user-full-session
.\tools\debug-harness\run.ps1 user-full-session -EnterLevel -PlaySeconds 20
.\tools\debug-harness\run.ps1 auto-loop
.\tools\debug-harness\run.ps1 auto-loop -Scenarios verify-harness,ui-launcher -MaxRetries 1
.\tools\debug-harness\run.ps1 smoke-split -SkipLaunch   # build + session only
.\tools\debug-harness\run.ps1 ci-gate    # L0+L1, no game, pre-commit
.\tools\debug-harness\run-all-scenarios.ps1   # all 19 scenarios, cold stop between game sessions
.\tools\debug-harness\run-ki-regression.ps1  # KI-002 Alt+Tab + KI-003 IME (2 scenarios)
```

`run-all-scenarios.ps1` behavior:

| Rule | Detail |
|------|--------|
| **Build** | First **game** scenario (`smoke-split`) runs full `build.ps1` + deploy; later game scenarios use `-SkipBuild` |
| **Zombie guard** | Zombie `MirrorsEdge` EPROCESS (0 threads) **blocks** the suite; remaining scenarios are `skipped:` not re-run |
| **Retries** | Each scenario retries once; zombie after failure also blocks the rest |

After a full suite, results are published to [`test-logs/`](../test-logs/README.md) under `machines/<testMachine>/` (history + CHANGELOG aligned to git commits) and **pushed by default** so peer test machines can `git pull` and see pass/fail per environment. Set `MMOD_HARNESS_LOG_PUSH=0` to skip push. Manual publish: `.\tools\debug-harness\publish-test-log.ps1`.

When two machines push harness results close together, `test-logs/index.json` may conflict on `git pull`. **Keep both machine entries** (accept both); `updatedAt` = latest `finishedAt`. `CHANGELOG.md` uses the same accept-both merge (newest entry first). `Publish-HarnessTestLog` / `Push-HarnessTestLog` pre-merge with `origin` via `Sync-HarnessTestLogWithRemote`; on push failure they retry with auto-merge. Git merge driver: run `setup.ps1` once (configures `merge.test-logs-index` for both files). Manual index merge: `merge-test-logs-index.ps1`. Regression: `verify-harness` → `Test-HarnessTestLogMerge`. Status: `show-test-logs-status.ps1`. Suite start: `Initialize-HarnessTestLogGit` (in `run-all-scenarios.ps1`). `index.json` is **generated** from `machines/*/latest.json` (`Rebuild-HarnessTestLogIndex`). CI: `validate-test-logs.ps1` / `.github/workflows/test-logs.yml`. MCP: `debug_get_harness_test_status`. Details: [`test-logs/README.md`](../test-logs/README.md).

Requires `deploy.config.json` with `deployPath` pointing at the game root (e.g. `F:\EA Games\Mirrors Edge`).

### Coverage gaps (known blind spots)

| Area | Gap / mitigation |
|------|------------------|
| **KI-002 / KI-003** | `alt-tab-menu`, `ime-roundtrip`; bundle: `run-ki-regression.ps1` (2号机建议每次大改后跑) |
| **L2 input** | Most tests use control pipe; only `user-flow` / `user-full-session` use `SendInput` |
| **SET_MOD** | Split mode returns `ERR deprecated`; feature mods use Modules tab `INJECT` |
| **Playthrough** | `inGameplay` / `currentMap` often empty at menu — see [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md); `Test-HarnessPlaythroughInLevel` no longer treats `gameplayHooks` alone as in-level |
| **Hang guard** | `Wait-CoreReady` monitors render stall during core bootstrap (`Assert-CoreBootstrapProgress`, 45s threshold). `smoke-split` / `inject-mp` also run `Assert-GameBootProgress` so a static startup splash after hooks/core ready fails with a hang bundle. See [KI-2026-008](known-issues/KI-2026-008-startup-splash-harness-gap.md). |
| **SDK binary scan** | `Invoke-DebugBuild` automatically runs `Assert-SdkBinaryInstructionScan` after every build+deploy — covers **all game scenarios** (smoke-split, inject-mp, mp-functional, mod-full, visual-test, etc.). Pure-instruction opcode scan against committed `sdk-reference-lite.json`. Failures are **non-fatal warnings** (binary may have changed intentionally). Also covered in CI (`sdk-verify` workflow) and `build.ps1 -VerifySdk` (fatal in CI). See [sdk-verification.md](sdk-verification.md). |
| **Multi-machine** | No automated two-client sync test; use `test-logs/` for cross-PC status |

## Visual regression (PrintWindow)

Overlay UI scenarios (`ui-module-manager`, `mod-full`, `visual-test`, …) capture PNG frames via `PrintWindow` + sample pixel statistics (mean luminance, variance, non-black ratio). Asserts are **delta-based** for menu/console toggles; all other game stages use **sanity checks** (non-black, minimum size).

**Automatic milestones** — `Write-HarnessInteraction` triggers a capture for whitelisted `phase|action` pairs (e.g. `intro|main_menu_ready`, `session|in_level`, `inject|module_ready`, `movement|session_end`). Additional hooks run at:

| Hook | Step label |
|------|------------|
| `Wait-ManagerHooksReady` | `hooks_ready` |
| `Wait-ManagerModuleLoaded` | `module_loaded_<id>` |
| `Wait-CoreReady` | `core_ready_event`, `core_ready_pipe` |
| `Wait-ModuleManagerLoadLog` | `module_load_log_<id>` |
| `Wait-GameWindowLayout` | `borderless_layout_ok` |
| `Open-ModuleManagerMenuGui` | `menu_open_gui` |
| `Test-ModMenuTabSuite` (per tab) | `tab_<name>` |
| Functional suites (end) | `suite_*_complete` |

| Artifact | Path |
|----------|------|
| PNG captures | `%TEMP%\mirroredge-debug\<sessionId>-visual\*.png` |
| Frame manifest | `%TEMP%\mirroredge-debug\<sessionId>-visual\manifest.json` |
| Thresholds | `tools/debug-harness/visual-baselines/thresholds.json` |

Interaction log entries use phase `visual`, action `capture`. Disable captures with `$env:MMOD_DEBUG_SKIP_VISUAL = "1"` or `-SkipVisual` on `visual-test` / `Test-ModuleManagerOverlayUi`.

`verify-harness` (L0) runs `Test-VisualHarnessPrimitives` only — no game window required.

## CI / pre-commit gate

`ci-gate` runs L0+L1 scenarios only — no game required, no `deploy.config.json` needed:

```powershell
.\tools\debug-harness\run.ps1 ci-gate
```

This is suitable as a pre-commit hook, CI step, or fast local smoke test after code changes.

Parse results programmatically:

```powershell
$output = & .\tools\debug-harness\run.ps1 ci-gate 2>&1
$result = ($output | Select-String '^harness-result: ').Line -replace '^harness-result: ', '' | ConvertFrom-Json
if (-not $result.pass) { throw "CI gate failed" }
```

### `mp-playthrough-bots` interaction log

Each run writes `%TEMP%\mirroredge-debug\<sessionId>-interactions.ndjson` with one JSON object per line:

| Phase | Examples |
|-------|----------|
| `intro` | `boot_wait_begin`, `poll` (map/inGameplay), `skip_key`, `main_menu_ready` |
| `session` | `inject_begin`, `connected_at_menu`, `in_level`, `pass` |
| `menu` | `start_key`, `entered_gameplay`, `loading` |
| `bots` | `spawn`, `remote_poll`, `remote_ready` |
| `movement` | `sample` (pos, yaw, remotes each tick), `session_end` |

Flow (2026-06):

1. Start local `multiplayer-server` (port 5222).
2. Write `%TEMP%\core.settings` (`client.server`, `client.room`, `mods.multiplayer`).
3. `Invoke-EnsureCoreLoaded` (waits for bootstrap) → `RELOAD_SETTINGS` (before intro cinematics).
4. Boot wait (`-MinIntroBootSec`, default 25s) → adaptive intro skip → **blind** Enter+Escape rounds (`Invoke-GameIntroSkipBlind`).
5. `INJECT multiplayer` → optional `MENU_TAB Multiplayer` (listener also starts on plugin `Initialize`).
6. `Wait-MmultiplayerConnected` (uses normalized `mpConnected`; see below).
7. `Invoke-GameStartFromMenu` (Enter-only or Enter+Escape) + `Wait-MmultiplayerInLevel` when status is available.
8. Spawn follower bots (`tools/debug-harness/tools/bot.ps1`) → movement samples → `session|pass`.
   - **v1.2.6+:** Follow defaults **on** (host UDP / `%TEMP%\mirroredge-bot-target.json`). Use `-NoFollow` only for the old ~500,500 orbit demo. Keep `bot.ps1` ASCII-only in double-quoted strings (PS 5.1).

Intro hang immunity is extended during boot, intro skip, and level-load waits (`Enable-HarnessIntroHangImmunity`, `SkipHangCheck` on `Assert-GameProcessAlive`).

**Status polling:** harness `Expand-CoreHarnessStatus` maps split-mode `GET_STATUS` fields (`multiplayer.connected` → `mpConnected`, `remotePlayers` → `mpRemotePlayers`, flattens `engine.*`). Prefer core pipe; manager `GET_STATUS` merges core `multiplayer` when core pipe is slow.

**Inject fallbacks** (used by `mod-full`, `mp-gui-test`, `user-flow`):

| Path | When |
|------|------|
| UI click (`manager/inject/<id>`) | Default; Modules list uses `BeginChild` — inject buttons register in `GET_UI_TARGETS` via `RecordRect` even when clipped; harness scrolls the list before polling |
| Pipe `INJECT <id>` | UI target missing / timeout |
| Console keyboard `` ` `` + `inject <id>` | L3 user paths |
| Pipe `CONSOLE_EXEC inject <id>` | Console keyboard did not queue the line (manager pipe) |

`user-full-session` writes `session|pass` (and its visual capture) **before** `Complete-SplitInjectionSession` so the game window still exists for `session_pass` screenshots.

### `user-flow` (real input path)

Unlike pipe-driven L2 tests (`MENU_OPEN`, `INJECT`), `user-flow` drives the game via `SendInput`:

| Step | Input | Assert |
|------|-------|--------|
| Main menu wait | — | hooks ready |
| Module Manager | `Insert` ×2 | `menuOpen` toggles |
| Console inject | `` ` `` → type `inject core` → Enter | `consoleOpen`, module load log, `engine.modReady` |
| Console close | `Escape` | `consoleOpen=false` |
| Movement sample | W/A/S/D + mouse move | no crash |
| Focus round-trip | minimize → restore | hooks still installed |
| Final menu | `Insert` → `Escape` | `menuOpen` closes |

### `user-full-session` (open → close, real input only)

Full visual regression: launcher **Launch Game** and **Close** use real mouse clicks; all in-game actions use `SendInput` (no `MENU_OPEN` / `INJECT` pipe). Writes `*-interactions.ndjson` with phases `launcher`, `intro`, `inject`, `menu`, `ui`, `level`, `movement`, `quit`.

| Step | Input | Assert |
|------|-------|--------|
| Launch | Real mouse **Launch Game** | game window + hooks |
| Boot logos | wait `${MinIntroBootSec}s` | process alive |
| Blind intro | Enter + Escape × N | — |
| Console inject | `` ` `` → `inject core` → Enter → Escape | `engine.modReady` |
| Adaptive intro | Enter/Escape until `tdmainmenu` | main menu |
| UI smoke | same as `user-flow` (`Insert`, WASD, focus) | hooks + `engine.modReady` |
| Optional level | `-EnterLevel`: Enter/Escape from menu | `inGameplay` |
| Optional play | `-PlaySeconds`: WASD + mouse | movement samples |
| Quit game | Escape → **Alt+F4** | `MirrorsEdge.exe` gone |
| Quit launcher | Real mouse **Close** | launcher exits |

### `auto-loop` (retry + triage)

Runs scenarios sequentially with up to `MaxRetries` re-attempts per step. On failure writes a bundle under `%TEMP%\mirroredge-debug\auto-loop-reports\<runId>\`:

- `failure-summary.json`, `debug-log-tail.txt`, `manager-status.json`, `core-status.json`, `manager-log.txt`
- `auto-loop-report.json` — master report with layer info per scenario
- `reflection.json` / `reflection.md` (per failed attempt) + `reflection-summary.md` (per run)

Use `-RebuildOnFail` to run `Invoke-DebugBuild` between retries; `-ContinueOnFail` to keep going after a exhausted scenario.

**Reflection step (自动反思):** After each failed attempt the loop runs `Write-HarnessReflection`, which is a deterministic (no-LLM) analysis:

1. **Evidence** — parses core/manager `GET_STATUS`, the engine phase ring (`mirroredge-phase.bin`), and spawn/debug log tails from the bundle.
2. **Test-flow meta-reflection** — a milestone checklist (core ready → hooks → real level → connected → hosted live → remotes present → remotes spawned) marks which stage the flow reached, reports **where the flow stopped**, and prints a concrete **improvement** for that stage (i.e. whether the test itself is correct / how to make it better).
3. **Known dead-end cross-check** — extracts signature tokens (phase names like `drain.warm.tdengine` / `spawn.char.begin`, `Api(true)` calls) from the evidence and matches them against the **Failed approaches** tables in `docs/known-issues/*.md` + `docs/mp-set-gameplay-runbook.md`. A match sets `StopRetry`: the loop **skips blind rebuild/retry** of a documented dead-end and records which approach + why it fails.

`Get-HarnessKnownFailedApproaches` and `Write-HarnessReflection` are exported for standalone use.

### Exit guard (normal quit required)

Every game scenario that calls `Start-SplitInjectionSession` must end with `Complete-SplitInjectionSession`. This enforces:

| Check | Failure means |
|-------|----------------|
| `ExitCode == 0` on `MirrorsEdge.exe` and `ModuleLauncher.exe` | Abnormal termination (e.g. `0xC0000005`) — investigate/fix |
| No Application log **Event 1000** since session start | Hard crash — investigate/fix |
| Teardown via **Alt+F4** + launcher **Close** (real input) | Forced `taskkill` is not used on success paths |

Mid-test crash: `Assert-GameProcessAlive` detects unexpected exit and fails with exit code + Event 1000 hint.

### Hang guard (freeze detection)

After `Start-SplitInjectionSession`, a background watchdog plus inline probes detect **process alive but unresponsive**:

| Signal | Meaning |
|--------|---------|
| `IsHungAppWindow` / `SendMessageTimeout` | Game window marked not responding |
| `GET_STATUS.endSceneCalls` / PrintWindow | Render pipeline stalled (soft hang: pipe alive, window not marked hung) |
| 4 consecutive `PING` failures on `module_manager` pipe | Mod IPC wedged while process lives |

Grace period: first **45s** after launch (boot logos / heavy load). After hooks install, also watches **render pipeline stall** (`GET_STATUS.endSceneCalls` unchanged or PrintWindow frame unchanged ≥20s) — catches soft freezes where `IsHungAppWindow` and pipe PING still succeed. Failures write `interaction` phase `hang` to `*-interactions.ndjson`, then auto-export a triage bundle under `%TEMP%\mirroredge-debug\hang-reports\<timestamp>-<label>\`:

Startup-specific guard: `Assert-GameBootProgress` runs after hooks/core ready in key boot scenarios. It polls `currentMap` and captures window frames. It passes on either a map/main-menu signal or repeated dynamic frame changes (the game left the static splash). It fails as `boot-progress` when the frame remains static too long, the window is repeatedly unresponsive, or no progress signal arrives before timeout. This covers the splash-window false negative tracked by [KI-2026-008](known-issues/KI-2026-008-startup-splash-harness-gap.md).

| Step | File | Purpose |
|------|------|---------|
| **1** | `interaction-hang-tail.jsonl` | Last hang interaction records |
| **2** | `debug-log-tail.txt` | Agent NDJSON tail before hang |
| **3** | `window-hang-context.json` | `IsHungAppWindow`, overlay/hooks/viewport (D3D/ImGui/focus) |
| **4** | `pipe-hang-context.json`, `manager-log.txt` | Pipe fail streak + module_manager ring buffer |

Also: `hang-summary.json`, `manager-status-raw.json`, full `interaction-log-full.ndjson`.

```
hang-guard: menu start: game window not responding (IsHungAppWindow); triage bundle -> %TEMP%\hang-reports\<timestamp>-menu-start
```

```powershell
# At end of every L2/L3 scenario:
Complete-SplitInjectionSession -Context $ctx
```

### Set Gameplay / bot visibility (read before re-debugging)

Hand-off runbook: [`docs/mp-set-gameplay-runbook.md`](mp-set-gameplay-runbook.md).

**Verified 2026-07-18:** Manual Set Gameplay reaches `activation set live`. Launcher lines `MmodDrainSpawnQueue queue=0` with `listSize=0` in `%TEMP%\mirroredge-multiplayer-client.log` mean **no remotes**, not a broken activation chain. Agents must read that client log first; do not re-litigate GetWorld(true) on the Set Gameplay click.

### Phase breadcrumb ring (where the game thread is stuck)

`engine.dll` writes an always-on rolling ring of **phase markers** into a memory-mapped file `%TEMP%\mirroredge-phase.bin` via `EngineInternal::SetPhase` (see `engine_internal.h`). Only the game/EndScene thread breadcrumbs — background (network) threads deliberately do **not**, so a hang preserves the culprit entry instead of overwriting it. No debugger or symbols required.

Instrumented spots: `GetWorld` (`iterate.begin/scan/end`), `GetPlayerController` (`GetPC.iterate.*`), `TickHook` (`tick.body/spawn.drain/callbacks/original/idle`), `SpawnCharacter` (`spawn.char.begin/ok`), EndScene drain (`drain.enter/spawn/spawn.done`).

Decode the ring at any time (highest `seq` = last thing the main thread ran):

```powershell
.\tools\decode-phase.ps1            # top 20 newest entries, "<== LAST" marks the culprit
.\tools\decode-phase.ps1 -Top 40 -Raw
```

### Freeze watchdog test (`mp-freeze-test.ps1`)

Standalone loop for the common workflow (user manually reaches a level, then MP is injected). Attaches to a running `MirrorsEdge`, runs the inject → force-live → connect → bots sequence, then watches `IsHungAppWindow`. On a hang it decodes the breadcrumb ring, snapshots both `GET_STATUS` pipes, copies all diagnostic logs into `%TEMP%\mirroredge-freeze\<timestamp>-freeze\`, optionally captures a ProcDump, and **kills the game so the machine recovers** — ending the run with a concrete `LAST PHASE BEFORE FREEZE` line.

```powershell
.\tools\mp-freeze-test.ps1                       # auto-detect level, 2 bots, 90s watch
.\tools\mp-freeze-test.ps1 -BotCount 1 -PlaySeconds 120 -CaptureDump
.\tools\mp-freeze-test.ps1 -NoInject -NoBots     # watchdog only (already set up)
```

Exits `spawned` (bots visible), `freeze`, `crash`, or `timeout`.

### Crash capture (`run-with-dump.ps1`)

Runs a harness scenario with Sysinternals ProcDump attached to `MirrorsEdge.exe` (unhandled exceptions, full minidump):

```powershell
.\tools\debug-harness\run-with-dump.ps1
.\tools\debug-harness\run-with-dump.ps1 -Scenario mp-playthrough-bots -ScenarioArgs @("-BotCount","1","-PlaySeconds","10")
.\tools\debug-harness\run-with-dump.ps1 -SkipProcDump   # scenario only, no dump watcher
```

Dumps land in `%TEMP%\mirroredge-dumps\`. Use MCP `debug_list_dumps` or inspect manually. Requires network on first run to download `procdump.exe` if not cached.

### ModLog → NDJSON bridge

When `MMOD_DEBUG_SESSION` is set (harness / MCP session), `ModLog::Write` in **module_manager**, **core**, and **engine** mirrors each line into the agent NDJSON log (`component: mod_log`). Filter with `debug_query_log -component mod_log`.

## Control pipes

| Target | Pipe | Commands |
|--------|------|----------|
| module_manager | `\\.\pipe\mirroredge_module_manager_control` | `PING`, `GET_STATUS`, `GET_UI_TARGETS`, `LIST_MODULES`, `GET_LOG [n]`, `INJECT <id>`, `UNLOAD <id>`, `MENU_OPEN`, `MENU_CLOSE`, `MENU_TAB <name>`, `CONSOLE_OPEN`, `CONSOLE_CLOSE`, `CONSOLE_EXEC <line>` |
| core | `\\.\pipe\mirroredge_module_control` | `PING`, `GET_STATUS`, `GET_UI_TARGETS`, `LIST_MODS`, `SET`, `RELOAD_SETTINGS`, `CONSOLE ...` |

`GET_STATUS` returns JSON (`hooksInstalled`, `overlayReady`, `menuOpen`, `consoleOpen`, `tabs`, `activeTab`, `modules`, `engine`, …). When `engine.dll` is loaded, `engine` is the object from `MMOD_EngineFormatStatusJson`; otherwise `engine` is `null`.

**Core hosted status** (when `core.dll` is loaded): top-level `currentMap`, `inGameplay`, and nested `multiplayer` (`connected`, `remotePlayers`, `posX/Y/Z`, `yaw`). Harness normalizes these to legacy flat names (`mpConnected`, `mpRemotePlayers`, …) for playthrough predicates. `Invoke-ModControlPipe -Target mmultiplayer` is an alias for `core`.

**Multiplayer client:** `multiplayer.dll` starts its TCP listener in `ClientPlugin::Initialize()` (not only when the Multiplayer ImGui tab renders).

## MCP tools (mirroredge-debug)

Registered in `.cursor/mcp.json` by `setup.ps1`. The server starts via `tools/mcp-debug-server/run.ps1` (PowerShell wrapper that locates `node.exe` when it is not on PATH — required because Cursor often lacks shell PATH entries).

| Tool | Purpose |
|------|---------|
| `debug_get_harness_test_status` | Cross-machine harness pass/fail from `test-logs/index.json` |
| `debug_initialize_session` | Create new session id + manifest |
| `debug_get_last_session` | Read last manifest |
| `debug_list_scenarios` | List scenario scripts |
| `debug_tail_log` | Tail NDJSON log |
| `debug_query_log` | Filter NDJSON by component, hypothesisId, location/message substring |
| `debug_get_mod_log` | Read module_manager ring buffer via `GET_LOG` pipe |
| `debug_tail_interactions` | Tail harness `*-interactions.ndjson` (optionally filter phase/action) |
| `debug_list_dumps` | List crash dumps from `run-with-dump.ps1` |
| `debug_list_hang_reports` | List hang triage bundles under `hang-reports/` |
| `debug_read_hang_report` | Read `hang-summary.json` + key triage files from a bundle |
| `mod_control` | Send pipe command (`GET_STATUS` parsed as JSON) |
| `debug_run_scenario` | Run harness scenario |
| `debug_run_auto_loop` | Run `auto-loop` with retries and failure bundles |

## AI debug loop

1. `debug_initialize_session` or `debug_run_scenario verify-harness`
2. `debug_run_scenario smoke-split` / `user-flow` / `inject-mp` for live tests
3. On failure: `debug_tail_log` / `debug_query_log` + `debug_get_mod_log` + `mod_control` with `GET_STATUS`, or `debug_run_auto_loop -RebuildOnFail`
4. Inspect `%TEMP%\mirroredge-debug\auto-loop-reports\<run>\` failure bundles
5. Add `DebugTrace::Event` / `AgentDebugLog` probes with `hypothesisId`
6. `build.ps1 -DeployProxy` and re-run scenario

## MCP troubleshooting

| Symptom | Fix |
|---------|-----|
| `mirroredge-debug` missing in Cursor MCP list | Run `.\tools\debug-harness\setup.ps1`, then **Reload Window** |
| Server shows error / exits immediately | Install [Node.js](https://nodejs.org/); re-run `setup.ps1` (`npm install` + merged `mcp.json`) |
| `Node.js not found` in MCP logs | Wrapper checks `Program Files\nodejs` and `%LOCALAPPDATA%\Programs\node`; install Node or add to PATH |
| Game scenarios fail at deploy | `deploy.config.json` with `deployPath`, or `ME_DEPLOY_PATH` |

## Related docs

- [troubleshooting.md](troubleshooting.md) — manual diagnostic flows
- [module-manager.md](module-manager.md) — verified log chain
