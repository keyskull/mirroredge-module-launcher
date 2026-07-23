# KI-2026-012: Bot disconnect Despawn soft-freezes after TransformBones

## Metadata

| Field | Value |
|------|------|
| **ID** | KI-2026-012 |
| **Status** | resolved |
| **First seen** | 2026-07-20 |
| **Last verified** | 2026-07-20 |
| **Area** | multiplayer / engine |

## Root cause (verified)

Any mutate of remotes after live TransformBones soft-freezes:

1. ShutDown
2. Raw bHidden / Location
3. Soft-hide on Tick
4. **TryWriteActorLocation park** (EXIT=4 hung=True after bot stop)

## Verified fix

- Disconnect: null Actor (nametags stop), chat "left the room", drop refs only.
- No park/ShutDown until level unload.
- Join: defaultBones + HasRemoteBoneMotion for rest; log "remote bones applied" on first UDP bones.

## Failed approaches — do NOT retry

| Date | Attempt | Result |
|------|---------|--------|
| 2026-07-20 | Esc as pause | no |
| 2026-07-20 | Soft-hide Flush/Tick | hung |
| 2026-07-20 | TryWriteActorLocation park on Tick | hung EXIT=4 |
| 2026-07-20 | Foreign BonesTick rewrite all remotes LocalAtoms | spawn pending drain + hung |
| 2026-07-20 | ProcessEvent Actor::Trace/FastTrace on live pose Tick | rem=2 sp=0 spawn drain hang |

## Related

mods/multiplayer/client_network.cpp, client_remote.cpp DrainParkedRemoteActors
