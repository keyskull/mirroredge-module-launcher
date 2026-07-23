# KI-2026-013: TdPawn remote spike (AnimTree / SetMove path)

## Metadata

| Field | Value |
|------|------|
| **ID** | KI-2026-013 |
| **Status** | **removed** 2026-07-22 (code deleted; remotes are mesh-only) |
| **First seen** | 2026-07-20 |
| **Last verified** | 2026-07-22 |
| **Area** | multiplayer / engine / sdk |
| **Tags** | `tdpawn`, `setmove`, `animtree`, `remote-spawn`, `removed` |

## Outcome

**2026-07-22:** Removed the spike entirely — `client.remoteTdPawn`, `SpawnTdPawnCharacter`, `spawn_tdpawn_safe.*`, `TrySetTdPawnMove`, harness `-EnableTdPawnRemote`, and UI checkbox. Production path is mesh + `TransformBones` only. Do not reintroduce without a new KI and evidence that SetMove/AnimTree on remotes is safe (see Failed approaches).

Mesh harness (`mp-real-level-bots.ps1 -BotCount 2`) remains the regression gate.

## Failed approaches — do NOT retry

| Date | Attempt | Result |
|------|---------|--------|
| *(pre-spike policy)* | Remote `PHYS_Falling` / `bCollideWorld=true` | KI-012 class — do not try on remotes |
| *(pre-spike policy)* | Park / ShutDown after TransformBones | KI-2026-012 |
| *(pre-spike policy)* | Foreign BonesTick rewrite all remotes | spawn drain hang |
| *(pre-spike policy)* | ProcessEvent Trace on live pose Tick | rem=2 sp=0 |
| 2026-07-21 | EndScene `TryWarmTdGameEngineIncremental` idle warm | hang after `idle.cont`; use game-thread warm |
| 2026-07-21 | `ATdPawn` Spawn without Mesh + EndScene requeue | Mesh/Mesh3p null; soft-freeze |
| 2026-07-21 | `ShutDown` on mesh-less TdPawn | soft-freeze |
| 2026-07-21 | C++ bitfield assign `bStasis` on TdPawn | clobber Actor flags |
| 2026-07-21 | Pose-path TransformBones bootstrap on donor-attached Mesh | hang risk |
| 2026-07-22 | Re-adding `client.remoteTdPawn` / `SpawnTdPawnCharacter` | **removed by product decision** — do not restore without new evidence |

## Related

- **Docs:** [tdpawn-remote-eval.md](../tdpawn-remote-eval.md) (historical), [mp-set-gameplay-runbook.md](../mp-set-gameplay-runbook.md)
- **KI-012:** disconnect null-only remains mandatory

## Changelog

| Date | Note |
|------|------|
| 2026-07-22 | **Removed** spike code and settings; status → removed |
| 2026-07-21 | Shelved Phase2 SetMove / promote; mesh focus |
| 2026-07-20 | Spike opened; mesh default |

For earlier investigation detail see git history of this file before 2026-07-22.