#include "client_platform.h"
#include "client_internal.h"

#include "menu_shim.h"

#include <vector>
namespace ClientInternal {
void ApplyManualClientLevelOnMainThread(const std::string &level) {
    if (level == "gameplay") {
        loading.store(false);

        // Do NOT call TryGetGameplayMapName here — it historically used
        // GetWorld(true) (GObjects walk) and even the cached path can call
        // world->GetMapName which stalls Set Gameplay on the game thread.
        // Keep synthetic "gameplay" until OnPostLevelLoad / remote adopt
        // supplies a real map name (e.g. tutorial_p).
        ClientLog("client: manual Set Gameplay — skip map resolve");

        players.Mutex.lock();
        for (auto *p : players.List) {
            if (!p) {
                continue;
            }

            if (p->Level.empty() || p->Level == "tdmainmenu" ||
                p->Level == "gameplay") {
                p->Level = client.Level;
            }
        }
        players.Mutex.unlock();

        OutputDebugStringA("DIAG: ApplyManualClientLevelOnMainThread(gameplay) -> EnsureClientRemotePlayerPresentation\n");
        EnsureClientRemotePlayerPresentation();

        return;
    }

    if (level != "tdmainmenu") {
        // Real map name (e.g. tutorial_p) already written to client.Level by
        // ApplyManualClientLevel / TryAdoptRemoteGameplayLevel — finish hosted
        // presentation so spawn can run without waiting for OnPostLevelLoad.
        loading.store(false);
        EnsureClientRemotePlayerPresentation();
        return;
    }

    Engine::CancelGameplayActivation();

    std::vector<Classes::ASkeletalMeshActorSpawnable *> actorsToDespawn;
    players.Mutex.lock();
    for (auto *p : players.List) {
        if (p && p->Actor) {
            actorsToDespawn.push_back(p->Actor);
        }
        if (p) {
            ClearPlayerRemoteVisual(p);
        }
    }
    players.Mutex.unlock();

    for (auto *actor : actorsToDespawn) {
        (void)actor; // null-only; no park after TransformBones (KI-2026-012)
    }
}

void ApplyManualClientLevel(const std::string &level) {
    const auto oldLevel = client.Level;
    client.Level = level;
    loading.store(level == "tdmainmenu");
    NotifyServerLevel();

    const auto capturedLevel = level;
    QueueClientEngineTask([capturedLevel, oldLevel]() {
        ApplyManualClientLevelOnMainThread(capturedLevel);
        UpdateHarnessSnapshot();
        ClientLogf("client: level manually set %s -> %s", oldLevel.c_str(),
                   capturedLevel.c_str());
    });
}

void NotifyServerLevel() {
    if (!connected.load() || disabled.load()) {
        return;
    }

    const auto notifyId = client.Id;
    const auto notifyLevel = client.Level;
    QueueClientEngineTask([notifyId, notifyLevel]() {
        if (!connected.load() || disabled.load()) {
            return;
        }

        const bool sent = SendJsonMessage({
            {"type", "level"},
            {"id", notifyId},
            {"level", notifyLevel},
        });
        ClientLogf("client: notify level id=%x level=%s sent=%s",
                   notifyId, notifyLevel.c_str(), sent ? "true" : "false");
        MpDebugLog("client.cpp:NotifyServerLevel", sent ? "sent" : "send_fail",
                   "H-LEVEL", static_cast<uintptr_t>(notifyLevel.size()),
                   notifyId, sent ? 1u : 0u);
    });
}

struct LevelProbeContext {
    bool ok = false;
    bool hadWorld = false;
    bool hadPawn = false;
    size_t streamingCount = 0;
    size_t loadedCount = 0;
    std::string level;
};

bool IsMenuLevelName(const std::string &level) {
    return level.empty() || level == "tdmainmenu" || level == "entry";
}

bool LevelsCompatible(const std::string &a, const std::string &b) {
    if (a == b) {
        return true;
    }
    // Synthetic "gameplay" (Set Gameplay / FORCE_HOSTED_LIVE) matches any
    // concrete non-menu map so bots/peers still sync while the host upgrades.
    if (a == "gameplay" && !IsMenuLevelName(b) && b != "gameplay") {
        return true;
    }
    if (b == "gameplay" && !IsMenuLevelName(a) && a != "gameplay") {
        return true;
    }
    return false;
}

bool TryAdoptRemoteGameplayLevel() {
    if (!IsMenuLevelName(client.Level) && client.Level != "gameplay") {
        return false;
    }

    std::string adopted;
    players.Mutex.lock_shared();
    for (auto *p : players.List) {
        if (!p) {
            continue;
        }
        if (!IsMenuLevelName(p->Level) && p->Level != "gameplay") {
            adopted = p->Level;
            break;
        }
    }
    players.Mutex.unlock_shared();

    if (adopted.empty()) {
        return false;
    }

    const auto oldLevel = client.Level;
    client.Level = adopted;
    loading.store(false);
    NotifyServerLevel();
    ClientLogf("client: adopted remote level %s -> %s", oldLevel.c_str(),
               adopted.c_str());
    return true;
}

std::string NormalizeLevelName(std::string level) {
    level = ToLower(std::move(level));

    for (auto &c : level) {
        if (c == '\\' || c == '/') {
            c = '.';
        }
    }

    const auto dot = level.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < level.size()) {
        level = level.substr(dot + 1);
    }

    return level;
}

// SEH helpers — no C++ destructors (C2712).
static bool RawStreamingLevelsHeader(
    const Classes::TArray<Classes::ULevelStreaming *> &levels, int &outCount,
    Classes::ULevelStreaming **&outBuffer) {
    outCount = 0;
    outBuffer = nullptr;
    __try {
        outCount = static_cast<int>(levels.Num());
        outBuffer = levels.Buffer();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool RawStreamingLevelAt(Classes::ULevelStreaming **buffer, size_t index,
                                Classes::ULevelStreaming *&out) {
    out = nullptr;
    if (!buffer) {
        return false;
    }
    __try {
        out = buffer[index];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadLoadedStreamingLevel(Classes::AWorldInfo *world,
                                        LevelProbeContext &ctx) {
    if (!MeSdk::Safe::IsPlausibleUObject(world)) {
        return false;
    }

    Classes::TArray<Classes::ULevelStreaming *> levels = {};
    if (!MeSdk::Safe::TryReadField(&world->StreamingLevels, levels)) {
        return false;
    }

    // Do NOT BoundedTArrayCount / TryGetTArrayElement here — VirtualQuery on
    // StreamingLevels during PHYS_Falling hung Tick at tick.cb.0 (2026-07-21).
    int rawCount = 0;
    Classes::ULevelStreaming **buffer = nullptr;
    if (!RawStreamingLevelsHeader(levels, rawCount, buffer) || rawCount <= 0 ||
        !buffer) {
        return false;
    }
    const size_t count = static_cast<size_t>(rawCount > 64 ? 64 : rawCount);
    ctx.streamingCount = count;
    for (size_t i = 0; i < count; ++i) {
        Classes::ULevelStreaming *streaming = nullptr;
        if (!RawStreamingLevelAt(buffer, i, streaming) || !streaming ||
            reinterpret_cast<uintptr_t>(streaming) < 0x10000u) {
            continue;
        }

        Classes::ULevel *loaded = nullptr;
        if (!MeSdk::Safe::TryReadField(&streaming->LoadedLevel, loaded) ||
            !loaded) {
            continue;
        }
        ++ctx.loadedCount;

        Classes::FName packageName = {};
        std::string package;
        if (!MeSdk::Safe::TryReadField(&streaming->PackageName, packageName) ||
            !MeSdk::Safe::TryGetFNameString(packageName, package)) {
            continue;
        }

        const auto candidate = NormalizeLevelName(package);
        if (IsMenuLevelName(candidate)) {
            continue;
        }

        ctx.level = candidate;
        return true;
    }

    return false;
}

bool TryDetectGameplayPawn(LevelProbeContext &ctx) {
    // Prefer cached lookups — update=true walks GObjects and stalls the game.
    if (auto *enginePawn = Engine::GetPlayerPawn(false)) {
        MeSdk::Safe::Gameplay::PawnPoseSnapshot pose = {};
        ctx.hadPawn =
            MeSdk::Safe::Gameplay::TryReadPawnPose(enginePawn, pose) && pose.valid;
        if (ctx.hadPawn) {
            return true;
        }
    }

    auto *controller = Engine::GetPlayerController(false);
    if (!controller ||
        reinterpret_cast<uintptr_t>(controller) < 0x10000u) {
        return false;
    }

    Classes::APawn *pawnBase = nullptr;
    if (!MeSdk::Safe::TryReadField(&controller->AcknowledgedPawn, pawnBase) ||
        !pawnBase) {
        pawnBase = nullptr;
        MeSdk::Safe::TryReadField(&controller->Pawn, pawnBase);
    }

    if (!pawnBase ||
        reinterpret_cast<uintptr_t>(pawnBase) < 0x10000u) {
        return false;
    }

    auto *pawn = static_cast<Classes::ATdPlayerPawn *>(pawnBase);
    MeSdk::Safe::Gameplay::PawnPoseSnapshot pose = {};
    ctx.hadPawn = MeSdk::Safe::Gameplay::TryReadPawnPose(pawn, pose) && pose.valid;
    return ctx.hadPawn;
}

void ReadCurrentWorldLevelThunk(void *data) {
    auto *ctx = static_cast<LevelProbeContext *>(data);
    if (!ctx) {
        return;
    }

    // Prefer cached world — GetWorld(true) walks ~87k GObjects (10-40s) and
    // freezes Mirror's Edge when called from multiplayer inject / late_init.
    auto *world = Engine::GetWorld(false);
    if (!world) {
        world = MeSdk::Safe::Gui::TryFindActiveWorldInfo(false);
    }
    if (!world ||
        reinterpret_cast<uintptr_t>(world) < 0x10000u) {
        // Do not invent synthetic "gameplay" here — callers that need a
        // placeholder already set client.Level via Set Gameplay UI.
        (void)TryDetectGameplayPawn(*ctx);
        return;
    }
    ctx->hadWorld = true;

    if (TryReadLoadedStreamingLevel(world, *ctx)) {
        ClientLogf("client: level probe streaming level=%s", ctx->level.c_str());
        ctx->ok = true;
        return;
    }

    std::string mapName;
    if (!MeSdk::Safe::Gui::TryGetWorldMapName(world, mapName)) {
        (void)TryDetectGameplayPawn(*ctx);
        return;
    }

    auto level = NormalizeLevelName(std::move(mapName));
    if (IsMenuLevelName(level)) {
        (void)TryDetectGameplayPawn(*ctx);
        return;
    }

    ctx->ok = true;
    ctx->level = level;
}

bool TryReadCurrentWorldLevel(std::string &level) {
    if (g_isClientBackgroundThread || g_isClientListenerThread) {
        return false;
    }

    LevelProbeContext ctx = {};
    DWORD exceptionCode = 0;
    if (!PluginSehGuard::InvokeVoid("multiplayer_level_probe",
                                    "client.cpp:TryReadCurrentWorldLevel",
                                    ReadCurrentWorldLevelThunk, &ctx,
                                    &exceptionCode)) {
        const auto faults = g_levelProbeFaults.fetch_add(1) + 1;
        if (faults <= 3) {
            ClientLogf("client: level probe fault exception=0x%08lx",
                       static_cast<unsigned long>(exceptionCode));
            MpDebugLog("client.cpp:TryReadCurrentWorldLevel", "fault",
                       "H-LEVEL", static_cast<uintptr_t>(exceptionCode),
                       faults, 0);
        }
        return false;
    }

    if (!ctx.ok) {
        const auto now = GetTickCount64();
        auto last = g_lastLevelProbeDiagnostic.load(std::memory_order_relaxed);
        if (now - last >= 2000 &&
            g_lastLevelProbeDiagnostic.compare_exchange_strong(
                last, now, std::memory_order_relaxed)) {
            ClientLogf("client: level probe no level world=%d streaming=%llu loaded=%llu pawn=%d current=%s",
                       ctx.hadWorld ? 1 : 0,
                       static_cast<unsigned long long>(ctx.streamingCount),
                       static_cast<unsigned long long>(ctx.loadedCount),
                       ctx.hadPawn ? 1 : 0, client.Level.c_str());
        }
        return false;
    }

    g_levelProbeFaults.store(0);
    level = std::move(ctx.level);
    return true;
}

static bool TryProbeConcreteWorldLevel(std::string &outLevel) {
    outLevel.clear();
    std::string probed;
    if (!TryReadCurrentWorldLevel(probed)) {
        return false;
    }
    if (probed.empty() || probed == "gameplay" || IsMenuLevelName(probed)) {
        return false;
    }
    outLevel = std::move(probed);
    return true;
}

bool TryUpgradeGameplayLevelName() {
    if (client.Level != "gameplay") {
        return false;
    }

    std::string real;
    if (!TryProbeConcreteWorldLevel(real)) {
        return false;
    }

    const auto oldLevel = client.Level;
    client.Level = real;
    loading.store(false);

    players.Mutex.lock();
    for (auto *p : players.List) {
        if (!p) {
            continue;
        }
        if (p->Level.empty() || p->Level == "tdmainmenu" ||
            p->Level == "gameplay") {
            p->Level = real;
        }
    }
    players.Mutex.unlock();

    NotifyServerLevel();
    UpdateHarnessSnapshot();
    ClientLogf("client: upgraded gameplay level %s -> %s", oldLevel.c_str(),
               real.c_str());
    return true;
}

void SyncCurrentLevelFromWorld() {
    // Disabled: active UE3 world/pawn probing is unsafe at the main menu in
    // hosted split mode and can raise access violations. Level sync is driven
    // by explicit UI actions and level-load callbacks instead.
}

void QueueLevelProbe() {
    // Automatic UE3 world/pawn probing has proven unsafe at the main menu in
    // hosted split mode. Keep this as a disabled extension point; level changes
    // are currently driven by explicit UI actions or level-load callbacks.
}

void SyncCurrentLevelIfInGameplay() {
    SyncCurrentLevelFromWorld();
    UpdateHarnessSnapshot();

    if (ModHost::IsAttached() && !client.Level.empty() &&
        client.Level != "tdmainmenu") {
        QueueActivateHostedGameplay();
    }
}

bool TryEnsureGameplayHooksForSetGameplay() {
    if (Engine::AreGameplayHooksInstalled()) {
        return true;
    }

    // Prefer: enter Story first, then Set Gameplay (hooks wrap LoadMap only for
    // later transitions). Menu Set Gameplay before Story still installs early —
    // that path can hitch on first Story entry; harness must delay FORCE until
    // after START_NEW_GAME settles (KI-2026-005).
    ClientLog("client: installing gameplay hooks for Set Gameplay");
    const bool ok = Engine::EnsureGameplayHooks();
    ClientLogf("client: gameplay hooks install result=%d installed=%d", ok ? 1 : 0,
               Engine::AreGameplayHooksInstalled() ? 1 : 0);

    if (Engine::AreGameplayHooksInstalled()) {
        EnsureClientGameplayCallbacks();
    }

    return Engine::AreGameplayHooksInstalled();
}

void HandlePostLevelGameplayEntry() {
    OutputDebugStringA("=== HandlePostLevelGameplayEntry ENTER ===\n");
    ClientLogf("client: post_level_load deferred level=%s hosted=%d live=%d connected=%d players=%d loading=1",
               client.Level.c_str(), ModHost::IsAttached() ? 1 : 0,
               Engine::IsHostedGameplayLive() ? 1 : 0, connected.load() ? 1 : 0,
               static_cast<int>(players.List.size()));

    if (client.Level.empty() || client.Level == "tdmainmenu") {
        OutputDebugStringA("=== HandlePostLevelGameplayEntry mainmenu SKIP ===\n");
        loading.store(false);
        return;
    }

    // Stay in loading until pawn is gameplay-safe (intro/cinematics may still
    // be running even though the level hook already fired).
    loading.store(true);

    if (ModHost::IsAttached()) {
        OutputDebugStringA("=== HandlePostLevelGameplayEntry HOSTED path ===\n");
        if (!Engine::AreGameplayHooksInstalled()) {
            ClientLog("client: post_level_load deferred abort gameplay hooks missing");
            loading.store(false);
            return;
        }
        // Always queue hosted activation; if no remote players are present
        // yet, activation waits for pawn readiness and bots will be spawned
        // when they arrive (via TrySpawnPendingRemotePlayers).  Engine::GetWorld
        // uses IsPlausibleUObject guards so the probe is safe now.
        QueueActivateHostedGameplay();
        ClientLog("client: post_level_load hosted activation queued");
        return;
    }

    if (!connected.load()) {
        loading.store(false);
        return;
    }

    if (!Engine::AreGameplayHooksInstalled()) {
        ClientLog("client: post_level_load deferred abort gameplay hooks missing");
        loading.store(false);
        return;
    }

    Engine::RequestGameplayActivation(CompleteMultiplayerRemoteLevelActivation);
}

void InstallClientRuntimeHooks() {
    MpDebugLog("client.cpp:InstallClientRuntimeHooks", "enter", "H-HOOKS");
    // Level/death hooks and menu-safe render overlay only.  Tick/bones callbacks
    // are deferred to EnsureClientGameplayCallbacks() immediately before
    // EnsureGameplayHooks() so we never push_back into callback vectors while
    // TickHook/BonesTickHook are iterating them.
    Engine::ClearFeaturePluginCallbacks();
    EnsureClientRenderHook();

    if (!ModHost::IsAttached()) {
        Engine::OnInput([](unsigned int &msg, int keycode) {
            if (!chat.Focused && msg == WM_KEYDOWN && keycode == chat.Keybind) {
                chat.Focused = true;
                Engine::BlockInput(true);
            }
            // B1: near-distance interact (TCP chat/UX only — KI-012 safe).
            if (!chat.Focused && msg == WM_KEYDOWN &&
                keycode == interactKeybind && connected.load() &&
                !loading.load()) {
                TrySendNearestInteract("wave");
            }
        });

        Engine::OnSuperInput([](unsigned int &msg, int keycode) {
            if (chat.Focused) {
                if (msg == WM_KEYUP && keycode == VK_RETURN) {
                    SendChatInput();
                    chat.Focused = false;
                    Engine::BlockInput(false);
                } else if (msg == WM_KEYUP && keycode == VK_ESCAPE) {
                    chat.Focused = false;
                    Engine::BlockInput(false);
                }
            }
        });
    }

    Engine::OnPreLevelLoad([](const wchar_t *levelNameW) {
        if (ModHost::IsAttached()) {
            Engine::SetHostedGameplayLive(false);
        }

        const auto level = NormalizeLevelName(WideToUtf8(levelNameW));
        ClientLogf("client: pre level load %s", level.c_str());

        // Do NOT Engine::Despawn here — LoadMap is about to tear the world
        // down; Despawn during PreLevelLoad stalls / crashes entry. Just drop
        // actor pointers; remotes respawn after PostLevelLoad activation.
        players.Mutex.lock();
        loading.store(true);
        client.Level = level;
        for (const auto &p : players.List) {
            if (p) {
                ClearPlayerRemoteVisual(p);
            }
        }
        players.Mutex.unlock();
        UpdateHarnessSnapshot();
    });

    Engine::OnPostLevelLoad([](const wchar_t *levelNameW) {
        OutputDebugStringA("=== OnPostLevelLoad ENTER ===\n");
        const auto level = NormalizeLevelName(WideToUtf8(levelNameW));
        MpDebugLog("client.cpp:OnPostLevelLoad", "enter", "H-LEVEL",
                   static_cast<uintptr_t>(level.size()), 0, 0);
        ClientLogf("client: post level load %s", level.c_str());
        OutputDebugStringA("=== OnPostLevelLoad AFTER first log ===\n");
        if (!level.empty()) {
            client.Level = level;
        }

        if (client.Level != "tdmainmenu") {
            ClientLogf("client: post_level_load enter level=%s hosted=%d live=%d connected=%d players=%d",
                       client.Level.c_str(), ModHost::IsAttached() ? 1 : 0,
                       Engine::IsHostedGameplayLive() ? 1 : 0,
                       connected.load() ? 1 : 0,
                       static_cast<int>(players.List.size()));
            OutputDebugStringA("=== OnPostLevelLoad QUEUEING TASK ===\n");
            // Keep loading=true until activation completes; intro cinematics can
            // still be running after the level hook fires.
            QueueClientEngineTask([]() { HandlePostLevelGameplayEntry(); });
        } else {
            loading.store(false);
        }

        if (connected.load()) {
            NotifyServerLevel();
        }
        OutputDebugStringA("=== OnPostLevelLoad EXIT ===\n");
    });

    Engine::OnPreDeath([]() {
        players.Mutex.lock_shared();
        loading.store(true);
        players.Mutex.unlock_shared();
    });

    Engine::OnPostDeath([]() {
        players.Mutex.lock_shared();
        loading.store(false);

        for (const auto &p : players.List) {
            if (!PlayerHasRemoteVisual(p) &&
                LevelsCompatible(p->Level, client.Level)) {
                QueueSpawnPlayerIfReady(p);
            }
        }

        players.Mutex.unlock_shared();
    });

    // Do NOT EnsureGameplayHooks at plugin init (KI-2026-005): wrapping
    // LoadMap before Story entry stalls EndScene / white-frames. Hooks install
    // from Set Gameplay / FORCE_HOSTED_LIVE via TryEnsureGameplayHooksForSetGameplay.
    ClientLog("client: gameplay hooks deferred until Set Gameplay");

    // Late-init used to call TryReadCurrentWorldLevel() which did
    // GetWorld(true)/GetPlayerPawn(true) on the game thread — a full GObjects
    // walk that stalls Mirror's Edge for tens of seconds and still usually
    // fails (menu WorldInfo, pawn=0) when injected mid-level.
    // Level sync is instead: Set Gameplay UI, OnPostLevelLoad, or adopting
    // remote bot levels once hosted live.
    if (!client.Level.empty() && client.Level != "tdmainmenu") {
        ClientLogf("client: late_init keep level=%s (no heavy probe)",
                   client.Level.c_str());
    } else {
        ClientLog("client: late_init skip heavy world probe — open Multiplayer "
                  "and click Set Gameplay if already in a level");
    }

    MpDebugLog("client.cpp:InstallClientRuntimeHooks", "hooks_done", "H-HOOKS");
}
} // namespace ClientInternal
