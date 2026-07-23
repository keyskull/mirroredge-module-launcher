# Multiplayer Set Gameplay â€” AI runbook (read first)

**Purpose:** Authoritative handoff for hosted-split bot visibility. Stop re-diagnosing the same loops.

**Product:** â‰¥ **1.2.6**  
**Related:** [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md) Â· [troubleshooting.md](troubleshooting.md) Â· [testing-character-spawn.md](testing-character-spawn.md) Â· [ai-debug-harness.md](ai-debug-harness.md)

---

## 1. Critical path (do not skip or reorder)

| # | Gate | Pass proof | Fail hint |
|---|------|------------|-----------|
| 1 | Story / real level **before** gameplay hooks | Manual Story or `START_NEW_GAME`; **no** hooks before load | White frame / EndScene stall |
| 2 | Inject multiplayer | `gameplay hooks deferred until Set Gameplay` | Stale DLL still installs at plugin init |
| 3 | Set Gameplay / `FORCE_HOSTED_LIVE` | `activation set live`, `hooks=1`, `live=1` | Do not blame empty spawn queue |
| 4 | Server + bots same room | `connected=1`, `listSize>=1`, room `playthrough-lobby` | `listSize=0` = no remotes |
| 5 | Spawn | `spawn ok id=... actor=` non-null | No `engine-spawn.log` => crash before spawn |
| 6 | Pose near host | `remote pose applied` near host (**not** `~(500,500,300)`); bot `Follow : on` | Use Follow / TargetFile |
| 7 | Visual mesh | Distinct skeletal meshes near camera | Nametag/chat alone != PASS |
| 8 | FPS after bots | Tick gaps ~frame time, not ~2s | Close console; need ActorTick fix |

**Motion phase1+2+3 PASS:** `mp-real-level-bots.ps1` EXIT=0 â€” host snapshot + **16-frame ring** `%TEMP%\mirroredge-host-bones-cycle.bin` (MEBC), `local bones cycle frames>=4`, bots cycle frames when moving (`-AnimateBones`). Phase1+2 still required: single dump 658 bytes, `remote bones applied` x BotCount.

**Golden client.log**

```text
client: gameplay hooks deferred until Set Gameplay
client: activation set live
client: spawn ok id=... actor=........
client: remote pose applied id=... loc=(-4494,-8064,5854)
client: local bones sampled nonZero=... (dumped %TEMP%\mirroredge-host-bones.bin)
client: local bones cycle frames=4 (dumped %TEMP%\mirroredge-host-bones-cycle.bin)
client: remote bones applied id=... (TransformBones on)
```

**Golden bot.ps1**

```text
Follow : on (dist=220)
[bot] Using bone cycle 4 frames from ...\mirroredge-host-bones-cycle.bin
[bot] Following player id ... via UDP score=18,571
[bot] Near host stand-off=(...) host=(...)
```

**Visual PASS proof:** `%TEMP%\mirroredge-debug\real-level-bots\130454-final.png` (Cam1 Kate + Cam5 Miller near host on tutorial rooftop).

---

## 2. Quick triage (before changing code)

```text
1. Read this runbook + KI-2026-005 Failed approaches
2. Tail %TEMP%\mirroredge-multiplayer-client.log (last 80 lines)
3. Check gates 3-6 above
4. listSize=0  -> start server + bots; do NOT "fix" Set Gameplay
5. Hang       -> .\tools\decode-phase.ps1 + mp-freeze-test.ps1
```

### Logs (read yourself; do not ask user to paste)

| File | Use |
|------|-----|
| `%TEMP%\mirroredge-multiplayer-client.log` | **Primary** â€” activation, spawn, pose, Diag |
| `%TEMP%\mirroredge-engine-spawn.log` | Spawn stages (missing => pre-spawn crash) |
| `%TEMP%\mirroredge-phase.bin` | Hang breadcrumb (`decode-phase.ps1`) |
| `%TEMP%\spawn_drain_trace.txt` | EndScene drain |
| Launcher `[core] MmodDrainSpawnQueue` | Engine drain only â€” not MP detail |

### False alarms

| Symptom | Meaning |
|---------|---------|
| Set Gameplay, nothing visible | Activation OK; **no remotes** (`listSize=0`) |
| `MmodDrainSpawnQueue queue=0` | Healthy empty queue â€” nobody queued a spawn |
| Expect bots from Set Gameplay alone | Need server + bots in same room |

---

## 3. How to test (Steam ME)

### A. Activation only

1. `.\build.ps1` (deploy via `deploy.config.json`)
2. ModuleLauncher -> real level (Faith controllable)
3. Inject core, then multiplayer
4. Connect `127.0.0.1:5222` / `playthrough-lobby`
5. Diag -> **Set Gameplay** -> expect `activation set live`

### B. Full bots visible

1. Complete A (`live=1`)
2. Ensure `multiplayer-server.exe` running
3. Bots after host in-level + Set Gameplay:

```powershell
.\tools\debug-harness\tools\bot.ps1 -Character 1 -Level gameplay
# or: .\tools\mp-real-level-bots.ps1
```

4. Expect gates 5-8 (spawn ok, near-host pose, mesh, FPS)

### C. Freeze / crash

```powershell
.\tools\decode-phase.ps1
.\tools\mp-freeze-test.ps1 -BotCount 2 -PlaySeconds 90
```

Do **not** reintroduce `GetWorld(true)` / `GetPlayerPawn(true)` on EndScene or Set Gameplay.

---

## 4. Hard rules (verified)

| Rule | Why |
|------|-----|
| Hooks only after Story settles / via Set Gameplay | Plugin-init hooks wrap LoadMap -> hitch |
| Warm TdEngine **EndScene only**, incremental | Tick / `TryFindTdGameEngine(true)` freezes |
| Hosted Tick must **not** drain spawns | Re-entrancy hang at `spawn.char.begin` |
| Host must send UDP (pull-model server) | Else no `remote pose applied` |
| `bot.ps1` Follow default on (`-NoFollow` = demo orbit) | Else stuck at ~(500,500,300) |
| ASCII only in `bot.ps1` / DebugHarness double-quoted strings | PS 5.1 parse break on em-dash |

---

## 5. Code map

| Step | Where |
|------|--------|
| Set Gameplay UI | `mods/multiplayer/client_ui.cpp` |
| Apply level | `client_level.cpp` `ApplyManualClientLevel*` |
| Activate | `client_remote.cpp` `QueueActivateHostedGameplay` |
| Live + spawn queue | `CompleteMultiplayerHostedActivation` / `QueueSpawnEligibleRemotePlayers` |
| Spawn + drain | `engine_hooks_gameplay.cpp` / `MMOD_EngineDrainSpawnQueue` |
| Warm | `safe_gui.cpp` `TryWarmTdGameEngineIncremental` (EndScene only) |
| Real level harness | `START_NEW_GAME` **before** gameplay hooks |

---

## 6. Failed approaches â€” do NOT retry

Full history: [KI-2026-005](known-issues/KI-2026-005-mp-playthrough-ingameplay.md). Top hits:

| Do not | Result |
|--------|--------|
| `GetWorld(true)` / `GetPlayerPawn(true)` / `TryFindTdGameEngine(true)` on Set Gameplay / EndScene / Tick | Freeze |
| Gameplay hooks before Story / at MP plugin init | White frame / level-entry hitch |
| Hosted Tick spawn drain | `spawn.char.begin` hang |
| Warm from TickHook | `tick.warm.slice` hang |
| `CONSOLE open` / `START_GAME LevelName` for tutorial | Crash / no map |
| Treat nametag/chat as mesh proof | False visual PASS |
| `OnActorTick` pose for every world actor | ~0.4 FPS after bots |
| Bare `bot.ps1` without Follow / lock first UDP peer at ~500 | Far from player |
| Em-dash in `bot.ps1` double-quotes | PS 5.1 parse error |
| `ShutDown` or soft-hide writes on live remotes after TransformBones | Soft-freeze; Despawn must be ref-drop only (KI-2026-012) |

---

## 7. Status / plan

**Done (v1.2.6 + mesh V1–V11):** activation live; spawn ok; TdEngine warm (pre-FORCE); host UDP; softColl; Tag/Interact; worldClamp; B3-lite; pose smooth; mesh visual corridor/FOV/wave/idle; `remoteTdPawn` removed.

**Shipped checklist** (historical order; all done unless noted)

1. _(done)_ B3-lite: MovementState trailer + parkour bone TX (UDP 690) — mesh `TransformBones` only
2. _(done)_ Phase 8 host-driven motion: bot speed-driven cycle + live UDP bone stream log; sin-modulate only if cycle missing
3. _(done)_ Prefer `client.log` `spawn ok` over harness `spawnedPlayers`
4. _(done)_ UDP pure push-relay
5. _(done)_ Despawn ref-drop only on bot disconnect (KI-2026-012)
6. _(done)_ Soft collision (fake XY push) — live pose path only; no disconnect mutates
7. _(done)_ Soft collision auto: SoftProbe overlap + `soft collision engaged` in mp-real-level-bots
8. _(done)_ Pre-bot host pose (A1/A2): empty-queue **TdEngine-only** warm when hosted-live (1100/500ms); Tick GamePlayers seeds PC/camera; quiet 1500ms; harness ≤70s; **do not** raise to 2000/200; **do not** warm TdEngine on Tick before FORCE (hung after IDLE_PC_SEED)
9. _(done)_ B0 Tag: server `startTagGameMode`/`tagged`/`canTag` + client ResolveLocal pawn-safe; announce→chat
10. _(done)_ B1 near-distance interact: E / keybind → TCP `interact` wave; chat UX only (KI-012)
11. _(done)_ SoftProbe auto Tag+Interact: `bot.ps1 -StartTag -SendInteract`; motionPass needs `tag mode live`/`tagged id=` + `interact recv|sent`
12. _(done)_ Host physics telemetry + remote FastTrace world clamp (floor/wall); SoftProbe `-PhysicsFallDrop`/`-PhysicsWallSlam`; opt-out `-SkipPhysicsProbe`
13. _(done)_ Pose smooth: From→To snap beyond `poseSnapUu`; EMA write (`poseSmooth`/`poseSmoothAlpha`) toward network target — reduces soft-coll/packet jitter
14. _(done)_ B3-lite true-motion TX: UDP `MovementState`+`Physics` (690); force Mesh3p sample + full-rate send on parkour moves (`MOVE_Falling+`); logs `move state from=… to=…` / `remote move state`
15. _(removed 2026-07-22)_ B3 TdPawn remote spike — deleted; see [KI-2026-013](known-issues/KI-2026-013-tdpawn-remote-spike.md)
16. _(done 2026-07-21)_ Mesh polish batch: host slow-walk bone sample; bone EMA; parkour bone snap; TransformBones diagnostics; leave-chat orphan note
17. _(done 2026-07-21)_ Mesh visual V1–V3: sample/TX throttle; boneSmooth alphas; first-live snap; idle yaw snap
18. _(done 2026-07-21)_ Mesh visual V4: LastGoodBonePose + grounded parkour override
19. _(done 2026-07-21)_ Mesh visual V5: `live-mesh` before bot stop; SoftProbe PhysicsProbeDelayMs
20. _(done 2026-07-21)_ Mesh visual V6: pitch-down before `live-mesh`
21. _(done 2026-07-21)_ Mesh visual V7: host UDP yaw from Controller look
22. _(done 2026-07-21)_ Mesh visual V8: corridor + SoftProbe FollowDistance 70; floor clamp PrePivot 94; FallDrop skip hostPush; `bUpdateSkelWhenNotRendered`
23. _(done 2026-07-22)_ Mesh visual V9: body-yaw corridor; MaxLateral 50; Cam ±30
24. _(done 2026-07-22)_ Engineering hygiene: `mp-real-level-bots` auto-copies `dist/modules/{engine,multiplayer}` (+ server exe from **dist first**) into GameRoot; fails closed if `module_manager` missing. Server UDP recv buffer 2048 (Windows WSAEMSGSIZE when buf≤packet).
25. _(done 2026-07-22)_ Mesh visual V10: walk bone EMA (`boneSmoothWalkAlpha`); host cycle dump while walking; bot cycle frame lerp; TCP wave → `WaveGestureFrames` + face host; foot plant Z≈host+94; face velocity when moving. Verified harness EXIT=0 (`remote wave gesture`).
26. _(reverted 2026-07-22)_ Pre-FORCE TdEngine warm / quiet 750ms — correlated with `IsHungAppWindow` after `IDLE_PC_SEED` under empty-world FORCE; quiet back to 1500ms; warm only when hosted-live.
27. _(done 2026-07-22)_ Mesh visual V11: stronger wave amp + slots; idle micro (client amp 0.28); bot stand-off MOVE_None + bone sway; wider grounded parkour override.
28. _(done 2026-07-22)_ Mesh visual V12: wave/idle retarget off Faith face atoms 15–23 (slots 40–71 → weird eyes); arms 45–52 + torso 1–6 + leg walk-in-place 70–74; idle face-host yaw (neck not in CompressedBoneOffsets).
29. _(done 2026-07-22)_ Mesh V12b: grounded-parkour bone override only after real fast fall (`LastFastFallMs`); SoftProbe FallDrop V≈0 hover was snapping to join/rest `LastGoodBonePose`.
30. _(done 2026-07-22)_ Mesh V13: remove synthetic wave/idle bone nudges — remotes = host Mesh3p only; wave = chat + face-host yaw. SoftProbe wave blackout through FallDrop/WallSlam+2s settle; snap back to Follow stand-off after WallSlam.
31. _(done 2026-07-22)_ Named Mesh3p bone-clip library (V14): host dumps `%TEMP%\mirroredge-bone-clip-{Idle,Walking,Falling}.bin` (MEBC); bot selects clip by MovementState (Falling→Walking→Idle fallback). Still TransformBones — not AnimTree. Harness `Drive-HostMeshClipCapture` holds W+Shift (+ optional Space) so Walking/Falling fill. **Clip bucket:** Falling = `PHYS_Falling`/`MOVE_Falling` only — do **not** treat Td `EMovement>=2` (e.g. sprint=15) as Falling.
32. _(done 2026-07-22)_ Mesh V15 clip/interp/clamp: remote fall bone-snap only for real Falling (not sprint); walk EMA for Td grounded moves; soft floor Z approach; bot Idle↔Walk↔Fall ~180ms crossfade (phase preserved). Wall clamp stays hard reject.
33. _(done 2026-07-22)_ Mesh V16: MEBC v2 host `baseTickMs` + per-frame `relMs` (bot wall-clock phase align); client live-UDP Idle/Walk/Fall bone stance crossfade ~180ms (`remote bone stance xfade`).
34. _(done 2026-07-22)_ Mesh V16b: stance bucket hysteresis — Idle↔Walk hold 150ms; Falling enter/leave 40ms; skip re-xfade while blending unless fall edge.
35. _(done 2026-07-22)_ Mesh V17: soft corridor lateral/along lerp; stale UDP bones (>180ms) ease to LastGood (`remote bones stale settle`).
36. _(done 2026-07-22)_ Mesh V18: adaptive `interpolationDelayMs` from UDP inter-arrival EMA + TCP `latencyMs` (`interp delay auto`); UI Auto Interp Delay (default on); base slider floors the auto target.
37. _(done 2026-07-22)_ Mesh V19: per-remote adaptive delay — each `Player` tracks `UdpIntervalEmaMs` / `UdpJitterPeakMs` / `InterpDelayMs`; `BuildRenderedPacket` uses peer delay (fallback global). Target ≈ `1.25×peerEma + 0.35×jitterPeak + RTT/2` (clamp 40–200, floored by base). Global UI delay = max(aggregate, peerMax).
38. _(done 2026-07-22)_ Mesh V20: velocity coast past `ToTime` uses squared fade (cap 200ms); stale-bone settle horizon ≈ `2×UdpIntervalEmaMs` (120–280); asymmetric delay EMA (rise 3/4, fall 5/6).
39. _(done 2026-07-23)_ Network harden: optional UDP `Seq` trailer (692 B); client 4-slot reorder before From/To; `client.hostPoseTxMaxHz` (default 60, parkour bypass). Server opaque forward `676–692`. No dual push+pull. Harness motionPass requires `udp seq stream`.
40. _(done 2026-07-23)_ KI-2026-005 playthrough: scenario + `Test-MmultiplayerPlaythroughWithBots` delegate to `mp-real-level-bots`. LAN soak client automates inject/`START_NEW_GAME`/`FORCE_HOSTED_LIVE` (`tools/mp-lan-dual-soak.ps1`).

**Not on the roadmap** (2026-07-22 — removed from plan; keep as hard constraints in §9 Do not)

| Item | Why dropped |
|------|-------------|
| Full remote AnimTree / `SetMove` / handshake montage | KI-012 class; mesh puppets cannot host AnimTree; TdPawn spike removed |
| Remote `PHYS_Falling` / `bCollideWorld=true` | Fights UDP pose writes; soft-freeze class (KI-012) |
| `client.remoteTdPawn` / `SpawnTdPawnCharacter` | Deleted 2026-07-22 (KI-2026-013) |

---

## 8. Changelog (recent)

| Date | Note |
|------|------|
| 2026-07-23 | UDP Seq (692) + client reorder; hostPoseTxMaxHz; playthrough→real-level; mp-lan-dual-soak |
| 2026-07-22 | Mesh V20: faded velocity coast; peer-EMA stale-bone horizon; asymmetric adaptive delay EMA |
| 2026-07-22 | Mesh V19: per-remote adaptive interp delay (UDP EMA + jitter peak); BuildRenderedPacket uses peer delay |
| 2026-07-22 | Mesh V18: adaptive interp delay (UDP interval + TCP ping); Auto Interp Delay UI |
| 2026-07-22 | Mesh V17: soft corridor lerp; stale UDP bone settle to LastGood |
| 2026-07-22 | Mesh V16b: stance xfade hysteresis (Idle↔Walk 150ms; Falling 40ms) |
| 2026-07-22 | Mesh V16: MEBC v2 timestamps + bot phase align; live-UDP stance bone crossfade |
| 2026-07-22 | Mesh V15: Falling-only bone snap; walk EMA for sprint; soft floor clamp; bot clip crossfade |
| 2026-07-22 | Mesh V14: named Mesh3p clips Idle/Walking/Falling (MEBC); bot MovementState clip select |
| 2026-07-22 | Mesh V13: host Mesh3p only (no synthetic bone nudge); SoftProbe wave blackout+stand-off snap after WallSlam |
| 2026-07-22 | Mesh V12b: grounded bone override needs prior |Vz|>400 (SoftProbe V≈0 FallDrop was rest-snapping) |
| 2026-07-22 | Mesh V12: wave/idle off face slots (atoms 15–23); arm/torso/leg slots + idle walk-in-place + face-host yaw |
| 2026-07-22 | Revert pre-FORCE TdEngine warm + quiet 1500ms — 750ms/prelive correlated with hung after IDLE_PC_SEED (world=0 FORCE) |
| 2026-07-22 | Idle visible: bot stand-off sends MOVE_None + bone sway (was forced Walking + static cycle); client idle amp 0.28 |
| 2026-07-22 | Pre-bot pose: warm before FORCE_HOSTED_LIVE; quiet 750ms; warm 1100/500; Mesh V11 wave/idle/grounded |
| 2026-07-22 | Hygiene: harness auto-deploy prefers `dist` server exe; Go UDP recv buf 2048 (stale 676-buf rejected B3-lite 690 → posed=0) |
| 2026-07-22 | Hygiene: harness auto-deploy dist engine/multiplayer (+server) to GameRoot; mesh V10 walk EMA + wave gesture + foot/yaw |
| 2026-07-22 | Plan: drop AnimTree / remote PHYS / TdPawn from roadmap (constraints stay in §9 Do not) |
| 2026-07-22 | Removed `client.remoteTdPawn` / `SpawnTdPawnCharacter` (KI-2026-013 spike deleted; mesh-only remotes) |
| 2026-07-22 | Mesh visual V9: body-yaw corridor + MaxLateral 50; harness Cam ±30 |
| 2026-07-21 | Mesh visual V7: host UDP yaw = Controller look (not pawn Rotation=0) so Follow stand-offs stay in FOV |
| 2026-07-21 | Mesh visual V6: pitch-down before `live-mesh` so stand-off remotes enter FOV |
| 2026-07-21 | Mesh visual V5: `live-mesh` screenshot before bot stop; SoftProbe PhysicsProbeDelayMs=10s (FallDrop no longer empties near-camera during softColl) |
| 2026-07-21 | Mesh visual V4: last-walk bone cache + grounded parkour override (`remote bones grounded override`) for FallDrop/floor-clamp crumple |
| 2026-07-21 | Mesh visual V1–V3: slow-walk sample 33ms / idle TX 66ms; boneSmoothAlpha 0.55 + boneSmoothIdleAlpha 0.70; first-live bone snap; idle yaw snap |
| 2026-07-21 | Harness SoftProbe gate scan: keep reading Tag/Interact after softColl greens (was gated on `!softColl`); latch remoteBones via full-log Select-String |
| 2026-07-21 | Seed pose: prefer `TryGetSeedHostPose` after quiet (not `seedAge<2000` gate); bots `-Level tutorial_p` on lit-visual; host adopt remote level — EXIT=0 map=tutorial_p |
| 2026-07-21 | Map name: do **not** `TryUpgradeGameplayLevelName` (StreamingLevels/GetMapName) from Tick — soft-froze rem=2 sp=0. Harness bots use `tutorial_p` on lit-visual; host `TryAdoptRemoteGameplayLevel` |
| 2026-07-21 | Idle PC seed: defer `g_idlePcSeedCommitted` until WorldInfo readable (`IDLE_PC_SEED_SOFT`); harness fail-closed if pre-bot host pose missing; Shift in unpause; mesh harness EXIT=0 |
| 2026-07-21 | Harness fail-fast on `SM_REMOTESESSION` / KI-2026-011 (no 120s hooks wait under RDP) |
| 2026-07-21 | Mesh polish: slow-walk Mesh3p ≥50ms; `boneSmooth`; MovementState≥2 bone snap; bot move trailer; nametag stance opt; leave-chat orphan note; TransformBones short dest log |
| 2026-07-20 | Pose smooth: snap large From→To jumps; EMA Location/Yaw write (`client.poseSmooth*`) |
| 2026-07-20 | Host phys telemetry; geometric world clamp (floor/wall); SoftProbe physics gates; **Failed:** ProcessEvent Trace on live pose |
| 2026-07-20 | SoftProbe harness gates Tag+Interact (`-StartTag -SendInteract`); client logs `tag mode live` / `tagged id=` |
| 2026-07-20 | B0/B1 docs: runbook §9 protocol tables + troubleshooting Tag/interact; Tag server + interact wave shipped |
| 2026-07-20 | A1/A2 PASS path: idle TdEngine 800/500ms → pre-bot pose ~60s; spawn gate World\|\|PC; **Failed:** idle 2000/200 and WorldInfo warm |
| 2026-07-20 | **Failed:** idle WorldInfo warm 2500/400ms — IsHungAppWindow; do not retry |
| 2026-07-20 | Temp purge at harness start (`clear-harness-temp.ps1`: old shots/reflections/empty dirs) |
| 2026-07-20 | Harness SoftProbe (FollowDistance=18 on host) asserts `soft collision engaged`; `-SkipSoftCollisionProbe` opt-out |
| 2026-07-20 | Soft collision: separate remotes + light local nudge (`client.softCollision*`); KI-012 safe |
| 2026-07-20 | Host pose: empty-queue WorldInfo warm only; motion TX idle sample/send skip; disconnect null-only (no park); join defaultBones + TransformBones; first-UDP bones log |
| 2026-07-20 | Despawn = ref-drop only; harness post-bot hung gate (KI-2026-012) |
| 2026-07-20 | Phase 8: bot speed-driven host cycle + `remote bones live stream`; idle walk-in-place |
| 2026-07-20 | Harness prefers client.log `spawn ok` / pose over GET_STATUS counts |
| 2026-07-20 | UDP pure push-relay (no pull-reply); dual mode caused ~2x UDP + host drop |
| 2026-07-20 | Motion phase5: Mesh3p atom-count clip + Faith 108 defaultBones assert + TransformBones log |
| 2026-07-20 | Motion phase4: Velocity trailer (688) + extrapolate; bot.ps1/server accept 676-688 |
| 2026-07-20 | Motion phase3: host bone cycle ring (MEBC, 16 frames) + bot frame playback |
| 2026-07-20 | Motion phase1+2 PASS: host dump + remote TransformBones; no StaticClass from Tick |
| 2026-07-19 | Remote motion kickoff: bone dump + bot BoneFile; host UDP no longer drops pos when bones fail |
| 2026-07-19 v1.2.6 | Level-entry hitch; post-bot hitch; bot Follow default; this runbook reorganized (milestones-first) |
| 2026-07-19 v1.2.5 | EndScene drain empty-queue early-out; rate-limit spawn/pipe logs |
| 2026-07-19 evening | Visual PASS `130454-final.png`; drain console spam cut |
| 2026-07-19 | Spawn hang / warm / host UDP heartbeat verified |
| 2026-07-18 | Set Gameplay activation verified; `listSize=0` false alarm documented |

---

## 9. Tag / Interact (B0/B1) — technical notes

B0/B1 + B3-lite are shipped (see §7). Full remote AnimTree / `SetMove` and remote PHYS are **not on the roadmap** (constraints below).

### Files

| Area | Path |
|------|------|
| Server Tag + interact | `mods/multiplayer/server/main.go` (`Room` tag state, `checkTagTouch`, `HandleInteract`) |
| Client Tag tick / Dist helpers | `mods/multiplayer/client_remote.cpp` (`OnTickGames`, `TryGetLocalHostLocation`, `FindNearestRemote`, `TrySendNearestInteract`) |
| Client TCP recv | `mods/multiplayer/client_network.cpp` (`announce`, `interact`, `gameMode` / `tagged` / `canTag`) |
| Client UI / input | `mods/multiplayer/client_ui.cpp`, `client_level.cpp` (E key), `client_state.cpp` / `client.cpp` settings |
| Constants | `shared/me_sdk/util/constants.h` (`GameMode_Tag` = `"tag"`) |

### Tag protocol (TCP JSON, null-terminated)

| Direction | `type` | Fields / behavior |
|-----------|--------|-------------------|
| C→S | `startTagGameMode` | Room enters `gameMode=tag`; broadcasts `gameMode`; picks first client as tagged |
| C→S | `endGameMode` | Clears tag state; broadcasts `gameMode=""` |
| C→S | `cooldown` | `cooldown` seconds (1–60); stored as `tagCoolDown` |
| C→S | `dead` | Untagged player died → becomes tagged |
| C→S | `announce` | `body` string; room relay as `announce` (client shows chat) |
| S→C | `gameMode` | `gameMode` = `"tag"` or `""` |
| S→C | `tagged` | `taggedPlayerId`, `coolDown` (int seconds); then timer → `canTag` |
| S→C | `canTag` | Tagged player may move / chase again |
| Join `id` | — | Includes current `gameMode`, `taggedPlayerId`, `canTag` |

**Proximity retag (server UDP):** On each pose packet, parse XYZ from bytes `[4:16]`. If `canTag` and sender ≠ tagged and same level and distance &lt; **1.3 m** → `setTaggedPlayer` to sender. Uses push-relay path (does not reintroduce pull-reply).

**Client pawn-safe (B0):** `OnTickGames` / distance overlay / Goto use `ResolveLocalPlayerPawn(false)` / `TryGetLocalHostLocation` — never rely on cold bare pawn for Tag death detection.

### Interact protocol (B1)

```json
{"type":"interact","from":<u32>,"to":<u32>,"kind":"wave","dist":<meters>}
```

- Client: nearest same-level remote within `interactMaxMeters` (default **2.5**); keybind default **E** (`0x45`); local 1.5 s cooldown; chat echo on send.
- Server: auth `from` = session client; same level; if both have UDP pos require ≤ **3.0 m**, else trust client `dist` when present; broadcast to room.
- UX only — **no** remote `TryWriteActorLocation` / ShutDown / mesh merge (KI-2026-012). Soft collision remains the only live-path remote XY nudge.

### Settings

| Key | Default | Meaning |
|-----|---------|---------|
| `client.interactKeybind` | `0x45` (E) | WM_KEYDOWN interact |
| `client.interactMaxMeters` | `2.5` | Client nearest-remote gate |
| `client.softCollision*` | see B2 | Unrelated; live pose only |
| `game.tagShowDistanceOverlay` | false | Distance panel (works without Tag mode) |

### Manual verify

```text
1. Rebuild/redeploy multiplayer.dll + multiplayer-server.exe (stop old server first)
2. Story → inject MP → Set Gameplay → connect room with ≥1 bot
3. Tag/Minigames → Start Tag → chat [Tag] …; cooldown overlay; tagged nametag red
4. Stand within ~2.5 m of remote → E or Wave Nearest → [Interact] … on both clients
5. Peer leave → no soft-freeze (KI-012)
```

### Auto verify (harness)

```powershell
.\tools\mp-real-level-bots.ps1 -BotCount 2 -PlaySeconds 90
```

SoftProbe starts with `-StartTag -SendInteract` (SoftProbe Character=Kate/1) and, unless skipped, `-PhysicsFallDrop -PhysicsWallSlam`. After SoftProbe log offset, motionPass requires:

| Gate | client.log token |
|------|------------------|
| softColl | `soft collision engaged` |
| Tag | `tag mode live` or `tagged id=` |
| Interact | `interact recv` or `interact sent` |
| floor | `world clamp floor` |
| wall | `world clamp wall` |

Harness notes:

- SoftProbe interact targets the **nearest UDP peer within ~3.5 m** (host at FollowDistance=18), not `followTargetId` (often a stand-off Cam rejected by server `InteractMaxMeters=3`).
- Phase5 `TransformBones visual=` may come from BonesTick match **or** a one-shot bootstrap in `ApplyRemotePlayerWorldPoses` (do **not** rewrite other remotes' LocalAtoms from a foreign BonesTick — soft-freezes spawn drain / KI-012).
- PhysicsFallDrop: SoftProbe hovers hostZ+1000 for ~4s then resumes Follow → host floor-clamps → `world clamp floor`.
- PhysicsWallSlam: SoftProbe teleports to hostX+900 for ~4.5–9s → FastTrace blocked → `world clamp wall`.

Opt-out SoftProbe softColl/Tag/Interact: `-SkipSoftCollisionProbe`.  
Opt-out physics floor/wall: `-SkipPhysicsProbe`.

### Do not

| Do not | Why |
|--------|-----|
| Mutate remote Actor on interact / Tag end | Soft-freeze after TransformBones (KI-012) |
| Assume old `multiplayer-server.exe` has Tag | Pre-B0 Go server ignored `startTagGameMode` |
| Reintroduce UDP pull-reply for Tag distance | Dual UDP load / host drop (phase7) |
| AnimTree / handshake montage / remote `SetMove` | **Not planned** — mesh puppets; ProcessEvent AnimTree = KI-012 class |
| TransformBones all remotes from foreign BonesTick | Spawn stuck pending drain + post-stop hung (2026-07-20) |
| Remote `PHYS_Falling` / `bCollideWorld=true` | **Not planned** — KI-012 class; geometric host-relative clamp only |
| Re-add `client.remoteTdPawn` / `SpawnTdPawnCharacter` | Removed 2026-07-22 (KI-2026-013) |

---

## 10. Host physics telemetry + remote world clamp

### Host (local Faith) — observe only

On each successful `TryReadPawnPose` in `OnLocalPoseNetworkTick`:

| Log | When |
|-----|------|
| `client: phys transition from=%d to=%d vz=...` | `Physics` enum changes |
| `client: fall start height=... z=...` | Enter `PHYS_Falling` (2); includes `EnterFallingHeight` |
| `client: fall land drop=... impactVz=...` | Leave Falling |
| `client: wall hit speedBefore=...` | Walking + horizontal speed collapse (>200→<50) with stable Z |

Rate-limit wall hits ≥500ms. Does not change gameplay.

### Remote world clamp (host-relative geometric)

Before `TryWriteActorLocation` on remotes (after soft-separate):

1. **Floor:** if `desired.Z > host.Z + worldClampUp` → `desired.Z = host.Z + 2`.
2. **Wall:** if horizontal step `|desired.XY - from.XY| > 200` → keep `from.XY`.

**Failed (do not retry):** `Actor::Trace` / `FastTrace` via ProcessEvent on the live pose path — hung EndScene spawn drain (`rem=2 sp=0`, 2026-07-20). Safe wrappers remain in `safe_gameplay` for future offline use only.

Settings: `client.worldClamp` (default true), `worldClampUp=80`, `worldClampDown=400` (reserved). UI: Multiplayer tab **World Clamp**.

Logs (rate-limited): `client: world clamp floor z=...`, `client: world clamp wall xy=(...)`.

**Never:** enable remote `PHYS_*` / `bCollideWorld`, or clamp on disconnect.
