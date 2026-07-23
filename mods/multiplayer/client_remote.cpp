#include "client_platform.h"
#include "client_internal.h"

#include "menu_shim.h"

#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace ClientInternal {
Engine::Character RemoteVisualCharacter(const Client::Player *player) {
    if (!player) {
        return Engine::Character::Faith;
    }

    // Faith's legacy spawn entry depends on Transient.* materials that are not
    // stable across machines/levels. Use Kate as the multiplayer visual until
    // the Faith asset path is verified against the game packages.
    if (player->Character == Engine::Character::Faith) {
        return Engine::Character::Kate;
    }

    return player->Character;
}

std::mutex g_spawnPendingMutex;
std::unordered_set<unsigned int> g_spawnPendingPlayers;

// Stable spawn out-slots: engine.dll drain writes through a fixed address.
// Passing &player->Actor is heap-stable in theory, but re-queue races and
// API marshalling left Actor null after SPAWN_OK — keep an explicit slot.
struct StableSpawnSlot {
    Classes::ASkeletalMeshActorSpawnable *actor = nullptr;
};
std::mutex g_stableSpawnMutex;
std::unordered_map<unsigned int, std::unique_ptr<StableSpawnSlot>> g_stableSpawnSlots;

void ClearPlayerRemoteVisual(Client::Player *p) {
    if (!p) {
        return;
    }
    p->Actor = nullptr;
    p->HasBoneSmooth = false;
    p->BoneFirstLiveBoostFrames = 0;
    p->HasLastGoodBones = false;
    p->WaveGestureFrames = 0;
    p->LastFastFallMs = 0;
    p->HasClipBucket = false;
    p->PendingClipBucketSinceMs = 0;
    p->HasBoneXfade = false;
    p->HasLastRenderedBones = false;
    p->UdpIntervalEmaMs = 50.0f;
    p->UdpJitterPeakMs = 50.0f;
    p->InterpDelayMs = 0;
}

bool TryReadPlayerRemoteSkel(Client::Player *p,
                             Classes::USkeletalMeshComponent *&out) {
    out = nullptr;
    if (!p) {
        return false;
    }
    if (p->Actor) {
        return MeSdk::Safe::Gameplay::TryReadSkeletalMeshComponent(p->Actor,
                                                                  out);
    }
    return false;
}

static StableSpawnSlot &StableSpawnSlotFor(unsigned int playerId) {
    std::lock_guard<std::mutex> lock(g_stableSpawnMutex);
    auto &slot = g_stableSpawnSlots[playerId];
    if (!slot) {
        slot = std::make_unique<StableSpawnSlot>();
    }
    return *slot;
}

void SyncActorFromStableSlot(Client::Player *player) {
    if (!player) {
        return;
    }
    auto &slot = StableSpawnSlotFor(player->Id);
    if (slot.actor && player->Actor != slot.actor) {
        static std::unordered_set<unsigned int> loggedSync;
        if (loggedSync.insert(player->Id).second) {
            ClientLogf("client: sync actor id=%x slotActor=%p prev=%p",
                       player->Id, slot.actor, player->Actor);
        }
        player->Actor = slot.actor;
        static std::unordered_set<unsigned int> loggedSpawnOkSync;
        if (loggedSpawnOkSync.insert(player->Id).second) {
            ClientLogf("client: spawn ok id=%x actor=%p plausible=%d",
                       player->Id, player->Actor,
                       MeSdk::Safe::IsPlausibleUObject(player->Actor) ? 1 : 0);
        }
    } else if (player->Actor && !slot.actor) {
        slot.actor = player->Actor;
    }
}

bool HasStableSpawnActor(unsigned int playerId) {
    auto &slot = StableSpawnSlotFor(playerId);
    return slot.actor != nullptr;
}

Classes::ASkeletalMeshActorSpawnable *TakeStableSpawnActor(unsigned int playerId) {
    std::lock_guard<std::mutex> lock(g_stableSpawnMutex);
    auto it = g_stableSpawnSlots.find(playerId);
    if (it == g_stableSpawnSlots.end() || !it->second) {
        return nullptr;
    }
    auto *actor = it->second->actor;
    it->second->actor = nullptr;
    g_stableSpawnSlots.erase(it);
    return actor;
}

namespace {
std::mutex g_parkRemoteMutex;
std::vector<Classes::ASkeletalMeshActorSpawnable *> g_parkRemoteActors;
} // namespace

void QueueParkRemoteActor(Classes::ASkeletalMeshActorSpawnable *actor) {
    if (!actor) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_parkRemoteMutex);
    g_parkRemoteActors.push_back(actor);
}

void DrainParkedRemoteActors() {
    // Drop pointers only. TryWriteActorLocation park after TransformBones also
    // soft-froze (KI-2026-012 harness EXIT=4 2026-07-20). Orphans linger until
    // level unload; nametags already stop because Actor is nulled on leave.
    std::lock_guard<std::mutex> lock(g_parkRemoteMutex);
    if (!g_parkRemoteActors.empty()) {
        ClientLogf("client: dropped %zu orphan remote ref(s) (no park write)",
                   g_parkRemoteActors.size());
        g_parkRemoteActors.clear();
    }
}

Classes::ATdPlayerController *
ResolveLocalPlayerController(bool allowExtraResolve) {
    if (auto *pc = Engine::GetPlayerController(false)) {
        return pc;
    }

    // Do NOT walk ControllerList / StaticClass from multiplayer.dll Tick.
    // Engine::GetPlayerController(false) already seeds from WorldInfo with a
    // prewarmed TdPC class. Duplicating that walk here SEH-killed OnTick right
    // after EndScene spawn warmed the world cache (bots spawned, never posed).
    if (allowExtraResolve) {
        static unsigned long long lastNoPcLogMs = 0;
        const auto nowMs = GetTickCount64();
        if (nowMs - lastNoPcLogMs > 5000ull) {
            lastNoPcLogMs = nowMs;
            ClientLogf("client: ResolveLocalPC no Engine PC cache (world=%d)",
                       Engine::GetWorld(false) ? 1 : 0);
        }
    }
    return nullptr;
}

static bool IsUsableLocalPawn(Classes::AActor *actor) {
    if (!actor || !MeSdk::Safe::IsPlausibleUObject(actor)) {
        return false;
    }
    // Type-check via pose read (SEH-safe RawRead). Uses Gameplay prewarmed
    // class when available — never StaticClass from Tick.
    MeSdk::Safe::Gameplay::PawnPoseSnapshot pose = {};
    return MeSdk::Safe::Gameplay::TryReadPawnPose(
        reinterpret_cast<Classes::ATdPlayerPawn *>(actor), pose);
}

static Classes::ATdPlayerPawn *
TryPawnFromCandidate(Classes::AActor *actor, const char *source,
                     bool &loggedSoft) {
    if (!IsUsableLocalPawn(actor)) {
        return nullptr;
    }
    auto *td = static_cast<Classes::ATdPlayerPawn *>(actor);
    if (!loggedSoft) {
        loggedSoft = true;
        ClientLogf("client: soft-resolved local pawn via %s ptr=%p", source,
                   td);
    }
    return td;
}

Classes::ATdPlayerPawn *ResolveLocalPlayerPawn(bool allowExtraResolve) {
    if (auto *pawn = Engine::GetPlayerPawn(false)) {
        return pawn;
    }

    auto *pc = ResolveLocalPlayerController(allowExtraResolve);
    if (!pc) {
        static bool loggedNoPc = false;
        if (!loggedNoPc && allowExtraResolve) {
            loggedNoPc = true;
            ClientLogf("client: ResolveLocalPlayerPawn no PC (world=%d)",
                       Engine::GetWorld(false) ? 1 : 0);
        }
        return nullptr;
    }

    static bool loggedSoft = false;
    Classes::APawn *ack = nullptr;
    Classes::APawn *pawnField = nullptr;
    Classes::AActor *viewTarget = nullptr;
    Classes::AActor *camViewTarget = nullptr;

    MeSdk::Safe::TryReadField(&pc->AcknowledgedPawn, ack);
    MeSdk::Safe::TryReadField(&pc->Pawn, pawnField);
    MeSdk::Safe::TryReadField(&pc->ViewTarget, viewTarget);

    Classes::ACamera *cam = nullptr;
    if (MeSdk::Safe::TryReadField(&pc->PlayerCamera, cam) && cam &&
        MeSdk::Safe::IsPlausibleUObject(cam)) {
        MeSdk::Safe::TryReadField(&cam->ViewTarget.Target, camViewTarget);
    }

    if (auto *p = TryPawnFromCandidate(ack, "AcknowledgedPawn", loggedSoft)) {
        return p;
    }
    if (auto *p = TryPawnFromCandidate(pawnField, "Pawn", loggedSoft)) {
        return p;
    }
    if (auto *p =
            TryPawnFromCandidate(viewTarget, "ViewTarget", loggedSoft)) {
        return p;
    }
    if (auto *p = TryPawnFromCandidate(camViewTarget, "Camera.ViewTarget",
                                       loggedSoft)) {
        return p;
    }

    // PawnList walk only on Tick.
    if (allowExtraResolve) {
        Classes::AWorldInfo *world = nullptr;
        if (!MeSdk::Safe::TryReadField(&pc->WorldInfo, world) || !world ||
            !MeSdk::Safe::IsPlausibleUObject(world)) {
            world = Engine::GetWorld(false);
        }
        if (world && MeSdk::Safe::IsPlausibleUObject(world)) {
            Classes::APawn *list = nullptr;
            if (MeSdk::Safe::TryReadField(&world->PawnList, list) && list) {
                int guard = 0;
                for (Classes::APawn *cur = list; cur && guard < 256;
                     ++guard) {
                    Classes::AController *ctrl = nullptr;
                    MeSdk::Safe::TryReadField(&cur->Controller, ctrl);
                    if (ctrl == static_cast<Classes::AController *>(pc)) {
                        if (auto *p = TryPawnFromCandidate(
                                cur, "PawnList.Controller==PC", loggedSoft)) {
                            return p;
                        }
                    }
                    Classes::APawn *next = nullptr;
                    if (!MeSdk::Safe::TryReadField(&cur->NextPawn, next)) {
                        break;
                    }
                    cur = next;
                }
            }
        }
    }

    static bool loggedFields = false;
    if (!loggedFields && allowExtraResolve) {
        loggedFields = true;
        ClientLogf("client: PC pawn fields empty pc=%p ack=%p pawn=%p vt=%p "
                   "camVt=%p",
                   pc, ack, pawnField, viewTarget, camViewTarget);
    }
    return nullptr;
}

bool TryGetLocalHostLocation(Classes::FVector &out) {
    if (auto *pawn = ResolveLocalPlayerPawn(false)) {
        MeSdk::Safe::Gameplay::PawnPoseSnapshot pose = {};
        if (MeSdk::Safe::Gameplay::TryReadPawnPose(pawn, pose)) {
            out = pose.location;
            return true;
        }
    }
    if (auto *pc = ResolveLocalPlayerController(false)) {
        Classes::ACamera *cam = nullptr;
        if (MeSdk::Safe::TryReadField(&pc->PlayerCamera, cam) && cam &&
            MeSdk::Safe::IsPlausibleUObject(cam)) {
            Classes::FVector loc = {};
            if (MeSdk::Safe::TryReadField(&cam->Location, loc)) {
                out = loc;
                return true;
            }
        }
    }
    return false;
}

bool TryGetRemoteLocation(const Client::Player *remote, Classes::FVector &out) {
    if (!remote) {
        return false;
    }
    if (auto *actor = PlayerRemoteActor(remote)) {
        if (MeSdk::Safe::IsPlausibleUObject(
                const_cast<Classes::AActor *>(actor))) {
            Classes::FVector loc = {};
            if (MeSdk::Safe::Gameplay::TryReadActorLocation(
                    const_cast<Classes::AActor *>(actor), loc)) {
                out = loc;
                return true;
            }
        }
    }
    if (remote->ToTime > 0) {
        out = remote->ToPacket.Position;
        return true;
    }
    return false;
}

float HostRemoteDistanceMeters(const Client::Player *remote) {
    Classes::FVector host = {};
    Classes::FVector rem = {};
    if (!TryGetLocalHostLocation(host) || !TryGetRemoteLocation(remote, rem)) {
        return -1.f;
    }
    return MeSdk::Distance(host, rem);
}

Client::Player *FindNearestRemote(float maxMeters, float *outDist) {
    Classes::FVector host = {};
    if (!TryGetLocalHostLocation(host)) {
        return nullptr;
    }

    Client::Player *best = nullptr;
    float bestDist = maxMeters;
    players.Mutex.lock_shared();
    for (auto *p : players.List) {
        if (!p || !LevelsCompatible(p->Level, client.Level)) {
            continue;
        }
        Classes::FVector rem = {};
        if (!TryGetRemoteLocation(p, rem)) {
            continue;
        }
        const float d = MeSdk::Distance(host, rem);
        if (d >= 0.f && d < bestDist) {
            bestDist = d;
            best = p;
        }
    }
    players.Mutex.unlock_shared();
    if (best && outDist) {
        *outDist = bestDist;
    }
    return best;
}

void TrySendNearestInteract(const char *kind) {
    if (!connected.load() || loading.load() || chat.Focused) {
        return;
    }
    const auto now = GetTickCount64();
    if (now - lastInteractSentMs < 1500ull) {
        return;
    }

    float dist = 0.f;
    auto *target = FindNearestRemote(interactMaxMeters, &dist);
    if (!target) {
        return;
    }

    const char *k = (kind && *kind) ? kind : "wave";
    if (!SendJsonMessage({
            {"type", "interact"},
            {"from", client.Id},
            {"to", target->Id},
            {"kind", k},
            {"dist", dist},
        })) {
        return;
    }

    lastInteractSentMs = now;
    char buffer[0x120];
    snprintf(buffer, sizeof(buffer), "[Interact] you %s %s (%.1fm)", k,
             target->Name.c_str(), dist);
    AddChatMessage(buffer);
    ClientLogf("client: interact sent kind=%s to=%x dist=%.2f", k, target->Id,
               dist);
}

void QueueSpawnPlayerIfReady(Client::Player *player) {
    if (!player) {
        return;
    }
    if (PlayerHasRemoteVisual(player)) {
        return;
    }

    // When hosted gameplay is live, push into engine.dll's spawn queue
    // immediately.  That queue is drained on the D3D9 main thread by
    // MMOD_EngineDrainSpawnQueue (EndScene) — TickHook may never fire.
    if (Engine::IsHostedGameplayLive() && !loading.load() &&
        connected.load() && player->Level == client.Level) {
        TrySpawnPlayerDirect(player);
        return;
    }

    std::lock_guard<std::mutex> lock(g_spawnPendingMutex);
    if (g_spawnPendingPlayers.insert(player->Id).second) {
        ClientLogf("client: spawn_pending_enqueue id=%x", player->Id);
    }
}

bool TrySpawnPlayerDirect(Client::Player *player) {
    if (!player) return false;
    SyncActorFromStableSlot(player);
    if (PlayerHasRemoteVisual(player)) return false;
    if (!LevelsCompatible(player->Level, client.Level)) return false;
    if (static_cast<int>(player->Character) < 0 ||
        player->Character >= Engine::Character::Max) return false;

    // Avoid re-queueing every tick while drain is still waiting on UE3.
    static std::mutex inFlightMutex;
    static std::unordered_map<unsigned int, ULONGLONG> inFlightSince;
    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(inFlightMutex);
        const auto it = inFlightSince.find(player->Id);
        if (it != inFlightSince.end() && (now - it->second) < 5000) {
            // Do NOT log every tick — flooded client.log/console and hitch.
            return false;
        }
        inFlightSince[player->Id] = now;
    }

    const auto visualCharacter = RemoteVisualCharacter(player);
    if (visualCharacter != player->Character) {
        ClientLogf("client: spawn remap id=%x character=%d->%d",
                   player->Id, static_cast<int>(player->Character),
                   static_cast<int>(visualCharacter));
    }

    static std::unordered_map<unsigned int, ULONGLONG> s_lastQueueLog;
    const bool logQueue = [&]() {
        const auto it = s_lastQueueLog.find(player->Id);
        if (it != s_lastQueueLog.end() && (now - it->second) < 5000) {
            return false;
        }
        s_lastQueueLog[player->Id] = now;
        return true;
    }();

    if (logQueue) {
        ClientLogf("client: spawn queue id=%x character=%d", player->Id,
                   static_cast<int>(visualCharacter));
    }
    auto &slot = StableSpawnSlotFor(player->Id);
    if (logQueue) {
        ClientLogf("client: spawn out id=%x slot=%p out=%p", player->Id, &slot,
                   &slot.actor);
    }
    Engine::SpawnCharacter(visualCharacter, slot.actor);
    SyncActorFromStableSlot(player);
    if (logQueue) {
        ClientLogf("client: spawn queued id=%x actor=%p (pending drain)",
                   player->Id, player->Actor);
    }
    if (PlayerHasRemoteVisual(player)) {
        std::lock_guard<std::mutex> lock(inFlightMutex);
        inFlightSince.erase(player->Id);
        return true;
    }
    return false;
}

float LerpFloat(float from, float to, float alpha) {
    return from + (to - from) * alpha;
}

Classes::FVector LerpVector(const Classes::FVector &from,
                                   const Classes::FVector &to, float alpha) {
    return {LerpFloat(from.X, to.X, alpha), LerpFloat(from.Y, to.Y, alpha),
            LerpFloat(from.Z, to.Z, alpha)};
}

unsigned short LerpYaw(unsigned short from, unsigned short to,
                              float alpha) {
    auto delta = static_cast<int>(to) - static_cast<int>(from);
    if (delta > 0x8000) {
        delta -= 0x10000;
    } else if (delta < -0x8000) {
        delta += 0x10000;
    }

    return static_cast<unsigned short>(
        (static_cast<int>(from) + static_cast<int>(delta * alpha)) & 0xFFFF);
}

void LerpBoneBuffer(Classes::FBoneAtom *dest,
                             const Classes::FBoneAtom *from,
                             const Classes::FBoneAtom *to, float alpha) {
    const auto destBase = reinterpret_cast<byte *>(dest);
    const auto fromBase = reinterpret_cast<const byte *>(from);
    const auto toBase = reinterpret_cast<const byte *>(to);

    for (auto i = 0; i < ARRAYSIZE(CompressedBoneOffsets); ++i) {
        const auto offset = CompressedBoneOffsets[i];
        *reinterpret_cast<float *>(destBase + offset) = LerpFloat(
            *reinterpret_cast<const float *>(fromBase + offset),
            *reinterpret_cast<const float *>(toBase + offset), alpha);
    }
}

float GetInterpolationAlpha(unsigned long long fromTime,
                                   unsigned long long toTime,
                                   unsigned long long renderTime) {
    if (toTime <= fromTime) {
        return 1.0f;
    }

    if (renderTime <= fromTime) {
        return 0.0f;
    }

    if (renderTime >= toTime) {
        return 1.0f;
    }

    return static_cast<float>(renderTime - fromTime) /
           static_cast<float>(toTime - fromTime);
}

// V18/V19: per-remote UDP gaps → adaptive interpolation delay.
static float g_udpIntervalEmaMs = 50.0f;

static int ClampInterpDelayTarget(int target) {
    if (target < interpolationDelayBaseMs) {
        target = interpolationDelayBaseMs;
    }
    if (target < 40) {
        target = 40;
    }
    if (target > 200) {
        target = 200;
    }
    return target;
}

static int RttForInterpMs() {
    int rtt = latencyMs;
    if (rtt < 0) {
        rtt = 40;
    }
    if (rtt > 300) {
        rtt = 300;
    }
    return rtt;
}

static void UpdatePlayerInterpDelay(Client::Player *player) {
    if (!player) {
        return;
    }
    if (!interpolationEnabled || !interpolationDelayAuto) {
        player->InterpDelayMs = 0;
        return;
    }
    const int rtt = RttForInterpMs();
    // 1.25× mean interval + 0.35× recent peak jitter + ½ RTT.
    int target =
        static_cast<int>(player->UdpIntervalEmaMs * 1.25f +
                         player->UdpJitterPeakMs * 0.35f) +
        (rtt / 2);
    target = ClampInterpDelayTarget(target);
    if (player->InterpDelayMs <= 0) {
        player->InterpDelayMs = target;
    } else if (target > player->InterpDelayMs) {
        // V20: rise fast on jitter spikes.
        player->InterpDelayMs = (player->InterpDelayMs * 3 + target) / 4;
    } else {
        // Fall faster when the link calms (was 7/8 — laggy after spikes).
        player->InterpDelayMs = (player->InterpDelayMs * 5 + target) / 6;
    }
    if (player->InterpDelayMs < 0) {
        player->InterpDelayMs = 0;
    }
    if (player->InterpDelayMs > 250) {
        player->InterpDelayMs = 250;
    }
}

static void NoteRemoteUdpIntervalMs(Client::Player *player, float gapMs) {
    if (gapMs < 8.0f || gapMs > 400.0f) {
        return;
    }
    g_udpIntervalEmaMs = g_udpIntervalEmaMs * 0.85f + gapMs * 0.15f;
    if (player) {
        player->UdpIntervalEmaMs =
            player->UdpIntervalEmaMs * 0.85f + gapMs * 0.15f;
        if (gapMs > player->UdpJitterPeakMs) {
            player->UdpJitterPeakMs = gapMs;
        } else {
            player->UdpJitterPeakMs =
                player->UdpJitterPeakMs * 0.92f + gapMs * 0.08f;
        }
        UpdatePlayerInterpDelay(player);
    }
}

static void UpdateAdaptiveInterpolationDelay() {
    if (!interpolationEnabled || !interpolationDelayAuto) {
        return;
    }
    const int rtt = RttForInterpMs();
    // Global (UI): same formula on aggregate EMA; also lift to busiest peer.
    int target =
        static_cast<int>(g_udpIntervalEmaMs * 1.5f) + (rtt / 2);
    target = ClampInterpDelayTarget(target);
    int peerMax = 0;
    players.Mutex.lock_shared();
    for (const auto &p : players.List) {
        if (p && p->InterpDelayMs > peerMax) {
            peerMax = p->InterpDelayMs;
        }
    }
    players.Mutex.unlock_shared();
    if (peerMax > target) {
        target = peerMax;
    }
    const int prev = interpolationDelayMs;
    if (target > interpolationDelayMs) {
        interpolationDelayMs = (interpolationDelayMs * 3 + target) / 4;
    } else {
        interpolationDelayMs = (interpolationDelayMs * 5 + target) / 6;
    }
    if (interpolationDelayMs < 0) {
        interpolationDelayMs = 0;
    }
    if (interpolationDelayMs > 250) {
        interpolationDelayMs = 250;
    }
    static unsigned long long s_lastAdaptLogMs = 0;
    const auto now = GetTickCount64();
    if ((now - s_lastAdaptLogMs) > 3000ull &&
        abs(interpolationDelayMs - prev) >= 8) {
        s_lastAdaptLogMs = now;
        ClientLogf("client: interp delay auto %d ms (pkt=%.0f rtt=%d base=%d "
                   "peerMax=%d)",
                   interpolationDelayMs, g_udpIntervalEmaMs, rtt,
                   interpolationDelayBaseMs, peerMax);
    }
}

// V15: Td EMovement>=2 is often sprint (15), not fall. Only PHYS/MOVE Falling.
static bool RemoteIsFallingBones(const Client::Player *player) {
    if (!player || !player->HasRemoteMoveState) {
        return false;
    }
    return player->RemotePhysics == 2 || player->RemoteMovementState == 2;
}

static bool RemoteIsGroundWalkBones(const Client::Player *player) {
    if (!player || !player->HasRemoteMoveState || RemoteIsFallingBones(player)) {
        return false;
    }
    // MOVE_Walking (1) or Td grounded moves (sprint=15, land variants, …).
    return player->RemoteMovementState == 1 ||
           player->RemoteMovementState >= 3;
}

// V16: stance bucket for live-UDP bone crossfade (matches MEBC Idle/Walk/Fall).
static int RemoteClipBucket(const Client::Player *player) {
    if (RemoteIsFallingBones(player)) {
        return 2;
    }
    if (RemoteIsGroundWalkBones(player)) {
        return 1;
    }
    if (player && player->HasRemoteVelocity) {
        const float vx = player->RemoteVelocity.X;
        const float vy = player->RemoteVelocity.Y;
        const float vz = player->RemoteVelocity.Z;
        if ((vx * vx + vy * vy + vz * vz) > 25.0f) {
            return 1;
        }
    }
    return 0;
}

// V16b: commit stance only after hold — Falling enter/leave is short.
static int RemoteClipBucketWithHysteresis(Client::Player *player,
                                         unsigned long long nowMs) {
    const int raw = RemoteClipBucket(player);
    if (!player->HasClipBucket) {
        player->ClipBucket = raw;
        player->HasClipBucket = true;
        player->PendingClipBucket = raw;
        player->PendingClipBucketSinceMs = nowMs;
        return raw;
    }
    if (raw == player->ClipBucket) {
        player->PendingClipBucket = raw;
        player->PendingClipBucketSinceMs = nowMs;
        return player->ClipBucket;
    }
    if (raw != player->PendingClipBucket) {
        player->PendingClipBucket = raw;
        player->PendingClipBucketSinceMs = nowMs;
        return player->ClipBucket;
    }
    const bool fallEdge =
        (raw == 2 || player->ClipBucket == 2);
    const unsigned long long holdMs = fallEdge ? 40ull : 150ull;
    if ((nowMs - player->PendingClipBucketSinceMs) < holdMs) {
        return player->ClipBucket;
    }
    return raw;
}

void BuildRenderedPacket(Client::Player *player, Client::PACKET &packet) {
    packet = player->LastPacket;

    if (!interpolationEnabled || !player->ToTime) {
        return;
    }

    const auto renderTime =
        GetTickCount64() -
        static_cast<unsigned long long>(
            (player->InterpDelayMs > 0) ? player->InterpDelayMs
                                       : interpolationDelayMs);
    const auto alpha =
        GetInterpolationAlpha(player->FromTime, player->ToTime, renderTime);

    packet.Id = player->ToPacket.Id;

    // Large From→To jumps (respawn / SoftProbe slam): snap — do not lerp
    // through walls over many frames.
    const float jdx =
        player->ToPacket.Position.X - player->FromPacket.Position.X;
    const float jdy =
        player->ToPacket.Position.Y - player->FromPacket.Position.Y;
    const float jdz =
        player->ToPacket.Position.Z - player->FromPacket.Position.Z;
    const float jump2 = jdx * jdx + jdy * jdy + jdz * jdz;
    const float snap = poseSnapUu > 1.f ? poseSnapUu : 350.f;
    const bool bigJump = jump2 > (snap * snap);
    // V16/V16b: Idle↔Walk↔Fall bone crossfade with stance hysteresis.
    constexpr unsigned long long kBoneXfadeMs = 180ull;
    const auto nowStance = GetTickCount64();
    const int stanceBucket =
        RemoteClipBucketWithHysteresis(player, nowStance);
    const bool fallInterrupt =
        (stanceBucket == 2 || player->ClipBucket == 2);
    if (!bigJump && player->HasClipBucket &&
        stanceBucket != player->ClipBucket && player->HasLastRenderedBones &&
        (!player->HasBoneXfade || fallInterrupt)) {
        player->BoneXfadeFrom = player->LastRenderedBones;
        player->HasBoneXfade = true;
        player->BoneXfadeStartMs = nowStance;
        static unsigned long long s_lastXfadeLogMs = 0;
        if (nowStance - s_lastXfadeLogMs > 400ull) {
            s_lastXfadeLogMs = nowStance;
            ClientLogf("client: remote bone stance xfade id=%x from=%d to=%d",
                       player->Id, player->ClipBucket, stanceBucket);
        }
    }
    player->ClipBucket = stanceBucket;
    player->HasClipBucket = true;
    // B3-lite: real fall prefers latest bones (no From→To blur), unless the
    // peer is settling after a real fast fall — then keep last walk pose so
    // floor clamp does not leave crumpled fall limbs. SoftProbe FallDrop
    // hovers at host+1000 with V≈0; that must NOT trip this (V12b).
    // V15: do not treat Td sprint (EMovement=15) as parkourBones.
    const bool parkourBones = RemoteIsFallingBones(player);
    bool groundedParkourOverride = false;
    if (parkourBones && player->HasRemoteVelocity) {
        const float vx = player->RemoteVelocity.X;
        const float vy = player->RemoteVelocity.Y;
        const float vz = player->RemoteVelocity.Z;
        const float horiz2 = vx * vx + vy * vy;
        const auto nowFall = GetTickCount64();
        if (fabsf(vz) > 400.0f) {
            player->LastFastFallMs = nowFall;
        }
        if (player->HasLastGoodBones && player->LastFastFallMs != 0ull &&
            (nowFall - player->LastFastFallMs) < 800ull &&
            fabsf(vz) < 120.0f && horiz2 < 22500.0f) {
            groundedParkourOverride = true;
        }
    }

    if (bigJump) {
        packet.Position = player->ToPacket.Position;
        packet.Yaw = player->ToPacket.Yaw;
        memcpy(packet.Bones, player->ToPacket.Bones, sizeof(packet.Bones));
        player->HasBoneSmooth = false;
    } else {
        packet.Position = LerpVector(player->FromPacket.Position,
                                     player->ToPacket.Position, alpha);
        packet.Yaw =
            LerpYaw(player->FromPacket.Yaw, player->ToPacket.Yaw, alpha);
        if (parkourBones && !groundedParkourOverride) {
            memcpy(packet.Bones, player->ToPacket.Bones, sizeof(packet.Bones));
            player->HasBoneSmooth = false;
        } else if (groundedParkourOverride) {
            memcpy(packet.Bones, player->LastGoodBonePose.Bones,
                   sizeof(packet.Bones));
            player->HasBoneSmooth = false;
            static unsigned long long s_lastGroundBoneLogMs = 0;
            const auto nowG = GetTickCount64();
            if (nowG - s_lastGroundBoneLogMs > 1000ull) {
                s_lastGroundBoneLogMs = nowG;
                ClientLogf("client: remote bones grounded override id=%x "
                           "move=%d",
                           player->Id, player->RemoteMovementState);
            }
        } else {
            LerpBoneBuffer(packet.Bones, player->FromPacket.Bones,
                           player->ToPacket.Bones, alpha);
        }
    }

    // Bone EMA after network lerp (limbs); skip on bigJump / live fall snap.
    // Walk/sprint: boneSmoothWalkAlpha. Idle: stronger idle alpha.
    const bool skipBoneEma =
        bigJump || (parkourBones && !groundedParkourOverride);
    if (boneSmoothEnabled && !skipBoneEma) {
        float a = boneSmoothAlpha;
        const float vx = player->RemoteVelocity.X;
        const float vy = player->RemoteVelocity.Y;
        const float vz = player->RemoteVelocity.Z;
        const float spd2 = vx * vx + vy * vy + vz * vz;
        const bool walking =
            RemoteIsGroundWalkBones(player) ||
            (player->HasRemoteVelocity && spd2 > 25.0f &&
             !RemoteIsFallingBones(player));
        const bool nearlyIdleRemote =
            (!player->HasRemoteVelocity || spd2 <= 25.0f) && !walking &&
            (!player->HasRemoteMoveState || player->RemoteMovementState == 0);
        if (walking) {
            a = boneSmoothWalkAlpha;
        } else if (nearlyIdleRemote) {
            a = boneSmoothIdleAlpha;
        }
        if (player->BoneFirstLiveBoostFrames > 0) {
            a = 1.0f;
            --player->BoneFirstLiveBoostFrames;
        }
        if (a < 0.15f) {
            a = 0.15f;
        }
        if (a > 1.0f) {
            a = 1.0f;
        }
        const auto destBase = reinterpret_cast<byte *>(packet.Bones);
        for (auto i = 0; i < ARRAYSIZE(CompressedBoneOffsets); ++i) {
            float *slot = reinterpret_cast<float *>(destBase +
                                                    CompressedBoneOffsets[i]);
            if (!player->HasBoneSmooth) {
                player->BoneSmooth[i] = *slot;
            } else {
                player->BoneSmooth[i] =
                    LerpFloat(player->BoneSmooth[i], *slot, a);
                *slot = player->BoneSmooth[i];
            }
        }
        player->HasBoneSmooth = true;
    }

    // V16: short stance crossfade after network lerp/EMA (real UDP remotes).
    if (player->HasBoneXfade && !bigJump) {
        const auto nowXf = GetTickCount64();
        float t = static_cast<float>(nowXf - player->BoneXfadeStartMs) /
                  static_cast<float>(kBoneXfadeMs);
        if (t >= 1.0f) {
            player->HasBoneXfade = false;
        } else {
            if (t < 0.0f) {
                t = 0.0f;
            }
            Client::PACKET target = packet;
            LerpBoneBuffer(packet.Bones, player->BoneXfadeFrom.Bones,
                           target.Bones, t);
        }
    }

    // V17/V20: UDP gap — ease bones toward LastGood. Horizon ≈ 2× peer
    // interval (min 120, max 280) so quiet links settle sooner.
    unsigned long long staleAfterMs = 180ull;
    if (player->UdpIntervalEmaMs > 1.0f) {
        staleAfterMs = static_cast<unsigned long long>(
            player->UdpIntervalEmaMs * 2.0f);
        if (staleAfterMs < 120ull) {
            staleAfterMs = 120ull;
        }
        if (staleAfterMs > 280ull) {
            staleAfterMs = 280ull;
        }
    }
    if (!bigJump && !parkourBones && !player->HasBoneXfade &&
        player->HasLastGoodBones &&
        renderTime > player->ToTime + staleAfterMs) {
        float staleT =
            static_cast<float>(renderTime - player->ToTime - staleAfterMs) /
            220.0f;
        if (staleT > 1.0f) {
            staleT = 1.0f;
        }
        if (staleT > 0.0f) {
            Client::PACKET cur = packet;
            LerpBoneBuffer(packet.Bones, cur.Bones,
                           player->LastGoodBonePose.Bones, staleT);
            static unsigned long long s_lastStaleLogMs = 0;
            const auto nowS = GetTickCount64();
            if (nowS - s_lastStaleLogMs > 1500ull) {
                s_lastStaleLogMs = nowS;
                ClientLogf("client: remote bones stale settle id=%x age=%ums "
                           "after=%ums",
                           player->Id,
                           static_cast<unsigned>(renderTime - player->ToTime),
                           static_cast<unsigned>(staleAfterMs));
            }
        }
    }

    player->LastRenderedBones = packet;
    player->HasLastRenderedBones = true;

    // Mesh V13: no synthetic wave/idle compressed-float nudges (weird limbs).
    // WaveGestureFrames only gates face-host yaw below / in ApplyRemote.
    if (player->WaveGestureFrames > 0) {
        --player->WaveGestureFrames;
    }

    // Phase4/V20: past last sample, coast with velocity but fade so a 200ms
    // gap does not keep full sprint into walls (clamp still hard-rejects).
    if (!bigJump && player->HasRemoteVelocity && renderTime > player->ToTime) {
        float dtSec =
            static_cast<float>(renderTime - player->ToTime) / 1000.f;
        constexpr float kExtrapCapSec = 0.20f;
        if (dtSec > kExtrapCapSec) {
            dtSec = kExtrapCapSec;
        }
        float fade = 1.0f - (dtSec / kExtrapCapSec);
        if (fade < 0.0f) {
            fade = 0.0f;
        }
        // Square fade: early coast stays useful; late coast nearly stops.
        fade *= fade;
        packet.Position.X += player->RemoteVelocity.X * dtSec * fade;
        packet.Position.Y += player->RemoteVelocity.Y * dtSec * fade;
        packet.Position.Z += player->RemoteVelocity.Z * dtSec * fade;
    }
}

// Write network target with optional EMA toward desired (jitter filter).
// snapYaw: idle remotes always take packet yaw (body faces turn without lag).
static void WriteSmoothedRemotePose(Classes::AActor *actor,
                                    const Classes::FVector &desired,
                                    unsigned short desiredYaw,
                                    bool snapYaw) {
    if (!actor) {
        return;
    }

    Classes::FVector cur = desired;
    const bool haveCur =
        MeSdk::Safe::Gameplay::TryReadActorLocation(actor, cur);
    Classes::FVector out = desired;
    unsigned short outYaw = desiredYaw;

    if (poseSmoothEnabled && haveCur) {
        const float dx = desired.X - cur.X;
        const float dy = desired.Y - cur.Y;
        const float dz = desired.Z - cur.Z;
        const float dist2 = dx * dx + dy * dy + dz * dz;
        const float snap = poseSnapUu > 1.f ? poseSnapUu : 350.f;
        if (dist2 <= (snap * snap)) {
            float a = poseSmoothAlpha;
            if (a < 0.15f) {
                a = 0.15f;
            }
            if (a > 1.0f) {
                a = 1.0f;
            }
            out = LerpVector(cur, desired, a);
            if (!snapYaw) {
                Classes::FRotator crot = {};
                if (MeSdk::Safe::Gameplay::TryReadActorRotation(actor, crot)) {
                    const auto cyaw =
                        static_cast<unsigned short>(crot.Yaw & 0xFFFF);
                    outYaw = LerpYaw(cyaw, desiredYaw, a);
                }
            }
        }
    }

    MeSdk::Safe::Gameplay::TryWriteActorLocation(actor, out);
    const Classes::FRotator rot = {0, outYaw, 0};
    MeSdk::Safe::Gameplay::TryWriteActorRotation(actor, rot);
}

// Soft XY separation from local host. Live pose writes only — never use on
// disconnect/park (KI-2026-012). outHostPush accumulates equal-opposite nudge.
static void SoftSeparateRemoteFromHost(Classes::FVector &remotePos,
                                       const Classes::FVector &hostPos,
                                       float radius, float strength,
                                       Classes::FVector *outHostPush) {
    if (radius <= 1.0f || strength <= 0.0f) {
        return;
    }
    const float dx = remotePos.X - hostPos.X;
    const float dy = remotePos.Y - hostPos.Y;
    const float dist2 = dx * dx + dy * dy;
    const float r2 = radius * radius;
    if (dist2 >= r2 || dist2 < 1.0f) {
        return;
    }
    const float dist = sqrtf(dist2);
    const float push = (radius - dist) * strength;
    const float nx = dx / dist;
    const float ny = dy / dist;
    remotePos.X += nx * push;
    remotePos.Y += ny * push;
    if (outHostPush) {
        // Smaller reverse so local physics is not overpowered each tick.
        outHostPush->X -= nx * push * 0.35f;
        outHostPush->Y -= ny * push * 0.35f;
    }
}

// Geometric world clamp — live pose only. Never SetPhysics / bCollideWorld.
// ProcessEvent Actor::Trace/FastTrace hung EndScene spawn drain (2026-07-20);
// SoftProbe FallDrop/WallSlam use host-relative snaps instead.
// Corridor axis: prefer pawn body yaw. Camera look (Controller) is for Follow
// UDP / FOV — using it here walked Kate into solar panels when the host glanced
// sideways. If body yaw is near-zero junk but look is live, fall back to look.
static unsigned short ReadHostCorridorYawUu(Classes::ATdPlayerPawn *localPawn) {
    unsigned short bodyYaw = 0;
    bool haveBody = false;
    if (localPawn) {
        Classes::FRotator prot = {};
        if (MeSdk::Safe::Gameplay::TryReadActorRotation(localPawn, prot)) {
            bodyYaw = static_cast<unsigned short>(prot.Yaw % 0x10000);
            haveBody = true;
        }
    }
    unsigned short lookYaw = bodyYaw;
    bool haveLook = false;
    if (auto *pc = Engine::GetPlayerController(false)) {
        Classes::FRotator crot = {};
        if (MeSdk::Safe::TryReadField(&pc->Rotation, crot)) {
            lookYaw = static_cast<unsigned short>(crot.Yaw % 0x10000);
            haveLook = true;
        }
    }
    if (haveBody && bodyYaw != 0) {
        return bodyYaw;
    }
    if (haveLook) {
        return lookYaw;
    }
    return bodyYaw;
}

// Keep remotes in a corridor along host body — tutorial_p walkway is narrow;
// ±160 lateral stand-offs put Kate into solar panels / fence (no Trace).
static void ClampRemoteToHostCorridor(Classes::ATdPlayerPawn *localPawn,
                                      Classes::FVector &desired) {
    if (!worldClampEnabled || !localPawn || worldClampMaxLateral < 1.0f) {
        return;
    }
    if (!MeSdk::Safe::IsPlausibleUObject(localPawn)) {
        return;
    }
    const Classes::FVector hostPos = localPawn->Location;
    const float rad =
        (static_cast<float>(ReadHostCorridorYawUu(localPawn)) / 65536.0f) *
        6.28318530718f;
    const float fx = cosf(rad);
    const float fy = sinf(rad);
    const float rx = -sinf(rad);
    const float ry = cosf(rad);
    float dx = desired.X - hostPos.X;
    float dy = desired.Y - hostPos.Y;
    float along = dx * fx + dy * fy;
    float lat = dx * rx + dy * ry;
    const float maxLat = worldClampMaxLateral;
    bool changed = false;
    // V17: soft lateral pull toward corridor (hard snap only if still far out).
    if (lat > maxLat) {
        lat = LerpFloat(lat, maxLat, 0.45f);
        if (lat > maxLat) {
            lat = maxLat;
        }
        changed = true;
    } else if (lat < -maxLat) {
        lat = LerpFloat(lat, -maxLat, 0.45f);
        if (lat < -maxLat) {
            lat = -maxLat;
        }
        changed = true;
    }
    const float hostMag = fabsf(hostPos.X) + fabsf(hostPos.Y);
    const float desMag = fabsf(desired.X) + fabsf(desired.Y);
    // Near-origin packets → reseed onto corridor Cam stand-off (260). SoftProbe
    // FollowDistance~70 pulls SoftProbe closer; do NOT reseed every remote to 70
    // (stacked SoftProbe+Cam at one tile → nametags only / invisible Kate).
    if (hostMag > 500.0f && desMag < 80.0f) {
        along = 260.0f;
        lat = 0.0f;
        changed = true;
    } else if (hostMag > 500.0f) {
        if (along > 360.0f) {
            along = LerpFloat(along, 260.0f, 0.40f);
            if (along > 360.0f) {
                along = 260.0f;
            }
            changed = true;
        } else if (along < -40.0f) {
            along = LerpFloat(along, 20.0f, 0.40f);
            if (along < -40.0f) {
                along = 20.0f;
            }
            changed = true;
        }
    }
    // Near-host remotes (SoftProbe): prefer slightly ahead so soft-separate
    // does not park Kate beside Faith inside rooftop props.
    const float nearR = softCollisionRadius * 2.5f;
    const float nearDx = desired.X - hostPos.X;
    const float nearDy = desired.Y - hostPos.Y;
    if ((nearDx * nearDx + nearDy * nearDy) < (nearR * nearR) && along < 20.0f &&
        along > -80.0f) {
        along = 20.0f;
        changed = true;
    }
    if (!changed) {
        return;
    }
    desired.X = hostPos.X + fx * along + rx * lat;
    desired.Y = hostPos.Y + fy * along + ry * lat;
    static unsigned long long s_lastCorridorLogMs = 0;
    const auto now = GetTickCount64();
    if ((now - s_lastCorridorLogMs) > 1500ull) {
        s_lastCorridorLogMs = now;
        ClientLogf("client: world clamp corridor lat=%.0f along=%.0f", lat,
                   along);
    }
}

static void ClampRemoteDesiredPosition(Classes::ATdPlayerPawn *localPawn,
                                       const Classes::FVector &from,
                                       Classes::FVector &desired) {
    if (!worldClampEnabled) {
        return;
    }
    static unsigned long long s_lastFloorLogMs = 0;
    static unsigned long long s_lastWallLogMs = 0;
    const auto now = GetTickCount64();

    if (localPawn && MeSdk::Safe::IsPlausibleUObject(localPawn)) {
        const float hostZ = localPawn->Location.Z;
        // SoftProbe FallDrop is hostZ+1000. Standing UDP often carries Faith
        // TargetMeshTranslationZ (~94). Clamping that to hostZ+2 buried Kate
        // (spawn PrePivot.Z=94) in the rooftop — nametags only on live-mesh.
        constexpr float kMeshTz = 94.0f;
        const float floorCeil = hostZ + worldClampUp + kMeshTz;
        if (desired.Z > floorCeil) {
            const float targetZ = hostZ + kMeshTz;
            // V15: soft approach toward floor (poseSmooth also damps). Hard
            // snap only if still wildly high so SoftProbe converges.
            desired.Z = LerpFloat(desired.Z, targetZ, 0.55f);
            if (desired.Z > floorCeil) {
                desired.Z = targetZ;
            }
            if ((now - s_lastFloorLogMs) > 500ull) {
                s_lastFloorLogMs = now;
                ClientLogf("client: world clamp floor z=%.0f", desired.Z);
            }
        }
    }

    const float dx = desired.X - from.X;
    const float dy = desired.Y - from.Y;
    const float horiz2 = dx * dx + dy * dy;
    // Large single-step XY (SoftProbe WallSlam ~900 UU) → hold previous XY.
    // Keep hard reject — soft blend would creep remotes through the slam.
    if (horiz2 > (200.0f * 200.0f)) {
        desired.X = from.X;
        desired.Y = from.Y;
        if ((now - s_lastWallLogMs) > 500ull) {
            s_lastWallLogMs = now;
            ClientLogf("client: world clamp wall xy=(%.0f,%.0f)", desired.X,
                       desired.Y);
        }
    }

    ClampRemoteToHostCorridor(localPawn, desired);
}

// Phase1: host Physics / fall / wall-hit telemetry (rate-limited).
static void LogHostPhysicsTelemetry(
    const MeSdk::Safe::Gameplay::PawnPoseSnapshot &pose) {
    constexpr int kPhysFalling = 2;
    constexpr int kPhysWalking = 1;

    static bool havePrev = false;
    static int prevPhysics = 0;
    static float prevHorizSpeed = 0.f;
    static float prevZ = 0.f;
    static float fallStartZ = 0.f;
    static float fallEnterHeight = 0.f;
    static unsigned long long s_lastWallLogMs = 0;

    const float hx = pose.velocity.X;
    const float hy = pose.velocity.Y;
    const float horizSpeed = sqrtf(hx * hx + hy * hy);
    const float vz = pose.velocity.Z;

    if (havePrev && pose.physics != prevPhysics) {
        ClientLogf("client: phys transition from=%d to=%d vz=%.0f", prevPhysics,
                   pose.physics, vz);
        if (pose.physics == kPhysFalling) {
            fallStartZ = pose.location.Z;
            fallEnterHeight = pose.enterFallingHeight;
            ClientLogf("client: fall start height=%.0f z=%.0f", fallEnterHeight,
                       fallStartZ);
        } else if (prevPhysics == kPhysFalling) {
            const float drop = fallStartZ - pose.location.Z;
            ClientLogf("client: fall land drop=%.0f impactVz=%.0f", drop, vz);
        }
    }

    // Wall heuristic: Walking + horizontal speed collapses, Z stable.
    if (havePrev && pose.physics == kPhysWalking && prevHorizSpeed > 200.0f &&
        horizSpeed < 50.0f && fabsf(pose.location.Z - prevZ) < 30.0f) {
        const auto now = GetTickCount64();
        if (now - s_lastWallLogMs > 500ull) {
            s_lastWallLogMs = now;
            ClientLogf("client: wall hit speedBefore=%.0f", prevHorizSpeed);
        }
    }

    prevPhysics = pose.physics;
    prevHorizSpeed = horizSpeed;
    prevZ = pose.location.Z;
    havePrev = true;
}

void ApplyInitialRemotePlayerPose(Client::Player *player) {
    auto *remote = PlayerRemoteActor(player);
    if (!player || !remote || !player->ToTime) {
        return;
    }
    if (!MeSdk::Safe::IsPlausibleUObject(remote)) {
        return;
    }

    static Client::PACKET rendered;
    BuildRenderedPacket(player, rendered);
    Classes::ATdPlayerPawn *localPawn = Engine::GetPlayerPawn(false);
    if (softCollisionEnabled && localPawn) {
        SoftSeparateRemoteFromHost(rendered.Position, localPawn->Location,
                                   softCollisionRadius,
                                   softCollisionStrength, nullptr);
    }
    Classes::FVector from = rendered.Position;
    MeSdk::Safe::Gameplay::TryReadActorLocation(remote, from);
    ClampRemoteDesiredPosition(localPawn, from, rendered.Position);
    MeSdk::Safe::Gameplay::TryWriteActorLocation(remote, rendered.Position);
    const Classes::FRotator rot = {0, rendered.Yaw, 0};
    MeSdk::Safe::Gameplay::TryWriteActorRotation(remote, rot);
    player->MaxZ = rendered.Position.Z;

    static std::unordered_set<uint32_t> loggedPose;
    if (loggedPose.insert(player->Id).second) {
        ClientLogf("client: remote pose applied id=%x loc=(%.0f,%.0f,%.0f)",
                   player->Id, rendered.Position.X, rendered.Position.Y,
                   rendered.Position.Z);
    }
}

static bool PacketHasBoneMotion(const Client::PACKET_COMPRESSED &packet) {
    for (auto i = 0; i < ARRAYSIZE(CompressedBoneOffsets); ++i) {
        if (packet.CompressedBones[i] != 0) {
            return true;
        }
    }
    return false;
}

void ApplyPacketSnapshot(Client::Player *player,
                                const Client::PACKET_COMPRESSED &packet,
                                bool hasVelocityTrailer, bool hasMoveTrailer) {
    const auto now = GetTickCount64();

    if (player->ToTime) {
        player->FromPacket = player->ToPacket;
        player->FromTime = player->ToTime;
    } else {
        player->FromPacket = {0};
        player->FromTime = now;
    }

    memcpy(&player->LastPacket, &packet,
           FIELD_OFFSET(Client::PACKET_COMPRESSED, CompressedBones));

    // Drop near-origin XY heartbeats (bots/seed) when we already have a real
    // stand-off — otherwise first pose (0,0,Z) + corridor lat-only left Kate
    // thousands of UU away on +X.
    if (player->ToTime) {
        const float nx = fabsf(packet.Position.X) + fabsf(packet.Position.Y);
        const float ox = fabsf(player->ToPacket.Position.X) +
                         fabsf(player->ToPacket.Position.Y);
        if (nx < 50.0f && ox > 200.0f) {
            player->LastPacket.Position.X = player->ToPacket.Position.X;
            player->LastPacket.Position.Y = player->ToPacket.Position.Y;
        }
    }

    // Location/yaw always update. Bone floats only when the peer sent real
    // compressed motion — all-zero bot packets must not overwrite default
    // bones or drive TransformBones (collapses Kate/etc. mesh to invisible).
    if (PacketHasBoneMotion(packet)) {
        const auto bonesBase =
            reinterpret_cast<byte *>(player->LastPacket.Bones);
        int nonZero = 0;
        for (auto i = 0; i < ARRAYSIZE(CompressedBoneOffsets); ++i) {
            *reinterpret_cast<float *>(bonesBase + CompressedBoneOffsets[i]) =
                static_cast<float>(packet.CompressedBones[i]) / 215.f;
            if (packet.CompressedBones[i] != 0) {
                ++nonZero;
            }
        }
        // Join seeds defaultBones with HasRemoteBoneMotion=true for rest pose;
        // still log once when the first non-zero UDP bone packet arrives.
        static std::unordered_set<uint32_t> loggedBonesApplied;
        if (loggedBonesApplied.insert(player->Id).second) {
            ClientLogf("client: remote bones applied id=%x (TransformBones on)",
                       player->Id);
            // Rest→live: snap bone EMA then boost follow so defaultBones does
            // not smear into the first walk pose (mesh visual V2).
            player->HasBoneSmooth = false;
            player->BoneFirstLiveBoostFrames = 4;
            ClientLogf("client: remote bones first live id=%x nonZero=%d",
                       player->Id, nonZero);
        } else {
            static bool loggedLiveStream = false;
            if (!loggedLiveStream && nonZero >= 50) {
                loggedLiveStream = true;
                ClientLogf("client: remote bones live stream id=%x nonZero=%d",
                           player->Id, nonZero);
            }
        }
        player->HasRemoteBoneMotion = true;
        // Cache walking/idle/sprint bone pose for grounded-fall override (V4/V15).
        // Only skip while truly falling — Td sprint must refresh LastGood.
        if (!hasMoveTrailer ||
            (packet.MovementState != 2 && packet.Physics != 2)) {
            memcpy(player->LastGoodBonePose.Bones, player->LastPacket.Bones,
                   sizeof(player->LastGoodBonePose.Bones));
            player->HasLastGoodBones = true;
        }
    }

    if (hasVelocityTrailer) {
        player->RemoteVelocity = packet.Velocity;
        player->HasRemoteVelocity = true;
    } else if (player->ToTime && now > player->ToTime) {
        const float dtSec =
            static_cast<float>(now - player->ToTime) / 1000.f;
        if (dtSec > 0.001f && dtSec < 0.5f) {
            player->RemoteVelocity.X =
                (packet.Position.X - player->ToPacket.Position.X) / dtSec;
            player->RemoteVelocity.Y =
                (packet.Position.Y - player->ToPacket.Position.Y) / dtSec;
            player->RemoteVelocity.Z =
                (packet.Position.Z - player->ToPacket.Position.Z) / dtSec;
            player->HasRemoteVelocity = true;
        }
    }

    if (hasMoveTrailer) {
        const int prevMove = player->RemoteMovementState;
        const int prevPhys = player->RemotePhysics;
        player->RemoteMovementState = packet.MovementState;
        player->RemotePhysics = packet.Physics;
        player->HasRemoteMoveState = true;
        // Leaving real fall: restart bone EMA + brief snap.
        const bool prevFall = prevPhys == 2 || prevMove == 2;
        const bool nowFall =
            packet.Physics == 2 || packet.MovementState == 2;
        if (prevFall && !nowFall) {
            player->HasBoneSmooth = false;
            player->BoneFirstLiveBoostFrames = 8;
            player->LastFastFallMs = 0;
        }
        if (prevMove != packet.MovementState) {
            static unsigned long long s_lastMoveLogMs = 0;
            if (now - s_lastMoveLogMs > 200ull) {
                s_lastMoveLogMs = now;
                ClientLogf("client: remote move state id=%x from=%d to=%d phys=%d",
                           player->Id, prevMove, packet.MovementState,
                           packet.Physics);
            }
        }
    }

    static bool loggedVelOnce = false;
    if (!loggedVelOnce) {
        const float speed2 =
            player->RemoteVelocity.X * player->RemoteVelocity.X +
            player->RemoteVelocity.Y * player->RemoteVelocity.Y +
            player->RemoteVelocity.Z * player->RemoteVelocity.Z;
        // Trailer proves phase4 wire even when bots are already near stand-off
        // (tick-delta speed often <10 UU/s). Derived velocity still needs motion.
        if (hasVelocityTrailer ||
            (player->HasRemoteVelocity && speed2 > 100.f)) {
            loggedVelOnce = true;
            ClientLogf("client: remote velocity active id=%x "
                       "trailer=%d speed=%.0f",
                       player->Id, hasVelocityTrailer ? 1 : 0,
                       sqrtf(speed2));
        }
    }

    player->ToPacket = player->LastPacket;
    // V18: track UDP inter-arrival for adaptive interp delay.
    if (player->ToTime && now > player->ToTime) {
        const unsigned long long gap = now - player->ToTime;
        if (gap >= 8ull && gap <= 400ull) {
            NoteRemoteUdpIntervalMs(player, static_cast<float>(gap));
        }
    }
    player->ToTime = now;

    // Continuous pose is applied by OnActorTick / OnBonesTick from LastPacket.
    // Only queue a one-shot initial pose (not every packet) — per-packet
    // QueueMainThreadTask flooded EndScene (console spam + hitch).
    if (!PlayerHasRemoteVisual(player) &&
        LevelsCompatible(player->Level, client.Level) &&
        !loading.load() && connected.load()) {
        QueueSpawnPlayerIfReady(player);
    } else if (PlayerHasRemoteVisual(player) && player->ToTime) {
        static std::unordered_set<uint32_t> initialPoseQueued;
        if (initialPoseQueued.insert(player->Id).second) {
            QueueClientEngineTask([player]() { ApplyInitialRemotePlayerPose(player); });
        }
    }
}
void TrackRemotePlayerSpawnResults() {
    static std::unordered_set<uint32_t> loggedSpawnOk;
    static std::unordered_set<uint32_t> loggedSpawnPending;

    players.Mutex.lock_shared();
    for (auto *p : players.List) {
        if (!p || !LevelsCompatible(p->Level, client.Level)) {
            continue;
        }

        SyncActorFromStableSlot(p);

        if (PlayerHasRemoteVisual(p)) {
            if (loggedSpawnOk.insert(p->Id).second) {
                ClientLogf("client: spawn ok id=%x actor=%p plausible=%d",
                           p->Id, p->Actor,
                           MeSdk::Safe::IsPlausibleUObject(p->Actor) ? 1 : 0);
                if (MeSdk::Safe::IsPlausibleUObject(p->Actor)) {
                    ApplyInitialRemotePlayerPose(p);
                }
            }
            continue;
        }

        if (loggedSpawnPending.insert(p->Id).second) {
            ClientLogf("client: spawn pending id=%x (waiting for actor)", p->Id);
        }
    }
    players.Mutex.unlock_shared();
}

void OnRemotePlayerMaintenanceTick() {
    // Always call TrySpawnPendingRemotePlayers so the activation fallback
    // can fire when hosted gameplay is not yet live.  Without this the
    // retry loop deadlocks: OnRemotePlayerMaintenanceTick gates on live,
    // but TrySpawnPendingRemotePlayers is the only path that requests
    // activation when !live.
    TrySpawnPendingRemotePlayers();
    TrackRemotePlayerSpawnResults();
}

namespace {

constexpr int kBoneCycleMaxFrames = 16;
// Named Mesh3p clip library (V14): Idle / Walking / Falling — not AnimTree.
constexpr int kBoneClipIdle = 0;
constexpr int kBoneClipWalking = 1;
constexpr int kBoneClipFalling = 2;
constexpr int kBoneClipCount = 3;

#pragma pack(push, 1)
struct BoneCycleHeader {
    char magic[4];
    unsigned short version;
    unsigned short boneCount;
    unsigned short frameCount;
    unsigned short frameBytes;
    // V14: EMovement bucket 0=Idle 1=Walking 2=Falling (was reserved=0).
    unsigned int clipId;
    // V16 MEBC v2: low-32 GetTickCount64 of newest sample (multi-bot phase align).
    unsigned int baseTickMs;
};
#pragma pack(pop)

static const char *BoneClipFileName(int clipId) {
    switch (clipId) {
    case kBoneClipIdle:
        return "mirroredge-bone-clip-Idle.bin";
    case kBoneClipWalking:
        return "mirroredge-bone-clip-Walking.bin";
    case kBoneClipFalling:
        return "mirroredge-bone-clip-Falling.bin";
    default:
        return "mirroredge-host-bones-cycle.bin";
    }
}

static int BoneClipIdFromHostMove(int movementState, int physics, float spd2) {
    // Td EMovement is not UE3 MOVE_* only — sprint is often 15 while PHYS_Walking
    // with near-zero Velocity (stuck / inject lag). Bucket by physics + move id.
    constexpr int kPhysFalling = 2;
    constexpr int kMoveFalling = 2;
    if (physics == kPhysFalling || movementState == kMoveFalling) {
        return kBoneClipFalling;
    }
    // Grounded Td moves beyond Idle/Walking (sprint, land variants, …).
    if (movementState >= 3) {
        return kBoneClipWalking;
    }
    // Classic MOVE_Walking with measurable speed.
    if (movementState == 1 && spd2 > 25.0f) {
        return kBoneClipWalking;
    }
    if (spd2 > 25.0f) {
        return kBoneClipWalking;
    }
    return kBoneClipIdle;
}

void WriteHostBoneCycleFile(const short *sampled, int boneCount, int clipId) {
    static short rings[kBoneClipCount][kBoneCycleMaxFrames][329] = {};
    static unsigned long long frameTicks[kBoneClipCount][kBoneCycleMaxFrames] = {};
    static int writeIdx[kBoneClipCount] = {};
    if (boneCount <= 0 || boneCount > 329 || !sampled) {
        return;
    }
    if (clipId < 0 || clipId >= kBoneClipCount) {
        clipId = kBoneClipIdle;
    }
    short(*ring)[329] = rings[clipId];
    unsigned long long *ticks = frameTicks[clipId];
    int &wIdx = writeIdx[clipId];
    const auto nowTick = GetTickCount64();
    memcpy(ring[wIdx % kBoneCycleMaxFrames], sampled,
           static_cast<size_t>(boneCount) * sizeof(short));
    ticks[wIdx % kBoneCycleMaxFrames] = nowTick;
    ++wIdx;
    const int frameCount =
        wIdx < kBoneCycleMaxFrames ? wIdx : kBoneCycleMaxFrames;
    const int frameBytes = boneCount * static_cast<int>(sizeof(short));

    auto writePath = [&](const char *fileName, unsigned int hdrClipId) {
        char path[MAX_PATH] = {};
        if (GetTempPathA(static_cast<DWORD>(sizeof(path)), path) == 0) {
            return;
        }
        strncat_s(path, fileName, _TRUNCATE);
        const HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                                        nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }
        BoneCycleHeader hdr = {};
        memcpy(hdr.magic, "MEBC", 4);
        hdr.version = 2;
        hdr.boneCount = static_cast<unsigned short>(boneCount);
        hdr.frameCount = static_cast<unsigned short>(frameCount);
        hdr.frameBytes = static_cast<unsigned short>(frameBytes);
        hdr.clipId = hdrClipId;
        hdr.baseTickMs = static_cast<unsigned int>(nowTick & 0xFFFFFFFFu);
        DWORD written = 0;
        WriteFile(file, &hdr, sizeof(hdr), &written, nullptr);
        unsigned short relMs[kBoneCycleMaxFrames] = {};
        unsigned long long t0 = 0;
        for (int i = 0; i < frameCount; ++i) {
            int slot = (wIdx - frameCount + i) % kBoneCycleMaxFrames;
            if (slot < 0) {
                slot += kBoneCycleMaxFrames;
            }
            WriteFile(file, ring[slot], static_cast<DWORD>(frameBytes),
                      &written, nullptr);
            if (i == 0) {
                t0 = ticks[slot];
            }
            unsigned long long d = ticks[slot] - t0;
            if (d > 65535ull) {
                d = 65535ull;
            }
            relMs[i] = static_cast<unsigned short>(d);
        }
        WriteFile(file, relMs, static_cast<DWORD>(frameCount) * sizeof(unsigned short),
                  &written, nullptr);
        CloseHandle(file);
    };

    writePath(BoneClipFileName(clipId), static_cast<unsigned int>(clipId));
    // Harness phase3 still expects the legacy cycle path. Prefer Walking;
    // if host never walks this session, Idle still seeds cycle.bin.
    const bool seedLegacyCycle =
        clipId == kBoneClipWalking ||
        (clipId == kBoneClipIdle && writeIdx[kBoneClipWalking] == 0);
    if (seedLegacyCycle) {
        writePath("mirroredge-host-bones-cycle.bin",
                  static_cast<unsigned int>(clipId));
    }

    static bool loggedClip[kBoneClipCount] = {};
    if (!loggedClip[clipId] && frameCount >= 4) {
        loggedClip[clipId] = true;
        ClientLogf("client: bone clip %s frames=%d "
                   "(dumped %%TEMP%%\\%s)",
                   clipId == kBoneClipIdle
                       ? "Idle"
                       : (clipId == kBoneClipWalking ? "Walking" : "Falling"),
                   frameCount, BoneClipFileName(clipId));
    }
    static bool loggedCycleOnce = false;
    if (!loggedCycleOnce && seedLegacyCycle && frameCount >= 4) {
        loggedCycleOnce = true;
        ClientLogf("client: local bones cycle frames=%d "
                   "(dumped %%TEMP%%\\mirroredge-host-bones-cycle.bin)",
                   frameCount);
    }
    static bool loggedMebcV2 = false;
    if (!loggedMebcV2 && frameCount >= 4) {
        loggedMebcV2 = true;
        ClientLogf("client: MEBC v2 timestamps baseTick=%u frames=%d",
                   static_cast<unsigned>(nowTick & 0xFFFFFFFFu), frameCount);
    }
}

} // namespace

void OnLocalPoseNetworkTick() {
    if (!Engine::IsHostedGameplayLive() || !udpSocket) {
        return;
    }

    static Client::PACKET_COMPRESSED packet;
    packet.Id = client.Id;

    // Cache-only pawn resolve — PawnList walk concurrent with EndScene warm
    // hung the frame after idle.done (2026-07-21).
    auto pawn = ResolveLocalPlayerPawn(false);

    const unsigned long long seedAgeMs = Engine::GetIdlePcSeedAgeMs();
    constexpr unsigned long long kSeedPoseCooldownMs = 1500ull;
    const bool seedPoseCooldown =
        seedAgeMs > 0 && seedAgeMs < kSeedPoseCooldownMs;

    auto applySeedHostPose = [&](Client::PACKET_COMPRESSED &out,
                                 bool &gotPose) {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        unsigned short yaw = 0;
        if (!Engine::TryGetSeedHostPose(&x, &y, &z, &yaw)) {
            return;
        }
        out.Position.X = x;
        out.Position.Y = y;
        out.Position.Z = z;
        out.Yaw = yaw;
        gotPose = true;
        std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
        g_harnessSnapshot.posX = x;
        g_harnessSnapshot.posY = y;
        g_harnessSnapshot.posZ = z;
        g_harnessSnapshot.yaw = yaw;
    };

    if (!pawn) {
        // Pull-model server only replies with peer LastPackets when we send.
        // Prefer camera location when pawn cache is empty so Follow targets
        // and peer heartbeats carry a real host pose in tutorial.
        packet = {};
        packet.Id = client.Id;
        bool gotCamera = false;
        // Engine writes seed pose AFTER the quiet window. Prefer seed whenever
        // present; live camera only after quiet (PlayerCamera during quiet hung).
        applySeedHostPose(packet, gotCamera);
        if (!gotCamera && !seedPoseCooldown) {
            if (auto *pc = ResolveLocalPlayerController(false)) {
                Classes::ACamera *cam = nullptr;
                if (MeSdk::Safe::TryReadField(&pc->PlayerCamera, cam) && cam &&
                    MeSdk::Safe::IsPlausibleUObject(cam)) {
                    Classes::FVector loc = {};
                    Classes::FRotator rot = {};
                    if (MeSdk::Safe::TryReadField(&cam->Location, loc)) {
                        packet.Position = loc;
                        gotCamera = true;
                        if (MeSdk::Safe::TryReadField(&cam->Rotation, rot)) {
                            packet.Yaw = static_cast<unsigned short>(
                                rot.Yaw % 0x10000);
                        }
                        {
                            std::lock_guard<std::mutex> lock(
                                g_harnessSnapshotMutex);
                            g_harnessSnapshot.posX = loc.X;
                            g_harnessSnapshot.posY = loc.Y;
                            g_harnessSnapshot.posZ = loc.Z;
                            g_harnessSnapshot.yaw = packet.Yaw;
                        }
                    }
                }
            }
        }
        static unsigned long long lastNoPawnLogMs = 0;
        const auto nowMs = GetTickCount64();
        if (nowMs - lastNoPawnLogMs > 5000ull) {
            lastNoPawnLogMs = nowMs;
            ClientLogf("client: pose tick no pawn camera=%d "
                       "(bones skipped until local pawn resolve)",
                       gotCamera ? 1 : 0);
            if (gotCamera) {
                ClientLogf("client: camera heartbeat pos=(%.0f,%.0f,%.0f) yaw=%u",
                           packet.Position.X, packet.Position.Y,
                           packet.Position.Z,
                           static_cast<unsigned>(packet.Yaw));
            }
        }
        sendto(udpSocket, reinterpret_cast<const char *>(&packet), sizeof(packet),
               0, reinterpret_cast<const sockaddr *>(&server), sizeof(server));
        UpdateHarnessSnapshot();
        return;
    }

    MeSdk::Safe::Gameplay::PawnPoseSnapshot pose = {};
    if (!MeSdk::Safe::Gameplay::TryReadPawnPose(pawn, pose)) {
        static unsigned long long lastBadPoseLogMs = 0;
        const auto nowMs = GetTickCount64();
        if (nowMs - lastBadPoseLogMs > 5000ull) {
            lastBadPoseLogMs = nowMs;
            ClientLogf("client: pose tick pawn=%p but TryReadPawnPose failed "
                       "(falling back to camera)",
                       pawn);
        }
        // Fall back to camera heartbeat instead of silent-returning — that
        // previously froze host=(0,0,0) with no further pose logs.
        packet = {};
        packet.Id = client.Id;
        bool gotCamera = false;
        applySeedHostPose(packet, gotCamera);
        if (!gotCamera && !seedPoseCooldown) {
            if (auto *pc = ResolveLocalPlayerController(false)) {
                Classes::ACamera *cam = nullptr;
                if (MeSdk::Safe::TryReadField(&pc->PlayerCamera, cam) && cam &&
                    MeSdk::Safe::IsPlausibleUObject(cam)) {
                    Classes::FVector loc = {};
                    Classes::FRotator rot = {};
                    if (MeSdk::Safe::TryReadField(&cam->Location, loc)) {
                        packet.Position = loc;
                        gotCamera = true;
                        if (MeSdk::Safe::TryReadField(&cam->Rotation, rot)) {
                            packet.Yaw = static_cast<unsigned short>(
                                rot.Yaw % 0x10000);
                        }
                        std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
                        g_harnessSnapshot.posX = loc.X;
                        g_harnessSnapshot.posY = loc.Y;
                        g_harnessSnapshot.posZ = loc.Z;
                        g_harnessSnapshot.yaw = packet.Yaw;
                    }
                }
            }
        }
        sendto(udpSocket, reinterpret_cast<const char *>(&packet),
               sizeof(packet), 0, reinterpret_cast<const sockaddr *>(&server),
               sizeof(server));
        UpdateHarnessSnapshot();
        return;
    }

    LogHostPhysicsTelemetry(pose);

    packet.Position = pose.location;
    packet.Position.Z += pose.targetMeshTranslationZ;
    // Follow bots stand off along host yaw. Pawn Rotation often stays near 0
    // while the camera uses Controller.Rotation — stand-offs then sit along +X
    // off-camera (empty live-mesh walkway; remotes still softColl near host).
    packet.Yaw = pose.rotation.Yaw % 0x10000;
    if (auto *pc = Engine::GetPlayerController(false)) {
        Classes::FRotator crot = {};
        if (MeSdk::Safe::TryReadField(&pc->Rotation, crot)) {
            const auto lookYaw =
                static_cast<unsigned short>(crot.Yaw % 0x10000);
            static bool s_loggedLookYaw = false;
            if (!s_loggedLookYaw && lookYaw != packet.Yaw) {
                s_loggedLookYaw = true;
                ClientLogf("client: host look yaw from controller pawn=%u look=%u",
                           static_cast<unsigned>(packet.Yaw),
                           static_cast<unsigned>(lookYaw));
            }
            packet.Yaw = lookYaw;
        }
    }
    packet.Velocity = pose.velocity;
    packet.MovementState =
        static_cast<unsigned char>(pose.movementState & 0xFF);
    packet.Physics = static_cast<unsigned char>(pose.physics & 0xFF);

    static int s_prevHostMoveState = -1;
    const bool moveChanged =
        s_prevHostMoveState >= 0 &&
        pose.movementState != s_prevHostMoveState;
    if (moveChanged) {
        ClientLogf("client: move state from=%d to=%d phys=%d",
                   s_prevHostMoveState, pose.movementState, pose.physics);
    }
    s_prevHostMoveState = pose.movementState;
    // MOVE_None/Walking = 0/1; parkour moves (>=Falling) need live Mesh3p.
    const bool parkourMove = pose.movementState >= 2 || pose.physics == 2;

    {
        std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
        g_harnessSnapshot.posX = packet.Position.X;
        g_harnessSnapshot.posY = packet.Position.Y;
        g_harnessSnapshot.posZ = packet.Position.Z;
        g_harnessSnapshot.yaw =
            static_cast<unsigned short>(packet.Yaw % 0x10000);
    }
    UpdateHarnessSnapshot();

    // Defer Mesh3p after world warm. First sample while PHYS_Falling / parkour
    // move hung Tick at tick.callbacks (phase dead → rem=2 sp=0; 2026-07-20).
    // Establish lastGoodBones only when Walking + low speed; parkour forces
    // refresh only after that baseline exists.
    static short lastGoodBones[ARRAYSIZE(CompressedBoneOffsets)] = {};
    static bool haveLastGoodBones = false;
    static int s_poseOkFrames = 0;
    static unsigned long long s_lastBoneSampleMs = 0;
    ++s_poseOkFrames;
    if (haveLastGoodBones) {
        memcpy(packet.CompressedBones, lastGoodBones,
               sizeof(packet.CompressedBones));
    } else {
        memset(packet.CompressedBones, 0, sizeof(packet.CompressedBones));
    }

    const float spd2 = pose.velocity.X * pose.velocity.X +
                       pose.velocity.Y * pose.velocity.Y +
                       pose.velocity.Z * pose.velocity.Z;
    const auto nowSampleMs = GetTickCount64();
    constexpr int kPhysWalking = 1;
    const bool wantFirstSample =
        !haveLastGoodBones && s_poseOkFrames > 30 &&
        pose.physics == kPhysWalking && spd2 < 100.0f && !parkourMove;
    // Fast move / parkour: sample every pose tick. Slow walk: ≥50ms so remotes
    // see multi-frame stream instead of a 200ms-held snapshot. Idle: 200ms.
    const bool slowWalk = spd2 > 25.0f && spd2 <= 100.0f && !parkourMove;
    const bool wantRefresh =
        haveLastGoodBones &&
        (spd2 > 100.0f || parkourMove || moveChanged ||
         (slowWalk && (nowSampleMs - s_lastBoneSampleMs) >= 33ull) ||
         (nowSampleMs - s_lastBoneSampleMs) >= 200ull);
    const bool wantBoneSample = wantFirstSample || wantRefresh;

    void *bonesBuffer = nullptr;
    int meshAtomCount = 0;
    if (wantBoneSample) {
        static bool s_loggedMesh3pAttempt = false;
        if (!s_loggedMesh3pAttempt) {
            s_loggedMesh3pAttempt = true;
            ClientLogf("client: Mesh3p sample begin frames=%d phys=%d "
                       "parkour=%d haveBones=%d",
                       s_poseOkFrames, pose.physics, parkourMove ? 1 : 0,
                       haveLastGoodBones ? 1 : 0);
        }
    }
    if (wantBoneSample &&
        MeSdk::Safe::Gameplay::TryGetMesh3pBoneBuffer(pawn, bonesBuffer,
                                                      meshAtomCount) &&
        bonesBuffer && meshAtomCount > 0) {
        s_lastBoneSampleMs = nowSampleMs;
        static bool loggedAtomsOnce = false;
        if (!loggedAtomsOnce) {
            loggedAtomsOnce = true;
            ClientLogf("client: host Mesh3p atoms=%d "
                       "(Faith layout; Kate remap needs src[0..%d])",
                       meshAtomCount, PLAYER_PAWN_BONE_COUNT - 1);
        }

        const auto bonesBase = reinterpret_cast<byte *>(bonesBuffer);
        const size_t byteLimit =
            static_cast<size_t>(meshAtomCount) * sizeof(Classes::FBoneAtom);
        bool allOk = true;
        short sampled[ARRAYSIZE(CompressedBoneOffsets)] = {};
        // Preserve prior sample for offsets past a short LocalAtoms buffer so
        // we do not zero Faith rest floats that Kate TransformBones still reads.
        if (haveLastGoodBones) {
            memcpy(sampled, lastGoodBones, sizeof(sampled));
        }
        int skippedPastMesh = 0;
        for (auto i = 0; i < ARRAYSIZE(CompressedBoneOffsets); ++i) {
            const auto offset =
                static_cast<size_t>(CompressedBoneOffsets[i]);
            if (offset + sizeof(float) > byteLimit) {
                ++skippedPastMesh;
                continue;
            }
            float boneValue = 0.f;
            if (!MeSdk::Safe::Gameplay::TryReadBufferFloat(
                    bonesBase, CompressedBoneOffsets[i], boneValue)) {
                allOk = false;
                break;
            }
            sampled[i] = static_cast<short>(roundf(boneValue * 215.0f));
        }
        if (allOk) {
            memcpy(packet.CompressedBones, sampled, sizeof(sampled));
            memcpy(lastGoodBones, sampled, sizeof(sampled));
            haveLastGoodBones = true;

            static bool loggedSkipOnce = false;
            if (skippedPastMesh > 0 && !loggedSkipOnce) {
                loggedSkipOnce = true;
                ClientLogf("client: host bone sample clipped "
                           "skippedOffsets=%d atoms=%d (kept lastGood/rest)",
                           skippedPastMesh, meshAtomCount);
            }

            // Dump snapshot + cycle ring often enough for bot walk playback.
            // Walking: every sample (~33ms). Idle: 500ms heartbeat.
            static unsigned long long lastDumpMs = 0;
            const auto nowMs = GetTickCount64();
            const unsigned long long dumpEvery =
                (spd2 > 25.0f || parkourMove) ? 40ull : 500ull;
            if (nowMs - lastDumpMs >= dumpEvery) {
                lastDumpMs = nowMs;
                char path[MAX_PATH] = {};
                if (GetTempPathA(static_cast<DWORD>(sizeof(path)), path) > 0) {
                    strncat_s(path, "mirroredge-host-bones.bin", _TRUNCATE);
                    HANDLE file = CreateFileA(
                        path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (file != INVALID_HANDLE_VALUE) {
                        DWORD written = 0;
                        WriteFile(file, sampled, sizeof(sampled), &written,
                                  nullptr);
                        CloseHandle(file);
                    }
                }
                WriteHostBoneCycleFile(
                    sampled, static_cast<int>(ARRAYSIZE(CompressedBoneOffsets)),
                    BoneClipIdFromHostMove(pose.movementState, pose.physics,
                                           spd2));
                static bool loggedBonesOnce = false;
                if (!loggedBonesOnce) {
                    loggedBonesOnce = true;
                    int nonZero = 0;
                    for (auto i = 0; i < ARRAYSIZE(sampled); ++i) {
                        if (sampled[i] != 0) {
                            ++nonZero;
                        }
                    }
                    ClientLogf("client: local bones sampled nonZero=%d "
                               "(dumped %%TEMP%%\\mirroredge-host-bones.bin)",
                               nonZero);
                }
            }
        }
    } else if (wantBoneSample) {
        static unsigned long long lastBoneFailMs = 0;
        const auto nowMs = GetTickCount64();
        if (nowMs - lastBoneFailMs > 5000ull) {
            lastBoneFailMs = nowMs;
            ClientLog("client: Mesh3p bone sample failed "
                      "(pawn ok; LocalAtoms empty/stale?)");
        }
    }

    // Idle TX: send full packet at most ~10Hz when nearly still (pos still
    // updates for Follow). Parkour moves / state changes keep per-tick bones.
    static unsigned long long s_lastIdleSendMs = 0;
    static Classes::FVector s_lastSentPos = {};
    const bool nearlyIdle = spd2 <= 25.0f && !parkourMove && !moveChanged;
    const float ddx = packet.Position.X - s_lastSentPos.X;
    const float ddy = packet.Position.Y - s_lastSentPos.Y;
    const float ddz = packet.Position.Z - s_lastSentPos.Z;
    const float dPos2 = ddx * ddx + ddy * ddy + ddz * ddz;
    const auto nowSendMs = GetTickCount64();
    if (nearlyIdle && dPos2 < 25.0f &&
        (nowSendMs - s_lastIdleSendMs) < 66ull) {
        return;
    }
    s_lastIdleSendMs = nowSendMs;
    s_lastSentPos = packet.Position;

    sendto(udpSocket, reinterpret_cast<const char *>(&packet), sizeof(packet),
           0, reinterpret_cast<const sockaddr *>(&server), sizeof(server));
}

void ApplyRemotePlayerWorldPoses() {
    UpdateAdaptiveInterpolationDelay();
    Classes::ATdPlayerPawn *localPawn = Engine::GetPlayerPawn(false);
    Classes::FVector hostPos = {};
    Classes::FVector hostPush = {};
    const bool doSoft = softCollisionEnabled;
    const bool haveHost = localPawn && MeSdk::Safe::IsPlausibleUObject(localPawn);
    if (haveHost) {
        hostPos = localPawn->Location;
    }

    players.Mutex.lock_shared();
    for (const auto &p : players.List) {
        if (!p) {
            continue;
        }
        // Pull EndScene SPAWN_OK into player visual even if maintenance Tick
        // was SEH-removed — otherwise remotes stay at spawn coords / invisible.
        SyncActorFromStableSlot(p);
        auto *remote = PlayerRemoteActor(p);
        if (!remote || p->Id != p->LastPacket.Id || !p->ToTime) {
            continue;
        }

        static Client::PACKET rendered;
        BuildRenderedPacket(p, rendered);
        if (doSoft && haveHost) {
            // FallDrop SoftProbe sits at hostZ+1000; XY soft-push still nudged
            // Faith off the tutorial roof (EXIT=3 rem=0). Separate remotes only.
            Classes::FVector *pushOut = &hostPush;
            if (fabsf(rendered.Position.Z - hostPos.Z) > 150.0f) {
                pushOut = nullptr;
            }
            SoftSeparateRemoteFromHost(rendered.Position, hostPos,
                                       softCollisionRadius,
                                       softCollisionStrength, pushOut);
        }
        Classes::FVector from = rendered.Position;
        MeSdk::Safe::Gameplay::TryReadActorLocation(remote, from);
        ClampRemoteDesiredPosition(localPawn, from, rendered.Position);

        // Foot plant: idle/walk remotes share host roof — snap Z to mesh PrePivot
        // offset when already near the walkway plane (avoids floating/sinking).
        constexpr float kMeshTz = 94.0f;
        if (haveHost &&
            (!p->HasRemoteMoveState || p->RemoteMovementState <= 1)) {
            const float targetZ = hostPos.Z + kMeshTz;
            if (fabsf(rendered.Position.Z - targetZ) < 40.0f) {
                rendered.Position.Z = targetZ;
            }
        }

        const float rvx = p->RemoteVelocity.X;
        const float rvy = p->RemoteVelocity.Y;
        const float rvz = p->RemoteVelocity.Z;
        const float rspd2 = rvx * rvx + rvy * rvy + rvz * rvz;
        unsigned short yawOut = rendered.Yaw;
        bool snapYaw = false;
        // Face host when waving or idle — neck/head atoms 7–14 are not in
        // CompressedBoneOffsets, so whole-body yaw is the only look-at proxy.
        if (haveHost &&
            (p->WaveGestureFrames > 0 ||
             ((!p->HasRemoteMoveState || p->RemoteMovementState <= 1) &&
              (!p->HasRemoteVelocity || rspd2 <= 400.0f)))) {
            const float dx = hostPos.X - rendered.Position.X;
            const float dy = hostPos.Y - rendered.Position.Y;
            if (dx * dx + dy * dy > 4.0f) {
                const float rad = atan2f(dy, dx);
                yawOut = static_cast<unsigned short>(
                    static_cast<int>(rad / 6.28318530718f * 65536.0f) & 0xFFFF);
                snapYaw = true;
            }
        } else if (p->HasRemoteVelocity && rspd2 > 1600.0f) {
            // Walking: face travel direction (packet yaw is often host look).
            const float rad = atan2f(rvy, rvx);
            yawOut = static_cast<unsigned short>(
                static_cast<int>(rad / 6.28318530718f * 65536.0f) & 0xFFFF);
            snapYaw = false;
        }
        WriteSmoothedRemotePose(remote, rendered.Position, yawOut, snapYaw);
        Classes::FVector written = rendered.Position;
        MeSdk::Safe::Gameplay::TryReadActorLocation(remote, written);
        p->MaxZ = written.Z;

        // One-shot TransformBones when BonesTick never matches this remote's
        // LocalAtoms (common if mesh is culled). Do NOT repeat — foreign-tick
        // rewrites soft-freeze (KI-012). Logs phase5 once when it succeeds.
        // Wait until every joined peer has a visual — TransformBones right after
        // the first SPAWN_OK hung Tick at tick.callbacks and left rem=2 sp=1
        // (Kate+Kate; 2026-07-20).
        if (p->HasRemoteBoneMotion) {
            static bool s_loggedBootstrapXform = false;
            if (!s_loggedBootstrapXform) {
                bool peerStillSpawning = false;
                for (const auto &other : players.List) {
                    if (!other || other->Id != other->LastPacket.Id) {
                        continue;
                    }
                    if (!PlayerRemoteActor(other)) {
                        peerStillSpawning = true;
                        break;
                    }
                }
                if (!peerStillSpawning) {
                    Classes::USkeletalMeshComponent *skel = nullptr;
                    Classes::TArray<Classes::FBoneAtom> *localAtoms = nullptr;
                    if (TryReadPlayerRemoteSkel(p, skel) && skel &&
                        MeSdk::Safe::Gameplay::TryGetMeshLocalAtomsArray(
                            skel, localAtoms) &&
                        localAtoms && localAtoms->Buffer()) {
                        Engine::TransformBones(RemoteVisualCharacter(p),
                                               localAtoms, rendered.Bones);
                        s_loggedBootstrapXform = true;
                        ClientLogf("client: TransformBones visual=%d "
                                   "destAtoms=%d srcSlots=%d",
                                   static_cast<int>(RemoteVisualCharacter(p)),
                                   static_cast<int>(localAtoms->Num()),
                                   PLAYER_PAWN_BONE_COUNT);
                    }
                }
            }
        }
    }
    players.Mutex.unlock_shared();

    // Light local nudge only while overlapping; never touch remotes on leave.
    if (doSoft && localPawn) {
        const float push2 =
            hostPush.X * hostPush.X + hostPush.Y * hostPush.Y;
        if (push2 > 0.25f) {
            Classes::FVector nudged = localPawn->Location;
            nudged.X += hostPush.X;
            nudged.Y += hostPush.Y;
            MeSdk::Safe::Gameplay::TryWriteActorLocation(localPawn, nudged);
            static unsigned long long s_lastSoftLogMs = 0;
            const auto nowSoft = GetTickCount64();
            if (nowSoft - s_lastSoftLogMs > 2000ull) {
                s_lastSoftLogMs = nowSoft;
                ClientLogf("client: soft collision engaged r=%.0f s=%.2f",
                           softCollisionRadius, softCollisionStrength);
                char flagPath[MAX_PATH] = {};
                if (GetTempPathA(static_cast<DWORD>(sizeof(flagPath)), flagPath) >
                    0) {
                    strncat_s(flagPath, "mirroredge-soft-collision.ok",
                              _TRUNCATE);
                    HANDLE flag = CreateFileA(
                        flagPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (flag != INVALID_HANDLE_VALUE) {
                        const char body[] = "ok\n";
                        DWORD written = 0;
                        WriteFile(flag, body, sizeof(body) - 1, &written,
                                  nullptr);
                        CloseHandle(flag);
                    }
                }
            }
        }
    }
}

// Split into separate Engine::OnTick registrations so a SEH in pose resolve
// does not remove spawn handoff + remote pose apply (engine removes only the
// crashed callback).
void OnTickMaintenance(float delta) {
    if (disabled.load()) {
        return;
    }
    static float sum = 0;
    sum += delta;
    if (!loading.load() && connected.load() && sum > 0.016f) {
        sum = 0;
        if (client.Level == "gameplay" && Engine::IsHostedGameplayLive()) {
            static ULONGLONG s_lastUpgradeMs = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - s_lastUpgradeMs >= 1000ull) {
                s_lastUpgradeMs = now;
                // Skip while engine PC/world cold — level probe used to call
                // TryFindTdPlayerController(false) which walked all GObjects
                // concurrent with EndScene idle warm (idle.done hang; 2026-07-21).
                // Outer already requires client.Level == "gameplay".
                // Do NOT call TryUpgradeGameplayLevelName from Tick —
                // StreamingLevels / GetMapName / FName VirtualQuery soft-froze
                // rem=2 sp=0 with no spawn_drain ENTER (2026-07-21). Keep
                // synthetic gameplay LevelsCompatible; adopt concrete map
                // from peers (bots send tutorial_p) instead.
                if (!Engine::GetPlayerController(false) &&
                    !Engine::GetWorld(false)) {
                    // cold caches — wait
                } else if (TryAdoptRemoteGameplayLevel()) {
                    QueueSpawnEligibleRemotePlayers();
                }
            }
        }
        OnRemotePlayerMaintenanceTick();
    }
}

void OnTickPoseNetwork(float delta) {
    if (disabled.load()) {
        return;
    }
    static float sum = 0;
    sum += delta;
    if (!loading.load() && connected.load() && sum > 0.016f) {
        sum = 0;
        OnLocalPoseNetworkTick();
    }
}

void OnTickApplyRemotePoses(float delta) {
    if (disabled.load()) {
        return;
    }
    static float sum = 0;
    sum += delta;
    if (!loading.load() && connected.load() && sum > 0.016f) {
        sum = 0;
        ApplyRemotePlayerWorldPoses();
    }
}

void OnTick(float delta) {
    // Kept for header / any external refs; prefer the split callbacks.
    OnTickMaintenance(delta);
    OnTickPoseNetwork(delta);
    OnTickApplyRemotePoses(delta);
}

void OnTickGames(float delta) {
    if (client.GameMode != GameMode_Tag) {
        return;
    }

    static float sum = 0;
    sum += delta;

    if (!loading.load() && connected.load() && sum > 0.16f) {
        // B0: never bare GetPlayerPawn() — soft-resolve via cache/PC fields.
        auto *pawn = ResolveLocalPlayerPawn(false);
        if (!pawn) {
            return;
        }

        sum = 0;

        if (client.Id != client.TaggedPlayerId) {
            float health = 0.f;
            if (MeSdk::Safe::Gameplay::TryReadPawnHealth(pawn, health) &&
                health <= 0.f && !playerDiedAndSentJsonMessage) {
                SendJsonMessage({
                    {"type", "dead"},
                });

                char buffer[0xFF];
                snprintf(buffer, sizeof(buffer),
                          "[Tag] %s died and they will chase instead",
                          client.Name.c_str());

                SendJsonMessage({
                    {"type", "announce"},
                    {"body", buffer},
                });

                playerDiedAndSentJsonMessage = true;
                client.CanTag = false;
            }
        } else if (!client.CanTag) {
            IgnorePlayerInput(true);
        }

        float health = 0.f;
        if (MeSdk::Safe::Gameplay::TryReadPawnHealth(pawn, health) &&
            playerDiedAndSentJsonMessage && health == 100.f) {
            playerDiedAndSentJsonMessage = false;
        }
    }
}

void InstallClientNetworkTickHooks() {
    if (networkTickHooksRegistered) {
        return;
    }
    networkTickHooksRegistered = true;

    // Do NOT call ATdPlayerPawn::StaticClass() here — activation runs this on
    // the game thread before live, and StaticClass hung the activation callback
    // (never reached "activation set live", spawns stuck in pending, 2026-07-20).
    // Soft-resolve uses only Engine::GetPlayerPawn until a safe prewarm exists.

    Engine::OnTick(OnTickMaintenance);
    Engine::OnTick(OnTickPoseNetwork);
    Engine::OnTick(OnTickApplyRemotePoses);
    Engine::OnTick(OnTickGames);
}

void InstallClientRemotePlayerHooks() {
    if (remotePlayerHooksRegistered) {
        return;
    }
    remotePlayerHooksRegistered = true;

    // Location/yaw: once per network tick via ApplyRemotePlayerWorldPoses.
    // Do NOT register OnActorTick — that runs for every world AActor and
    // VirtualQuery/SEH/alloc per actor dropped the game to ~0.4 FPS after bots
    // spawned (2026-07-19 phase ring).

    Engine::OnBonesTick([](Classes::TArray<Classes::FBoneAtom> *bones) {
        if (disabled.load() || !bones || !bones->Buffer()) {
            return;
        }

        // Skip all VirtualQuery/IsPlausible work when no remote has bone motion.
        players.Mutex.lock_shared();
        bool anyBoneMotion = false;
        for (const auto &p : players.List) {
            if (p && PlayerHasRemoteVisual(p) && p->ToTime &&
                p->HasRemoteBoneMotion && p->Id == p->LastPacket.Id) {
                anyBoneMotion = true;
                break;
            }
        }
        if (!anyBoneMotion) {
            players.Mutex.unlock_shared();
            return;
        }

        const void *boneBuf = bones->Buffer();
        for (const auto &p : players.List) {
            if (!p || !PlayerHasRemoteVisual(p) || !p->ToTime ||
                !p->HasRemoteBoneMotion || p->Id != p->LastPacket.Id) {
                continue;
            }

            Classes::USkeletalMeshComponent *skel = nullptr;
            if (!TryReadPlayerRemoteSkel(p, skel) || !skel) {
                continue;
            }

            Classes::TArray<Classes::FBoneAtom> *localAtoms = nullptr;
            if (!MeSdk::Safe::Gameplay::TryGetMeshLocalAtomsArray(skel,
                                                                  localAtoms) ||
                !localAtoms || !localAtoms->Buffer()) {
                continue;
            }
            // Only rewrite the mesh that is currently BonesTick'ing — writing
            // other remotes' LocalAtoms from a foreign tick soft-freezes (KI-012).
            if (localAtoms->Buffer() != boneBuf) {
                continue;
            }

            static Client::PACKET rendered;
            BuildRenderedPacket(p, rendered);
            const auto visual = RemoteVisualCharacter(p);
            const int destAtoms = static_cast<int>(localAtoms->Num());
            int expectedDest = 63;
            if (visual == Engine::Character::Faith ||
                visual == Engine::Character::Ghost) {
                expectedDest = PLAYER_PAWN_BONE_COUNT;
            } else if (visual == Engine::Character::Kate) {
                expectedDest = 102;
            }
            Engine::TransformBones(visual, localAtoms, rendered.Bones);
            static bool loggedXformOnce = false;
            if (!loggedXformOnce) {
                loggedXformOnce = true;
                ClientLogf("client: TransformBones visual=%d destAtoms=%d "
                           "srcSlots=%d",
                           static_cast<int>(visual), destAtoms,
                           PLAYER_PAWN_BONE_COUNT);
            }
            if (destAtoms < expectedDest) {
                static unsigned long long s_lastShortLogMs = 0;
                const auto nowMs = GetTickCount64();
                if (nowMs - s_lastShortLogMs > 5000ull) {
                    s_lastShortLogMs = nowMs;
                    ClientLogf("client: TransformBones short destAtoms=%d "
                               "need>=%d visual=%d id=%x",
                               destAtoms, expectedDest,
                               static_cast<int>(visual), p->Id);
                }
            }
        }

        players.Mutex.unlock_shared();
    });
}

void InstallClientSimulationHooks() {
    InstallClientNetworkTickHooks();
    InstallClientRemotePlayerHooks();
}

void EnsureClientGameplayCallbacks() {
    // Register tick/bones callbacks only from the main-thread pump once gameplay
    // hooks are already installed at plugin init.
    InstallClientSimulationHooks();
}

void EnsureClientRenderHook() {
    if (renderHookRegistered) {
        return;
    }

    renderHookRegistered = true;
    Engine::OnRenderScene(OnRender);
}

void EnsureClientRemotePlayerPresentation() {
    const int hosted = ModHost::IsAttached() ? 1 : 0;
    const int dis = disabled.load() ? 1 : 0;
    const int live = Engine::IsHostedGameplayLive() ? 1 : 0;
    const int hooksOk = Engine::AreGameplayHooksInstalled() ? 1 : 0;

    char diag[256] = {};
    snprintf(diag, sizeof(diag),
             "DIAG: EnsureClientRemotePlayerPresentation ENTER hosted=%d disabled=%d level=%s live=%d hooks=%d\n",
             hosted, dis, client.Level.c_str(), live, hooksOk);
    OutputDebugStringA(diag);

    ClientLogf("client: ensure_presentation enter hosted=%d disabled=%d level=%s live=%d",
               hosted, dis, client.Level.c_str(), live);

    if (!ModHost::IsAttached() || disabled.load()) {
        snprintf(diag, sizeof(diag),
                 "DIAG: EnsureClientRemotePlayerPresentation EXIT skip hosted=%d disabled=%d\n",
                 hosted, dis);
        OutputDebugStringA(diag);
        ClientLogf("client: ensure_presentation skip hosted=%d disabled=%d",
                   hosted, dis);
        return;
    }

    if (client.Level.empty() || client.Level == "tdmainmenu") {
        snprintf(diag, sizeof(diag),
                 "DIAG: EnsureClientRemotePlayerPresentation EXIT skip level=%s\n",
                 client.Level.c_str());
        OutputDebugStringA(diag);
        ClientLogf("client: ensure_presentation skip level=%s", client.Level.c_str());
        return;
    }

    if (!TryEnsureGameplayHooksForSetGameplay()) {
        OutputDebugStringA("DIAG: EnsureClientRemotePlayerPresentation EXIT abort hooks not installed\n");
        ClientLog("client: ensure_presentation abort gameplay hooks not installed");
        return;
    }

    if (!Engine::IsHostedGameplayLive()) {
        OutputDebugStringA("DIAG: EnsureClientRemotePlayerPresentation -> QueueActivateHostedGameplay (not live yet)\n");
        ClientLog("client: ensure_presentation defer activation");
        QueueActivateHostedGameplay();
        return;
    }

    OutputDebugStringA("DIAG: EnsureClientRemotePlayerPresentation -> live, callbacks + spawn\n");
    EnsureClientGameplayCallbacks();
    ClientLog("client: ensure_presentation gameplay callbacks ready");
    ClientLog("client: ensure_presentation spawn now (live=true)");
    QueueSpawnEligibleRemotePlayers();
}

void CompleteMultiplayerRemoteLevelActivation() {
    ClientLog("client: remote level activation callback");
    if (!connected.load() || disabled.load()) {
        return;
    }
    if (client.Level.empty() || client.Level == "tdmainmenu") {
        loading.store(false);
        return;
    }

    EnsureClientGameplayCallbacks();
    Engine::SetHostedGameplayLive(true);
    ClientLog("client: remote activation set live");
    loading.store(false);
    QueueSpawnEligibleRemotePlayers();
}

void CompleteMultiplayerHostedActivation() {
    OutputDebugStringA("DIAG: CompleteMultiplayerHostedActivation ENTER\n");
    ClientLog("client: activation callback firing");

    if (!ModHost::IsAttached() || disabled.load()) {
        OutputDebugStringA("DIAG: CompleteMultiplayerHostedActivation EXIT not-attached or disabled\n");
        loading.store(false);
        return;
    }

    if (client.Level.empty()) {
        OutputDebugStringA("DIAG: CompleteMultiplayerHostedActivation EXIT level empty\n");
        loading.store(false);
        return;
    }

    if (!Engine::AreGameplayHooksInstalled()) {
        OutputDebugStringA("DIAG: CompleteMultiplayerHostedActivation EXIT hooks not installed\n");
        ClientLog("client: activation abort gameplay hooks missing");
        loading.store(false);
        return;
    }

    OutputDebugStringA("DIAG: CompleteMultiplayerHostedActivation -> callbacks + set live + spawn\n");
    EnsureClientGameplayCallbacks();
    ClientLog("client: activation gameplay callbacks ready");
    // After explicit Set Gameplay (client.Level != menu), allow hosted live
    // without calling IsGameplayReadySafe() — that path refreshes world/pawn
    // caches via GObjects and freezes the game on the Set Gameplay click.
    const bool menuLevel = client.Level.empty() || client.Level == "tdmainmenu";
    if (menuLevel) {
        OutputDebugStringA("DIAG: CompleteMultiplayerHostedActivation EXIT still menu level\n");
        ClientLog("client: activation defer live still on menu level");
        loading.store(false);
        return;
    }
    Engine::SetHostedGameplayLive(true);
    ClientLog("client: activation set live");
    loading.store(false);
    // Replace synthetic "gameplay" with the real map (streaming/GetMapName)
    // once world caches are warm — Set Gameplay intentionally skipped this.
    (void)TryUpgradeGameplayLevelName();
    QueueSpawnEligibleRemotePlayers();

    MpDebugLog("client.cpp:CompleteMultiplayerHostedActivation", "live",
               "H-LEVEL", 0, 0, 0);
}

void QueueActivateHostedGameplay() {
    if (!ModHost::IsAttached()) {
        OutputDebugStringA("DIAG: QueueActivateHostedGameplay SKIP not attached\n");
        return;
    }

    OutputDebugStringA("DIAG: QueueActivateHostedGameplay -> RequestGameplayActivation\n");
    ClientLog("client: QueueActivateHostedGameplay starting");
    Engine::RequestGameplayActivation(CompleteMultiplayerHostedActivation);
    ClientLog("client: QueueActivateHostedGameplay done");
}

void QueueSpawnEligibleRemotePlayers() {
    if (loading.load() || !connected.load()) {
        return;
    }

    // Allow adopt/spawn once hosted live is set (Set Gameplay / FORCE).
    // Do not call IsGameplayReadySafe here — it is cache-only now, but older
    // builds refreshed GObjects and froze after Set Gameplay.
    if (!Engine::IsHostedGameplayLive()) {
        return;
    }

    TryAdoptRemoteGameplayLevel();
    (void)TryUpgradeGameplayLevelName();

    players.Mutex.lock_shared();
    size_t needSpawn = 0;
    for (auto *p : players.List) {
        if (p && LevelsCompatible(p->Level, client.Level) &&
            !PlayerHasRemoteVisual(p)) {
            ++needSpawn;
            QueueSpawnPlayerIfReady(p);
        }
    }
    players.Mutex.unlock_shared();
    if (needSpawn > 0) {
        static ULONGLONG s_lastEligLog = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_lastEligLog >= 2000) {
            s_lastEligLog = now;
            ClientLogf("client: QueueSpawnEligible needSpawn=%zu level=%s",
                       needSpawn, client.Level.c_str());
        }
    }
}

void TrySpawnPendingRemotePlayers() {
    // Drain the pending spawn set first — these were queued by
    // QueueSpawnPlayerIfReady from various threads.  We are on the game
    // main thread (TickHook callback), so UE3 object access is safe here.
    {
        std::unordered_set<unsigned int> pending;
        {
            std::lock_guard<std::mutex> lock(g_spawnPendingMutex);
            pending.swap(g_spawnPendingPlayers);
        }
        if (!pending.empty()) {
            ClientLogf("client: spawn_pending_drain count=%zu", pending.size());
        }
        players.Mutex.lock_shared();
        for (unsigned int id : pending) {
            Client::Player *p = GetPlayerById(id);
            if (!p) { ClientLogf("client: spawn_pending_drain gone id=%x", id); continue; }
            if (PlayerHasRemoteVisual(p)) {
                ClientLogf("client: spawn_pending_drain already_actor id=%x", id);
                continue;
            }
            if (TrySpawnPlayerDirect(p)) {
                ClientLogf("client: spawn_pending_drain ok id=%x actor=%p", id,
                           p->Actor);
            }
        }
        players.Mutex.unlock_shared();
    }

    static float spawnRetrySum = 0.f;
    spawnRetrySum += 0.016f;
    if (spawnRetrySum < 2.f) {
        return;
    }
    spawnRetrySum = 0.f;

    if (!Engine::IsHostedGameplayLive()) {
        // Hosted gameplay not yet live — check if there are players waiting
        // and trigger activation if so.  This handles the case where bots
        // connect after the initial PostLevelLoad where activation was
        // skipped because the player list was empty.
        const bool menuLevel =
            client.Level.empty() || client.Level == "tdmainmenu";
        if (menuLevel && !Engine::IsGameplayReadySafe()) {
            return;
        }
        TryAdoptRemoteGameplayLevel();
        bool hasPending = false;
        players.Mutex.lock_shared();
        for (auto *p : players.List) {
            if (p && LevelsCompatible(p->Level, client.Level)) {
                hasPending = true;
                break;
            }
        }
        players.Mutex.unlock_shared();
        if (hasPending) {
            ClientLog("client: spawn_pending found players, requesting hosted activation");
            QueueActivateHostedGameplay();
        }
        return;
    }

    if (!connected.load() || loading.load()) {
        return;
    }

    TryAdoptRemoteGameplayLevel();

    players.Mutex.lock_shared();
    for (auto *p : players.List) {
        if (!p || !LevelsCompatible(p->Level, client.Level)) {
            continue;
        }

        if (PlayerHasRemoteVisual(p)) {
            continue;
        }

        static std::unordered_map<unsigned int, ULONGLONG> s_lastRetryLog;
        const ULONGLONG retryNow = GetTickCount64();
        const auto rit = s_lastRetryLog.find(p->Id);
        if (rit == s_lastRetryLog.end() || (retryNow - rit->second) >= 5000) {
            s_lastRetryLog[p->Id] = retryNow;
            ClientLogf("client: spawn retry id=%x level=%s", p->Id,
                       p->Level.c_str());
        }
        TrySpawnPlayerDirect(p);
    }
    players.Mutex.unlock_shared();
}
} // namespace ClientInternal
