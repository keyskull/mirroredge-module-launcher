# TdPawn remote spike — historical (removed)

**Status:** removed 2026-07-22.

The `client.remoteTdPawn` / `SpawnTdPawnCharacter` / `TrySetTdPawnMove` experiment is gone. Remotes are **mesh-only**: `ASkeletalMeshActorSpawnable` + `TransformBones` (B3-lite `MovementState` trailer remains for bone polish, not AnimTree).

See [KI-2026-013](known-issues/KI-2026-013-tdpawn-remote-spike.md) for failed approaches and why the spike must not be restored casually.

**Regression gate:** `tools/mp-real-level-bots.ps1 -BotCount 2` (no TdPawn switch).