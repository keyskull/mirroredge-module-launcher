#include <Windows.h>
#include <Psapi.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <mutex>
#include <vector>

#include "engine.h"
#include "debug_trace.h"
#include "engine_core_bridge.h"
#include "mod_host_api.h"
#include "hook.h"
#include "module_contract.h"
#include "me_sdk/runtime/init.h"
#include "timing_constants.h"
#include "me_sdk/runtime/safe_access.h"
#include "me_sdk/runtime/safe_gui.h"
#include "me_sdk/runtime/pattern.h"
#include "me_sdk/patterns/hooks.h"
#include "win_input.h"

#include "plugin_ui.h"
#include "engine_internal.h"
#include "engine_presentation_internal.h"

static void ReportInitFailure(const char *message) {
    EngineDebugTrace::Log(message);
}

// Freeze breadcrumb ring (see engine_internal.h). A memory-mapped file lets the
// external watchdog read the last main-thread phase when the game hangs, with
// no per-call file syscalls. Only main/game + EndScene paths call this; keeping
// background threads out means a hang preserves the culprit entry in the ring.
void EngineInternal::SetPhase(const char *phase) {
    static const int kSlots = 48;
    static const int kSlotSize = 96;
    static const int kMapSize = kSlots * kSlotSize;
    static std::atomic<char *> g_view{nullptr};
    static std::atomic<unsigned long> g_seq{0};
    static std::once_flag g_once;

    std::call_once(g_once, []() {
        char tmp[MAX_PATH] = {};
        if (!GetTempPathA(sizeof(tmp), tmp)) {
            return;
        }
        char path[MAX_PATH] = {};
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%smirroredge-phase.bin", tmp);
        HANDLE f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            return;
        }
        HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, kMapSize,
                                      nullptr);
        CloseHandle(f);
        if (!m) {
            return;
        }
        void *v = MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, kMapSize);
        if (!v) {
            return;
        }
        memset(v, ' ', kMapSize);
        g_view.store(static_cast<char *>(v));
    });

    char *view = g_view.load();
    if (!view || !phase) {
        return;
    }

    const unsigned long seq = g_seq.fetch_add(1);
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    char line[kSlotSize] = {};
    int len = _snprintf_s(line, sizeof(line), _TRUNCATE,
                          "seq=%08lu t=%02u:%02u:%02u.%03u tid=%lu %s", seq,
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                          GetCurrentThreadId(), phase);
    if (len < 0 || len > kSlotSize - 1) {
        len = kSlotSize - 1;
    }
    char *slot = view + (static_cast<size_t>(seq % kSlots) * kSlotSize);
    memset(slot, ' ', kSlotSize);
    memcpy(slot, line, static_cast<size_t>(len));
    slot[kSlotSize - 1] = '\n';
}

static HANDLE modReadyEvent = nullptr;

static const wchar_t *ModReadyEventName() { return MMOD_READY_EVENT_NAME; }

Classes::UTdGameEngine *Engine::GetEngine(bool update) {
    static Classes::UTdGameEngine *cache = nullptr;

    if (!cache || update) {
        const auto &objects = Classes::UObject::GetGlobalObjects();
        for (auto i = 0UL; i < objects.Num(); ++i) {
            const auto object = objects.GetByIndex(i);

            if (!(object &&
                  object->IsA(Classes::UTdGameEngine::StaticClass()))) {

                continue;
            }

            if (object->Outer->GetName() == "Transient") {
                cache = static_cast<Classes::UTdGameEngine *>(object);
                return cache;
            }
        }
    }

    return cache;
}

Classes::UTdGameViewportClient *Engine::GetViewportClient(bool update) {
    static Classes::UTdGameViewportClient *cache = nullptr;

    if (!cache || update) {
        auto engine = GetEngine(update);
        if (engine) {
            cache = static_cast<Classes::UTdGameViewportClient *>(
                engine->GameViewport);
        }
    }

    return cache;
}

Classes::UTdConsole *Engine::GetConsole(bool update) {
    static Classes::UTdConsole *cache = nullptr;

    if (!cache || update) {
        auto viewportClient = GetViewportClient(update);
        if (viewportClient) {
            cache = static_cast<Classes::UTdConsole *>(
                viewportClient->ViewportConsole);
        }
    }

    return cache;
}

void Engine::ExecuteCommand(const wchar_t *command) {
    EngineInternal::commands.Mutex.lock();
    EngineInternal::commands.Queue.push_back(command);
    EngineInternal::commands.Mutex.unlock();
}

bool Engine::RunConsoleCommandNow(const wchar_t *command) {
    if (!command || !command[0]) {
        return false;
    }

    if (auto *console = GetConsole(true)) {
        console->ConsoleCommand(command);
        return true;
    }

    auto *viewport = GetViewportClient(true);
    if (!viewport) {
        return false;
    }

    static Classes::UFunction *fn = nullptr;
    if (!fn) {
        fn = Classes::UObject::FindObject<Classes::UFunction>(
            "Function Engine.GameViewportClient.ConsoleCommand");
    }
    if (!fn) {
        return false;
    }

    Classes::UGameViewportClient_ConsoleCommand_Params params = {};
    params.Command = command;
    viewport->ProcessEvent(fn, &params);
    return true;
}

bool Engine::LoadLevel(const wchar_t *mapName) {
    if (!mapName || !mapName[0]) {
        return false;
    }

    auto *controller = GetPlayerController(true);
    if (!controller) {
        return false;
    }

    static Classes::UFunction *fn = nullptr;
    if (!fn) {
        fn = Classes::UObject::FindObject<Classes::UFunction>(
            "Function Engine.PlayerController.ClientTravel");
    }
    if (!fn) {
        return false;
    }

    Classes::APlayerController_ClientTravel_Params params = {};
    params.URL = mapName;
    params.TravelType = Classes::ETravelType::TRAVEL_Absolute;
    params.bSeamless = false;
    controller->ProcessEvent(fn, &params);
    return true;
}

namespace {

void LogStartGameStep(const char *step, int detail) {
    char tmpPath[MAX_PATH] = {};
    GetTempPathA(sizeof(tmpPath), tmpPath);
    char logPath[MAX_PATH] = {};
    snprintf(logPath, sizeof(logPath), "%smirroredge-startgame.log", tmpPath);
    HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char line[256] = {};
        int len = snprintf(line, sizeof(line), "%s=%d\r\n", step, detail);
        WriteFile(h, line, static_cast<DWORD>(len), nullptr, nullptr);
        CloseHandle(h);
    }
}

// ViewportClient -> UIController -> SceneClient -> GetGameDataStore().
Classes::UUIDataStore_TdGameData *ResolveTdGameDataStore() {
    auto *vp = Engine::GetViewportClient(true);
    if (!vp) {
        LogStartGameStep("vp_null", 1);
        return nullptr;
    }
    LogStartGameStep("vp_ok", (int)(uintptr_t)vp & 0xFFFF);

    if (!vp->UIController) {
        auto raw = *reinterpret_cast<Classes::UUIInteraction **>(
            reinterpret_cast<uint8_t *>(vp) + 0x5C);
        LogStartGameStep("uic_null", (int)(uintptr_t)raw & 0xFFFF);
        return nullptr;
    }
    LogStartGameStep("uic_ok", (int)(uintptr_t)vp->UIController & 0xFFFF);

    auto *sc = vp->UIController->SceneClient;
    if (!sc) {
        LogStartGameStep("sc_null", 0);
        return nullptr;
    }
    LogStartGameStep("sc_ok", (int)(uintptr_t)sc & 0xFFFF);

    static Classes::UFunction *getFn = nullptr;
    if (!getFn) {
        getFn = Classes::UObject::FindObject<Classes::UFunction>(
            "Function TdGame.TdGameUISceneClient.GetGameDataStore");
    }
    if (!getFn) {
        LogStartGameStep("getFn_null", 0);
        return nullptr;
    }
    LogStartGameStep("getFn_ok", 1);

    Classes::UTdGameUISceneClient_GetGameDataStore_Params getParams = {};
    sc->ProcessEvent(getFn, &getParams);
    auto *gameData = getParams.ReturnValue;
    if (!gameData) {
        LogStartGameStep("gameData_null", 0);
        return nullptr;
    }
    LogStartGameStep("gameData_ok", (int)(uintptr_t)gameData & 0xFFFF);
    return gameData;
}

} // namespace

bool Engine::StartGameFromMenu(const wchar_t *mapName) {
    if (!mapName || !mapName[0]) {
        return false;
    }

    auto *gameData = ResolveTdGameDataStore();
    if (!gameData) {
        return false;
    }

    static Classes::UFunction *startFn = nullptr;
    if (!startFn) {
        startFn = Classes::UObject::FindObject<Classes::UFunction>(
            "Function TdGame.UIDataStore_TdGameData.StartGame");
    }
    if (!startFn) {
        LogStartGameStep("startFn_null", 0);
        return false;
    }
    LogStartGameStep("startFn_ok", 1);

    Classes::UUIDataStore_TdGameData_StartGame_Params startParams = {};
    startParams.LevelName = mapName;
    startParams.CheckpointName = L"";
    startParams.GameMode = L"";
    startParams.URL = L"";
    startParams.bShouldSaveCheckpointProgress = false;
    startParams.bAllowSPLevelAchievements = false;

    gameData->ProcessEvent(startFn, &startParams);
    LogStartGameStep("start_pe_done", 1);
    return true;
}

bool Engine::StartNewGameFromMenu(bool playCutScene) {
    auto *gameData = ResolveTdGameDataStore();
    if (!gameData) {
        return false;
    }

    static Classes::UFunction *startFn = nullptr;
    if (!startFn) {
        startFn = Classes::UObject::FindObject<Classes::UFunction>(
            "Function TdGame.UIDataStore_TdGameData.StartNewGameWithTutorial");
    }
    if (!startFn) {
        LogStartGameStep("newGameFn_null", 0);
        return false;
    }
    LogStartGameStep("newGameFn_ok", playCutScene ? 1 : 0);

    Classes::UUIDataStore_TdGameData_StartNewGameWithTutorial_Params params = {};
    params.bPlayCutScene = playCutScene;
    gameData->ProcessEvent(startFn, &params);
    LogStartGameStep("newGame_pe_done", 1);
    return true;
}

// Prewarmed UClass pointers — never call StaticClass from Tick/EndScene.
Classes::UClass *g_tdPlayerControllerClass = nullptr;
Classes::UClass *g_tdPlayerPawnClass = nullptr;
Classes::UClass *g_pawnClass = nullptr;

void PrewarmTdPlayerControllerClass() {
    if (g_tdPlayerControllerClass) {
        return;
    }
    g_tdPlayerControllerClass = Classes::ATdPlayerController::StaticClass();
}

void PrewarmTdPlayerPawnClass() {
    if (g_tdPlayerPawnClass) {
        return;
    }
    g_tdPlayerPawnClass = Classes::ATdPlayerPawn::StaticClass();
}

void PrewarmPawnClass() {
    if (g_pawnClass) {
        return;
    }
    g_pawnClass = Classes::APawn::StaticClass();
}

static bool IsControllablePlayerPawn(Classes::ATdPlayerPawn *pawn) {
    if (!pawn || reinterpret_cast<uintptr_t>(pawn) < 0x10000u) {
        return false;
    }

    // SEH delete flags only — do NOT TryIsA/object->IsA here. SuperField walks
    // can infinite-loop and freeze Tick at tick.callbacks after idle.done while
    // host is PHYS_Falling (rem=2 sp=0, no drain ENTER; 2026-07-21). Fallback
    // below used to return true anyway, so TryIsA only added hang risk.
    __try {
        if (pawn->bDeleteMe || pawn->bPendingDelete) {
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

static bool ControllerHasControllablePawn(
    Classes::ATdPlayerController *playerController) {
    if (!MeSdk::Safe::IsPlausibleUObject(playerController)) {
        return false;
    }

    Classes::APawn *ackPawn = nullptr;
    if (MeSdk::Safe::TryReadField(&playerController->AcknowledgedPawn, ackPawn)) {
        if (IsControllablePlayerPawn(
                static_cast<Classes::ATdPlayerPawn *>(ackPawn))) {
            return true;
        }
    }

    Classes::APawn *genericPawn = nullptr;
    if (MeSdk::Safe::TryReadField(&playerController->Pawn, genericPawn)) {
        if (IsControllablePlayerPawn(
                static_cast<Classes::ATdPlayerPawn *>(genericPawn))) {
            return true;
        }
    }

    return false;
}

static Classes::AWorldInfo *g_worldCache = nullptr;
static Classes::ATdPlayerController *g_playerControllerCache = nullptr;
static float g_seedHostPosX = 0.f;
static float g_seedHostPosY = 0.f;
static float g_seedHostPosZ = 0.f;
static unsigned short g_seedHostYaw = 0;
static bool g_hasSeedHostPose = false;
static ULONGLONG g_idlePcSeedDoneMs = 0;
static std::atomic<bool> g_idlePcSeedCommitted{false};

static Classes::AWorldInfo *PeekWorldCache();
static Classes::ATdPlayerController *PeekPlayerControllerCache();
static Classes::AWorldInfo *
RawReadWorldInfoFromPc(Classes::ATdPlayerController *playerController);

namespace EngineInternal {

void RequestIdlePcSeed() {
    if (g_idlePcSeedCommitted.load(std::memory_order_acquire)) {
        return;
    }
    if (!MeSdk::Safe::Gui::TryFindTdGameEngine(false)) {
        return;
    }
    pendingIdlePcSeed.store(true, std::memory_order_release);
}

void DrainPendingIdlePcSeedOnGameThread() {
    if (!pendingIdlePcSeed.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    Engine::CommitIdleWarmPlayerSeed();
}

bool IsIdlePcSeedQuietPeriod() {
    if (g_idlePcSeedDoneMs == 0) {
        return false;
    }
    const ULONGLONG age = GetTickCount64() - g_idlePcSeedDoneMs;
    // Keep ≥1500ms — 750ms correlated with IsHungAppWindow after IDLE_PC_SEED
    // when FORCE_HOSTED_LIVE raced an empty world (2026-07-22).
    return age < 1500ull;
}

void CommitIdleWarmWorldAndPoseFromPc() {
    auto *pc = PeekPlayerControllerCache();
    if (!pc) {
        return;
    }
    if (!PeekWorldCache()) {
        if (auto *worldInfo = RawReadWorldInfoFromPc(pc)) {
            g_worldCache = worldInfo;
        }
    }
    if (g_hasSeedHostPose) {
        return;
    }
    Classes::ACamera *cam = nullptr;
    if (!MeSdk::Safe::TryReadField(&pc->PlayerCamera, cam) || !cam ||
        reinterpret_cast<uintptr_t>(cam) < 0x10000u) {
        return;
    }
    Classes::FVector loc = {};
    Classes::FRotator rot = {};
    if (MeSdk::Safe::TryReadField(&cam->Location, loc)) {
        g_seedHostPosX = loc.X;
        g_seedHostPosY = loc.Y;
        g_seedHostPosZ = loc.Z;
        g_hasSeedHostPose = true;
    }
    if (MeSdk::Safe::TryReadField(&cam->Rotation, rot)) {
        g_seedHostYaw = static_cast<unsigned short>(rot.Yaw % 0x10000);
    }
}

void WarmTdGameEngineOnGameThread() {
    if (g_idlePcSeedCommitted.load(std::memory_order_acquire)) {
        return;
    }
    if (MeSdk::Safe::Gui::TryFindTdGameEngine(false)) {
        Engine::CommitIdleWarmPlayerSeed();
        if (g_idlePcSeedCommitted.load(std::memory_order_acquire)) {
            SetPhase("tick.warm.engine.done");
        } else {
            SetPhase("tick.warm.engine.wait_world");
        }
        return;
    }
    static ULONGLONG s_lastTickEngineWarmMs = 0;
    const ULONGLONG now = GetTickCount64();
    if ((now - s_lastTickEngineWarmMs) < 500ull) {
        return;
    }
    s_lastTickEngineWarmMs = now;

    if (auto *pc = Engine::GetPlayerController(false)) {
        MeSdk::Safe::Gui::TrySeedTdGameEngineFromPlayerController(pc);
    }
    if (MeSdk::Safe::Gui::TryFindTdGameEngine(false)) {
        Engine::CommitIdleWarmPlayerSeed();
        if (g_idlePcSeedCommitted.load(std::memory_order_acquire)) {
            SetPhase("tick.warm.engine.done");
        } else {
            SetPhase("tick.warm.engine.wait_world");
        }
        return;
    }

    SetPhase("tick.warm.engine.slice");
    // Soft raise vs 800 (do NOT use 2000/200 — hangs spawn). Pre-live warm +
    // 1100/500ms cuts worst-case GObjects scan without the banned cadence.
    MeSdk::Safe::Gui::TryWarmTdGameEngineIncremental(1100);
    if (MeSdk::Safe::Gui::TryFindTdGameEngine(false)) {
        Engine::CommitIdleWarmPlayerSeed();
        if (g_idlePcSeedCommitted.load(std::memory_order_acquire)) {
            SetPhase("tick.warm.engine.done");
        } else {
            SetPhase("tick.warm.engine.wait_world");
        }
    } else {
        SetPhase("tick.warm.engine.cont");
    }
}

} // namespace EngineInternal

static Classes::AWorldInfo *PeekWorldCache() {
    // Pointer-only peek on Tick/EndScene — IsPlausibleUObject VirtualQuerys and
    // has hung pose ticks after TdEngine warm (2026-07-20/21).
    if (g_worldCache &&
        reinterpret_cast<uintptr_t>(g_worldCache) >= 0x10000u) {
        return g_worldCache;
    }
    g_worldCache = nullptr;
    return nullptr;
}

static Classes::ATdPlayerController *PeekPlayerControllerCache() {
    // Pointer-only — do not IsPlausibleUObject/TryIsA clear on hot path
    // (false negative / hang left rem>0/sp=0 while GET_STATUS still had pose).
    if (g_playerControllerCache &&
        reinterpret_cast<uintptr_t>(g_playerControllerCache) >= 0x10000u) {
        return g_playerControllerCache;
    }
    g_playerControllerCache = nullptr;
    return nullptr;
}

static bool RawControllerIsTdPc(Classes::AController *controller) {
    if (!controller || !g_tdPlayerControllerClass ||
        reinterpret_cast<uintptr_t>(controller) < 0x10000u) {
        return false;
    }
    Classes::UClass *cls = nullptr;
    __try {
        cls = controller->Class;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    for (int depth = 0; depth < 64 && cls; ++depth) {
        if (cls == g_tdPlayerControllerClass) {
            return true;
        }
        __try {
            cls = static_cast<Classes::UClass *>(cls->SuperField);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    return false;
}

static Classes::AWorldInfo *
RawReadWorldInfoFromPc(Classes::ATdPlayerController *pc) {
    if (!pc || reinterpret_cast<uintptr_t>(pc) < 0x10000u) {
        return nullptr;
    }
    Classes::AWorldInfo *worldInfo = nullptr;
    __try {
        worldInfo = pc->WorldInfo;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!worldInfo || reinterpret_cast<uintptr_t>(worldInfo) < 0x10000u) {
        return nullptr;
    }
    return worldInfo;
}

// Walk WorldInfo->ControllerList for a camera-bearing TdPlayerController.
// Safe on Tick and EndScene when the world cache is already warm — does not
// touch GamePlayers (verified hang after TdEngine warm, 2026-07-19).
static Classes::ATdPlayerController *
TrySeedPlayerControllerFromWorldCache() {
    auto *world = PeekWorldCache();
    if (!world) {
        return nullptr;
    }
    if (!g_tdPlayerControllerClass) {
        // Avoid StaticClass/FindClass from Tick/EndScene.
        return nullptr;
    }

    Classes::AController *controller = nullptr;
    if (!MeSdk::Safe::TryReadField(&world->ControllerList, controller) ||
        !controller) {
        return nullptr;
    }

    int guard = 0;
    for (Classes::AController *cur = controller; cur && guard < 64; ++guard) {
        if (reinterpret_cast<uintptr_t>(cur) < 0x10000u ||
            !RawControllerIsTdPc(cur)) {
            Classes::AController *next = nullptr;
            if (!MeSdk::Safe::TryReadField(&cur->NextController, next)) {
                break;
            }
            cur = next;
            continue;
        }

        auto *pc = static_cast<Classes::ATdPlayerController *>(cur);
        Classes::ACamera *cam = nullptr;
        if (!MeSdk::Safe::TryReadField(&pc->PlayerCamera, cam) || !cam ||
            reinterpret_cast<uintptr_t>(cam) < 0x10000u) {
            Classes::AController *next = nullptr;
            if (!MeSdk::Safe::TryReadField(&cur->NextController, next)) {
                break;
            }
            cur = next;
            continue;
        }

        // Do NOT call IsInMainMenu() here — ProcessEvent from Tick/EndScene
        // after world warm has hung the game thread (2026-07-20). Any
        // camera-bearing TdPC is good enough for pose/spawn seeding.
        g_playerControllerCache = pc;
        return g_playerControllerCache;
    }

    return nullptr;
}

Classes::AWorldInfo *Engine::GetWorld(bool update) {
    Classes::AWorldInfo *&cache = g_worldCache;

#ifdef _DEBUG
    // Diagnostic: log entry
    {
        char tmpPath[MAX_PATH] = {};
        if (GetTempPathA(sizeof(tmpPath), tmpPath)) {
            char logPath[MAX_PATH] = {};
            snprintf(logPath, sizeof(logPath), "%smirroredge-engine-spawn.log", tmpPath);
            HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                char line[256] = {};
                int len = snprintf(line, sizeof(line),
                    "GetWorld: ENTER update=%d cache=%p loading=%d\r\n",
                    update ? 1 : 0, static_cast<const void *>(cache),
                    EngineInternal::levelLoad.Loading ? 1 : 0);
                DWORD written = 0;
                WriteFile(h, line, len, &written, nullptr);
                CloseHandle(h);
            }
        }
    }
#endif

    if (EngineInternal::levelLoad.Loading) {
        // If Loading has been true for >10 seconds, the LevelLoadHook
        // may be permanently stuck (original LoadMap crashed/hung).
        // Auto-expire the flag to prevent permanent spawn deadlock.
        const ULONGLONG elapsed = GetTickCount64() - EngineInternal::levelLoad.loadingStartTick;
        if (elapsed < Timing::kMaxSpawnQueueAgeMs) {
            // update=false: still allow cache / PC seed (tutorial can be
            // playable while Loading flickers). Only skip GObjects walks.
            if (!update) {
                if (PeekWorldCache()) {
                    return cache;
                }
                if (auto *pc = PeekPlayerControllerCache()) {
                    if (auto *worldInfo = RawReadWorldInfoFromPc(pc)) {
                        cache = worldInfo;
                        return cache;
                    }
                }
                return nullptr;
            }
            return nullptr;
        }
        // Timed out — proceed with iteration below only if update==true
    }

    // update=false must NEVER walk GObjects. An empty cache used to fall
    // through into a 10-40s full iteration on the first Tick after Set
    // Gameplay set hosted live — freezing the game immediately.
    if (!update) {
        if (PeekWorldCache()) {
            return cache;
        }
        // Seed from Safe Gui active-world cache (filled by EndScene incremental
        // warm) — no GamePlayers, no full GObjects walk.
        if (auto *warmed = MeSdk::Safe::Gui::TryFindActiveWorldInfo(false)) {
            if (reinterpret_cast<uintptr_t>(warmed) >= 0x10000u) {
                cache = warmed;
                return cache;
            }
        }
        // Seed from cached PC only (no GetPlayerController — that can recurse
        // into GetWorld / GamePlayers). ControllerList seeding runs the other
        // direction once world is warm.
        if (auto *pc = PeekPlayerControllerCache()) {
            if (auto *worldInfo = RawReadWorldInfoFromPc(pc)) {
                cache = worldInfo;
                return cache;
            }
        }
        if (EngineInternal::inEndSceneSpawnDrain.load()) {
            return nullptr;
        }
        return nullptr;
    }

    if (!cache || update) {
        // Prevent concurrent GObjects iterations (87k+ objects takes
        // 10-40 seconds).  If another thread is already iterating, return
        // the current cache (possibly null) rather than contending.
        static std::mutex iterMutex;
        const bool locked = iterMutex.try_lock();
        if (!locked) {
            // Another thread is iterating — return cached value.
            return cache;
        }
        // Ensure unlock on all exit paths (structured or exception).
        struct UnlockGuard { std::mutex &m; ~UnlockGuard() { m.unlock(); } };
        UnlockGuard guard{iterMutex};

        EngineInternal::SetPhase("GetWorld.iterate.begin");

#ifdef _DEBUG
        // Log iteration start
        {
            char tmpPath[MAX_PATH] = {};
            if (GetTempPathA(sizeof(tmpPath), tmpPath)) {
                char logPath[MAX_PATH] = {};
                snprintf(logPath, sizeof(logPath), "%smirroredge-engine-spawn.log", tmpPath);
                HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    char line[128] = {};
                    int len = snprintf(line, sizeof(line), "GetWorld: ITERATE begin\r\n");
                    DWORD written = 0;
                    WriteFile(h, line, len, &written, nullptr);
                    CloseHandle(h);
                }
            }
        }
#endif

        const auto objects = Classes::UObject::GetGlobalObjects();
        const auto numObjects = objects.Num();
        EngineInternal::SetPhase("GetWorld.iterate.scan");

#ifdef _DEBUG
        // Log after GetGlobalObjects
        {
            char tmpPath[MAX_PATH] = {};
            if (GetTempPathA(sizeof(tmpPath), tmpPath)) {
                char logPath[MAX_PATH] = {};
                snprintf(logPath, sizeof(logPath), "%smirroredge-engine-spawn.log", tmpPath);
                HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    char line[128] = {};
                    int len = snprintf(line, sizeof(line),
                        "GetWorld: GOBJECTS numObjects=%u\r\n",
                        static_cast<unsigned int>(numObjects));
                    DWORD written = 0;
                    WriteFile(h, line, len, &written, nullptr);
                    CloseHandle(h);
                }
            }
        }
#endif

        Classes::AWorldInfo *bestPawnWorld = nullptr;
        Classes::AWorldInfo *bestTdWorld = nullptr;
        Classes::AWorldInfo *firstWorld = nullptr;
        
        for (auto i = 0UL; i < numObjects; ++i) {
            const auto object = objects.GetByIndex(i);
            if (!MeSdk::Safe::IsPlausibleUObject(object) ||
                !object->IsA(Classes::AWorldInfo::StaticClass())) {
                continue;
            }

            const auto world = static_cast<Classes::AWorldInfo *>(object);
            
            // Track first valid world as fallback (e.g. main menu has no
            // TdPlayerController but the world still exists for spawning).
            if (!firstWorld) {
                firstWorld = world;
            }

            auto *controller = world->ControllerList;
            if (!MeSdk::Safe::IsPlausibleUObject(controller)) {
                continue;
            }

            for (; controller;
                 controller = controller->NextController) {
                if (!MeSdk::Safe::IsPlausibleUObject(controller)) {
                    break;
                }

                if (!MeSdk::Safe::TryIsA(
                        controller,
                        Classes::ATdPlayerController::StaticClass())) {
                    continue;
                }

                auto *playerController =
                    static_cast<Classes::ATdPlayerController *>(controller);
                if (!bestTdWorld) {
                    bestTdWorld = world;
                }
                // Prefer the world whose player controller already owns Faith.
                // Picking the first TdPlayerController world often selects the
                // lingering tdmainmenu WorldInfo while tutorial_p is active.
                if (ControllerHasControllablePawn(playerController)) {
                    bestPawnWorld = world;
                    cache = bestPawnWorld;
                    EngineInternal::SetPhase("GetWorld.iterate.end.pawn");
                    return cache;
                }
            }
        }
        
        // Prefer any world with a TdPlayerController over a random first world.
        cache = bestPawnWorld ? bestPawnWorld
                              : (bestTdWorld ? bestTdWorld : firstWorld);
        EngineInternal::SetPhase("GetWorld.iterate.end");

#ifdef _DEBUG
        // Log iteration done
        {
            char tmpPath[MAX_PATH] = {};
            if (GetTempPathA(sizeof(tmpPath), tmpPath)) {
                char logPath[MAX_PATH] = {};
                snprintf(logPath, sizeof(logPath), "%smirroredge-engine-spawn.log", tmpPath);
                HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    char line[256] = {};
                    int len = snprintf(line, sizeof(line),
                        "GetWorld: FALLBACK firstWorld=%p numObjects=%u\r\n",
                        static_cast<const void *>(firstWorld),
                        static_cast<unsigned int>(numObjects));
                    DWORD written = 0;
                    WriteFile(h, line, len, &written, nullptr);
                    CloseHandle(h);
                }
            }
        }
#endif
        return cache;
    }

#ifdef _DEBUG
    // Log cache hit for debugging
    {
        char tmpPath[MAX_PATH] = {};
        if (GetTempPathA(sizeof(tmpPath), tmpPath)) {
            char logPath[MAX_PATH] = {};
            snprintf(logPath, sizeof(logPath), "%smirroredge-engine-spawn.log", tmpPath);
            HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                char line[128] = {};
                int len = snprintf(line, sizeof(line), "GetWorld: CACHED=%p\r\n", static_cast<const void *>(cache));
                DWORD written = 0;
                WriteFile(h, line, len, &written, nullptr);
                CloseHandle(h);
            }
        }
    }
#endif

    return cache;
}

static bool IsCurrentThreadStackAddress(const void *address) {
    if (!address) {
        return false;
    }

    const auto value = reinterpret_cast<std::uintptr_t>(address);
#if defined(_M_IX86)
    const auto high = static_cast<std::uintptr_t>(__readfsdword(0x04));
    const auto low = static_cast<std::uintptr_t>(__readfsdword(0x08));
#elif defined(_M_X64)
    const auto high = static_cast<std::uintptr_t>(__readgsqword(0x08));
    const auto low = static_cast<std::uintptr_t>(__readgsqword(0x10));
#else
    return false;
#endif
    return value >= low && value < high;
}

void Engine::CommitIdleWarmPlayerSeed() {
    if (g_idlePcSeedCommitted.load(std::memory_order_acquire)) {
        return;
    }
    auto *engine = MeSdk::Safe::Gui::TryFindTdGameEngine(false);
    if (!engine) {
        return;
    }

    // Game thread only — EndScene must use RequestIdlePcSeed instead.
    // Seed PC once; do not re-enter GamePlayers every tick (hangs after TdEngine
    // warm). Do not commit until WorldInfo is readable — committing with
    // world=null left rem=2/sp=0 and tick.idle freeze (2026-07-21 harness).
    Classes::ATdPlayerController *pc = PeekPlayerControllerCache();
    if (!pc) {
        if (!MeSdk::Safe::Gui::TrySeedPlayerControllerFromTdEngineGamePlayers(
                engine, pc) ||
            !pc) {
            return;
        }
        g_playerControllerCache = pc;
    }

    if (!PeekWorldCache()) {
        if (auto *worldInfo = RawReadWorldInfoFromPc(pc)) {
            g_worldCache = worldInfo;
        } else {
            static int softLogs = 0;
            if (softLogs < 8) {
                ++softLogs;
                char tmpPath[MAX_PATH] = {};
                if (GetTempPathA(sizeof(tmpPath), tmpPath)) {
                    char drainLog[MAX_PATH] = {};
                    snprintf(drainLog, sizeof(drainLog),
                             "%sspawn_drain_trace.txt", tmpPath);
                    HANDLE h = CreateFileA(
                        drainLog, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (h != INVALID_HANDLE_VALUE) {
                        SYSTEMTIME now = {};
                        GetLocalTime(&now);
                        char line[192] = {};
                        const int len = snprintf(
                            line, sizeof(line),
                            "%02u:%02u:%02u.%03u IDLE_PC_SEED_SOFT pc=%p "
                            "world=null (defer commit)\r\n",
                            now.wHour, now.wMinute, now.wSecond,
                            now.wMilliseconds, static_cast<void *>(pc));
                        if (len > 0) {
                            DWORD written = 0;
                            WriteFile(h, line, static_cast<DWORD>(len),
                                      &written, nullptr);
                        }
                        CloseHandle(h);
                    }
                }
            }
            return;
        }
    }

    // Defer world/camera pose reads until after quiet window — reading
    // PlayerCamera on the seed frame then calling tick.original hung the next
    // frame (rerun14).
    g_idlePcSeedDoneMs = GetTickCount64();
    g_idlePcSeedCommitted.store(true, std::memory_order_release);

    char tmpPath[MAX_PATH] = {};
    if (GetTempPathA(sizeof(tmpPath), tmpPath)) {
        char drainLog[MAX_PATH] = {};
        snprintf(drainLog, sizeof(drainLog), "%sspawn_drain_trace.txt", tmpPath);
        HANDLE h = CreateFileA(drainLog, FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            SYSTEMTIME now = {};
            GetLocalTime(&now);
            char line[192] = {};
            const int len = snprintf(
                line, sizeof(line),
                "%02u:%02u:%02u.%03u IDLE_PC_SEED pc=%p world=%p\r\n",
                now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                static_cast<void *>(pc), static_cast<void *>(PeekWorldCache()));
            if (len > 0) {
                DWORD written = 0;
                WriteFile(h, line, static_cast<DWORD>(len), &written, nullptr);
            }
            CloseHandle(h);
        }
    }
}

bool Engine::TryGetSeedHostPose(float *x, float *y, float *z,
                                unsigned short *yaw) {
    if (!g_hasSeedHostPose) {
        return false;
    }
    if (x) {
        *x = g_seedHostPosX;
    }
    if (y) {
        *y = g_seedHostPosY;
    }
    if (z) {
        *z = g_seedHostPosZ;
    }
    if (yaw) {
        *yaw = g_seedHostYaw;
    }
    return true;
}

unsigned long long Engine::GetIdlePcSeedAgeMs() {
    if (g_idlePcSeedDoneMs == 0) {
        return 0;
    }
    const ULONGLONG now = GetTickCount64();
    return now >= g_idlePcSeedDoneMs ? (now - g_idlePcSeedDoneMs) : 0;
}

Classes::ATdPlayerController *Engine::GetPlayerController(bool update) {
    Classes::ATdPlayerController *&cache = g_playerControllerCache;

    const bool loadingRecent =
        EngineInternal::levelLoad.Loading &&
        (GetTickCount64() - EngineInternal::levelLoad.loadingStartTick) <
            Timing::kMaxSpawnQueueAgeMs;

    // update=false: never GObjects-walk. PC cache is filled by game-thread
    // CommitIdleWarmPlayerSeed — EndScene uses RequestIdlePcSeed only.
    if (!update) {
        if (PeekPlayerControllerCache()) {
            return cache;
        }
        if (auto *fromWorld = TrySeedPlayerControllerFromWorldCache()) {
            return fromWorld;
        }
        if (EngineInternal::inEndSceneSpawnDrain.load()) {
            return nullptr;
        }
        return nullptr;
    }

    if (loadingRecent) {
        return GetPlayerController(false);
    }

    if (!cache || update) {
        cache = nullptr;
        EngineInternal::SetPhase("GetPC.iterate.begin");

        auto world = GetWorld(update);
        if (!MeSdk::Safe::IsPlausibleUObject(world)) {
            return nullptr;
        }
        if (world) {
            auto *controller = world->ControllerList;
            if (!MeSdk::Safe::IsPlausibleUObject(controller)) {
                controller = nullptr;
            }
            Classes::ATdPlayerController *fallback = nullptr;
            for (; controller;
                 controller = controller->NextController) {

                if (!MeSdk::Safe::IsPlausibleUObject(controller) ||
                    !MeSdk::Safe::TryIsA(
                        controller,
                        Classes::ATdPlayerController::StaticClass())) {
                    // Non-player controllers (AI, etc.) sit on the same list —
                    // skip them instead of aborting the walk.
                    continue;
                }

                auto playerController =
                    static_cast<Classes::ATdPlayerController *>(controller);

                if (!MeSdk::Safe::IsPlausibleUObject(
                        playerController->PlayerCamera)) {
                    continue;
                }

                if (playerController->IsInMainMenu()) {
                    // Keep a camera-bearing fallback: IsInMainMenu can stay
                    // true during tutorial/cinematic while Faith is already
                    // controllable, which previously made pawn probes always fail.
                    if (!fallback) {
                        fallback = playerController;
                    }
                    continue;
                }

                cache = playerController;
                break;
            }
            if (!cache) {
                cache = fallback;
            }
        }
        EngineInternal::SetPhase("GetPC.iterate.end");
    } else if (!MeSdk::Safe::IsPlausibleUObject(cache) ||
               !MeSdk::Safe::TryIsA(cache,
                                    Classes::ATdPlayerController::StaticClass())) {
        cache = nullptr;
    }

    return cache;
}

Classes::ATdPlayerPawn *Engine::GetPlayerPawn(bool update) {
    static Classes::ATdPlayerPawn *cache = nullptr;

    // update=false must not blank the cache during LevelLoad Loading —
    // that blocked EndScene spawn after tutorial was already playable.
    if (!update) {
        if (IsControllablePlayerPawn(cache)) {
            return cache;
        }
        auto controller = GetPlayerController(false);
        if (!controller ||
            reinterpret_cast<uintptr_t>(controller) < 0x10000u) {
            return nullptr;
        }
        Classes::APawn *ackPawn = nullptr;
        if (MeSdk::Safe::TryReadField(&controller->AcknowledgedPawn, ackPawn)) {
            auto pawn = static_cast<Classes::ATdPlayerPawn *>(ackPawn);
            if (IsControllablePlayerPawn(pawn)) {
                cache = pawn;
                return cache;
            }
        }
        Classes::APawn *genericPawn = nullptr;
        if (MeSdk::Safe::TryReadField(&controller->Pawn, genericPawn)) {
            auto pawn = static_cast<Classes::ATdPlayerPawn *>(genericPawn);
            if (IsControllablePlayerPawn(pawn)) {
                cache = pawn;
                return cache;
            }
        }
        return nullptr;
    }

    if (EngineInternal::levelLoad.Loading) {
        const ULONGLONG elapsed =
            GetTickCount64() - EngineInternal::levelLoad.loadingStartTick;
        if (elapsed < Timing::kMaxSpawnQueueAgeMs) {
            return GetPlayerPawn(false);
        }
    }

    if (!cache || update) {
        cache = nullptr;

        auto controller = GetPlayerController(update);
        if (!MeSdk::Safe::IsPlausibleUObject(controller)) {
            return nullptr;
        }
        if (controller) {
            Classes::APawn *ackPawn = nullptr;
            if (MeSdk::Safe::TryReadField(&controller->AcknowledgedPawn,
                                           ackPawn)) {
                auto pawn = static_cast<Classes::ATdPlayerPawn *>(ackPawn);
                if (IsControllablePlayerPawn(pawn)) {
                    cache = pawn;
                    return cache;
                }
            }

            Classes::APawn *genericPawn = nullptr;
            if (MeSdk::Safe::TryReadField(&controller->Pawn,
                                           genericPawn)) {
                auto pawn =
                    static_cast<Classes::ATdPlayerPawn *>(genericPawn);
                if (IsControllablePlayerPawn(pawn)) {
                    cache = pawn;
                    return cache;
                }
            }

            return nullptr;
        }
    } else if (!IsControllablePlayerPawn(cache)) {
        cache = nullptr;
    }

    return cache;
}

bool Engine::CanSafelyUsePlayerPawn() {
    if (!EngineInternal::modReady || EngineInternal::levelLoad.Loading) {
        return false;
    }

    // Never refresh from here — callers on Tick/Set Gameplay must stay offline
    // from GObjects. Warm caches elsewhere with an explicit throttled update.
    const auto controller = GetPlayerController(false);
    if (!controller) {
        return false;
    }

    return GetPlayerPawn(false) != nullptr;
}

void Engine::SpawnCharacter(Character character,
                            Classes::ASkeletalMeshActorSpawnable *&spawned) {
    SpawnCharacter(character, &spawned);
}

void Engine::SpawnCharacter(Character character,
                            Classes::ASkeletalMeshActorSpawnable **spawned) {
    if (!spawned) {
        return;
    }
    if (IsCurrentThreadStackAddress(spawned)) {
        EngineCoreBridge::Log("engine: remote character spawn rejected stack out pointer");
        return;
    }

    // Never clear a live actor here. Re-queue while EndScene drain is writing
    // used to null *spawned and erase SPAWN_OK results (client Actor stayed 0).
    if (*spawned) {
        return;
    }

    EngineInternal::spawns.Mutex.lock();
    for (const auto &entry : EngineInternal::spawns.Queue) {
        if (entry.second == spawned) {
            EngineInternal::spawns.Mutex.unlock();
            return;
        }
    }
    EngineInternal::spawns.Queue.push_back({character, spawned});
    const size_t qsize = EngineInternal::spawns.Queue.size();
    EngineInternal::spawns.Mutex.unlock();

    // Do not CreateFile/OutputDebugString/console-log on every queue push —
    // spawn retries while waiting for PC were hitching EndScene (2026-07-19).
    static ULONGLONG s_lastQueueLogMs = 0;
    const ULONGLONG nowMs = GetTickCount64();
    if (nowMs - s_lastQueueLogMs >= 2000) {
        s_lastQueueLogMs = nowMs;
        char buf2[128] = {};
        snprintf(buf2, sizeof(buf2),
                 "engine: SpawnCharacter queued char=%d queueSize=%zu",
                 static_cast<int>(character), qsize);
        EngineCoreBridge::Log(buf2);
    }
}

namespace {
std::mutex g_softDespawnMutex;
std::vector<Classes::ASkeletalMeshActorSpawnable *> g_softDespawnQueue;
} // namespace

void EngineInternal::DrainSoftDespawnQueue() {
    // Engine drops refs only — multiplayer parks orphans via Safe gameplay
    // TryWriteActorLocation on its Tick (engine.dll does not link safe_gameplay).
    // Never ShutDown / raw bHidden after TransformBones (KI-2026-012).
    std::lock_guard<std::mutex> lock(g_softDespawnMutex);
    if (!g_softDespawnQueue.empty()) {
        SetPhase("despawn.drop");
        g_softDespawnQueue.clear();
    }
}

void Engine::Despawn(Classes::ASkeletalMeshActorSpawnable *actor) {
    if (!actor) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_softDespawnMutex);
        g_softDespawnQueue.push_back(actor);
    }
    EngineInternal::SetPhase("despawn.queued");
}

namespace {
constexpr size_t kMaxRemoteBoneAtoms = 512;

bool ValidateBoneTransformBuffers(Classes::TArray<Classes::FBoneAtom> *destBones,
                                  Classes::FBoneAtom *src,
                                  Classes::FBoneAtom *&dest,
                                  size_t &destCount) {
    dest = nullptr;
    destCount = 0;

    if (!destBones || !src) {
        return false;
    }

    const auto signedDestCount = destBones->Num();
    if (signedDestCount <= 0) {
        return false;
    }

    destCount = static_cast<size_t>(signedDestCount);
    if (destCount > kMaxRemoteBoneAtoms) {
        return false;
    }

    dest = destBones->Buffer();
    if (!dest) {
        return false;
    }

    if (!MeSdk::Safe::IsReadableMemory(
            src, PLAYER_PAWN_BONE_COUNT * sizeof(Classes::FBoneAtom))) {
        return false;
    }

    return MeSdk::Safe::IsWritableMemory(
        dest, destCount * sizeof(Classes::FBoneAtom));
}
} // namespace

void Engine::TransformBones(Character character,
                            Classes::TArray<Classes::FBoneAtom> *destBones,
                            Classes::FBoneAtom *src) {

    Classes::FBoneAtom *dest = nullptr;
    size_t destCount = 0;
    if (!ValidateBoneTransformBuffers(destBones, src, dest, destCount)) {
        return;
    }

    size_t expectedDest = 63;
    switch (character) {
    case Character::Faith:
    case Character::Ghost:
        expectedDest = PLAYER_PAWN_BONE_COUNT;
        break;
    case Character::Kate:
        expectedDest = 102;
        break;
    default:
        break;
    }
    if (destCount < expectedDest) {
        static bool s_loggedShort[32] = {};
        const int idx = static_cast<int>(character);
        if (idx >= 0 && idx < 32 && !s_loggedShort[idx]) {
            s_loggedShort[idx] = true;
            char tmpPath[MAX_PATH] = {};
            if (GetTempPathA(static_cast<DWORD>(sizeof(tmpPath)), tmpPath) > 0) {
                char logPath[MAX_PATH] = {};
                snprintf(logPath, sizeof(logPath),
                         "%smirroredge-engine-spawn.log", tmpPath);
                FILE *f = nullptr;
                if (fopen_s(&f, logPath, "a") == 0 && f) {
                    fprintf(f,
                            "TransformBones short destAtoms=%zu need>=%zu "
                            "visual=%d\r\n",
                            destCount, expectedDest, idx);
                    fclose(f);
                }
            }
        }
    }

    switch (character) {
    case Character::Faith:
    case Character::Ghost:
        if (destCount >= PLAYER_PAWN_BONE_COUNT) {
            memcpy(dest, src, PLAYER_PAWN_BONE_COUNT * sizeof(Classes::FBoneAtom));
        }
        break;
    case Character::Kate:
        if (destCount >= 102) {
            memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
            memcpy(dest + 14, src + 14, 10 * sizeof(Classes::FBoneAtom));
            memcpy(dest + 33, src + 39, sizeof(Classes::FBoneAtom));
            memcpy(dest + 36, src + 42, sizeof(Classes::FBoneAtom));
            memcpy(dest + 39, src + 45, 63 * sizeof(Classes::FBoneAtom));
        }
        break;
    case Character::Celeste:
        if (destCount >= 63) {
            memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
            memcpy(dest + destCount - 63, src + 45,
                   63 * sizeof(Classes::FBoneAtom));
            memcpy(dest + 18, src + 18, sizeof(Classes::FBoneAtom));
        }
        break;
    case Character::AssaultCeleste:
        if (destCount >= 63) {
            memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
            memcpy(dest + destCount - 63, src + 45,
                   63 * sizeof(Classes::FBoneAtom));
            memcpy(dest + 17, src + 18, sizeof(Classes::FBoneAtom));
        }
        break;
    case Character::Jacknife:
        if (destCount >= 63) {
            memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
            memcpy(dest + destCount - 63, src + 45,
                   63 * sizeof(Classes::FBoneAtom));
            memcpy(dest + 18, src + 18, sizeof(Classes::FBoneAtom));
        }
        break;
    case Character::Miller:
        if (destCount >= 63) {
            memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
            memcpy(dest + destCount - 63, src + 45,
                   63 * sizeof(Classes::FBoneAtom));
            memcpy(dest + 18, src + 18, sizeof(Classes::FBoneAtom));
        }
        break;
    case Character::Kreeg:
        if (destCount >= 63) {
            memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
            memcpy(dest + destCount - 63, src + 45,
                   63 * sizeof(Classes::FBoneAtom));
            memcpy(dest + 18, src + 18, sizeof(Classes::FBoneAtom));
        }
        break;
    case Character::PursuitCop:
        if (destCount >= 63) {
            memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
            memcpy(dest + destCount - 63, src + 45,
                   63 * sizeof(Classes::FBoneAtom));
            memcpy(dest + 15, src + 18, sizeof(Classes::FBoneAtom));
        }
        break;
    }
}

// Define these to remove the D3DX dependency
D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, const D3DXMATRIX *pM1,
                                      const D3DXMATRIX *pM2) {

    D3DXMATRIX out;

    for (auto i = 0; i < 4; i++) {
        for (auto j = 0; j < 4; j++) {
            out.m[i][j] =
                pM1->m[i][0] * pM2->m[0][j] + pM1->m[i][1] * pM2->m[1][j] +
                pM1->m[i][2] * pM2->m[2][j] + pM1->m[i][3] * pM2->m[3][j];
        }
    }

    *pOut = out;
    return pOut;
}

D3DXVECTOR4 *WINAPI D3DXVec4Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV,
                                      const D3DXMATRIX *pM) {

    *pOut = {pM->m[0][0] * pV->x + pM->m[1][0] * pV->y + pM->m[2][0] * pV->z +
                 pM->m[3][0] * pV->w,
             pM->m[0][1] * pV->x + pM->m[1][1] * pV->y + pM->m[2][1] * pV->z +
                 pM->m[3][1] * pV->w,
             pM->m[0][2] * pV->x + pM->m[1][2] * pV->y + pM->m[2][2] * pV->z +
                 pM->m[3][2] * pV->w,
             pM->m[0][3] * pV->x + pM->m[1][3] * pV->y + pM->m[2][3] * pV->z +
                 pM->m[3][3] * pV->w};

    return pOut;
}

bool Engine::IsKeyDown(int vk) {
    return !EnginePresentationInternal::window.BlockInput && vk >= 0 && vk < ARRAYSIZE(EnginePresentationInternal::window.KeysDown) &&
           EnginePresentationInternal::window.KeysDown[vk];
}

bool Engine::WorldToScreen(IDirect3DDevice9 *device,
                           Classes::FVector &inOutLocation) {
    const auto controller = Engine::GetPlayerController();
    if (!controller || !EngineInternal::projectionTick.Matrix ||
        !controller->PlayerCamera) {
        return false;
    }

    const auto fov = tanf(
        (controller->PlayerCamera->GetFOVAngle() * CONST_Pi / 180.0f) / 2.0f);
    ImVec2 displaySize;
    if (PluginUi::IsBound()) {
        displaySize = PluginUi::GetIO().DisplaySize;
    } else {
        D3DVIEWPORT9 viewport = {};
        if (FAILED(device->GetViewport(&viewport))) {
            return false;
        }
        displaySize = ImVec2(static_cast<float>(viewport.Width),
                             static_cast<float>(viewport.Height));
    }
    const auto ratioFov = (displaySize.x / displaySize.y) / fov;

    D3DXMATRIX result, proj, world, view;
    proj = *EngineInternal::projectionTick.Matrix;

    for (int i = 0; i < 4; ++i) {
        proj.m[i][0] /= fov;
        proj.m[i][1] *= ratioFov;
        proj.m[i][3] = proj.m[i][2];
        proj.m[i][2] *= 0.998f;
    }

    device->GetTransform(D3DTS_VIEW, &view);
    device->GetTransform(D3DTS_WORLD, &world);

    D3DXMatrixMultiply(&result, &proj, &view);
    D3DXMatrixMultiply(&proj, &result, &world);

    D3DXVECTOR4 in(inOutLocation.X, inOutLocation.Y, inOutLocation.Z, 1), out;
    D3DXVec4Transform(&out, &in, &proj);

    inOutLocation = {(((out.x / out.w) + 1.0f) / 2.0f) * displaySize.x,
                     ((1.0f - (out.y / out.w)) / 2.0f) * displaySize.y, out.w};

    return !(out.z < 0 || out.w < 0);
}

namespace {

struct FindHwndByPidCtx {
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

BOOL CALLBACK FindVisibleGameWindowProc(HWND hwnd, LPARAM lParam) {
    auto *ctx = reinterpret_cast<FindHwndByPidCtx *>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != ctx->pid || !IsWindowVisible(hwnd)) {
        return TRUE;
    }
    // Prefer the main Unreal / Mirror's Edge top-level window. Borderless
    // layouts sometimes change the title (e.g. "Mirror's EdgeT"), so never
    // rely on an exact FindWindow title match.
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, static_cast<int>(sizeof(cls) / sizeof(cls[0])));
    wchar_t title[128] = {};
    GetWindowTextW(hwnd, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
    const bool classOk =
        (wcsstr(cls, L"Unreal") != nullptr) || (wcsstr(cls, L"Mirror") != nullptr);
    const bool titleOk = wcsstr(title, L"Mirror") != nullptr;
    if (classOk || titleOk) {
        ctx->hwnd = hwnd;
        return FALSE;
    }
    if (!ctx->hwnd) {
        // Fallback: first visible top-level owned by this process.
        if (GetWindow(hwnd, GW_OWNER) == nullptr) {
            ctx->hwnd = hwnd;
        }
    }
    return TRUE;
}

HWND ResolveGameHwnd() {
    HWND hwnd = EnginePresentationInternal::window.Window;
    if (hwnd && IsWindow(hwnd)) {
        return hwnd;
    }

    FindHwndByPidCtx ctx = {};
    ctx.pid = GetCurrentProcessId();
    EnumWindows(FindVisibleGameWindowProc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.hwnd) {
        EnginePresentationInternal::window.Window = ctx.hwnd;
        return ctx.hwnd;
    }

    hwnd = FindWindowW(nullptr, L"Mirror's Edge");
    if (!hwnd) {
        hwnd = FindWindowW(L"UnrealWindow", nullptr);
    }
    if (hwnd) {
        EnginePresentationInternal::window.Window = hwnd;
    }
    return hwnd;
}

} // namespace

HWND Engine::GetWindow() { return ResolveGameHwnd(); }

static void InjectVirtualKey(UINT vk, bool keyUp) {
    HWND hwnd = ResolveGameHwnd();
    if (hwnd) {
        if (GetForegroundWindow() != hwnd) {
            SetForegroundWindow(hwnd);
        }
        const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        const LPARAM lp =
            1 | (static_cast<LPARAM>(scan) << 16) |
            (keyUp ? (static_cast<LPARAM>(1) << 30) |
                         (static_cast<LPARAM>(1) << 31)
                   : 0);
        PostMessageW(hwnd, keyUp ? WM_KEYUP : WM_KEYDOWN, vk, lp);
        if (!keyUp) {
            // Title / press-start screens often need WM_CHAR as well.
            PostMessageW(hwnd, WM_CHAR, vk == VK_RETURN ? '\r' : vk, lp);
        }
    }

    // SendInput reaches DirectInput / raw paths that ignore PostMessage.
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vk);
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &input, sizeof(INPUT));
}

void Engine::InjectKeyDown(UINT vk) {
    HWND hwnd = ResolveGameHwnd();
    {
        char tmpPath[MAX_PATH] = {};
        GetTempPathA(sizeof(tmpPath), tmpPath);
        char logPath[MAX_PATH] = {};
        snprintf(logPath, sizeof(logPath), "%smirroredge-inject.log", tmpPath);
        HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char line[256] = {};
            int len = snprintf(line, sizeof(line),
                "InjectKeyDown vk=0x%02X hwnd=%p rawHwnd=%p\r\n",
                vk, static_cast<void *>(hwnd),
                static_cast<void *>(EnginePresentationInternal::window.Window));
            WriteFile(h, line, len, nullptr, nullptr);
            CloseHandle(h);
        }
    }
    InjectVirtualKey(vk, false);
}

void Engine::InjectKeyUp(UINT vk) { InjectVirtualKey(vk, true); }

const char *const *Engine::GetCharacterNames() { return Characters; }

int Engine::GetCharacterNameCount() {
	return static_cast<int>(sizeof(Characters) / sizeof(Characters[0]));
}

void Engine::OnRenderScene(RenderSceneCallback callback) {
    if (EngineInternal::hostedMode.load()) {
        const auto *bridge = EngineCoreBridge::Get();
        if (bridge && bridge->host && bridge->host->OnRenderScene &&
            bridge->wrapRenderScene) {
            bridge->host->OnRenderScene(bridge->wrapRenderScene(callback));
            return;
        }
    }

    std::lock_guard<std::mutex> lock(
        EnginePresentationInternal::renderScene.Mutex);
    EnginePresentationInternal::renderScene.Callbacks.push_back(callback);
}

void Engine::OnProcessEvent(ProcessEventCallback callback) {
    std::lock_guard<std::mutex> lock(EngineInternal::processEvent.Mutex);
    EngineInternal::processEvent.Callbacks.push_back(callback);
    EngineInternal::processEvent.CallbackCount.store(
        static_cast<int>(EngineInternal::processEvent.Callbacks.size()),
        std::memory_order_relaxed);
}

void Engine::OnPreLevelLoad(LevelLoadCallback callback) {
    std::lock_guard<std::mutex> lock(EngineInternal::levelLoad.Mutex);
    EngineInternal::levelLoad.PreCallbacks.push_back(callback);
}

void Engine::OnPostLevelLoad(LevelLoadCallback callback) {
    std::lock_guard<std::mutex> lock(EngineInternal::levelLoad.Mutex);
    EngineInternal::levelLoad.PostCallbacks.push_back(callback);
}

void Engine::OnPreDeath(DeathCallback callback) {
    std::lock_guard<std::mutex> lock(EngineInternal::death.Mutex);
    EngineInternal::death.PreCallbacks.push_back(callback);
}

void Engine::OnPostDeath(DeathCallback callback) {
    std::lock_guard<std::mutex> lock(EngineInternal::death.Mutex);
    EngineInternal::death.PostCallbacks.push_back(callback);
}

void Engine::OnActorTick(ActorTickCallback callback) {
    std::lock_guard<std::mutex> lock(EngineInternal::actorTick.Mutex);
    EngineInternal::actorTick.Callbacks.push_back(callback);
    EngineInternal::actorTick.CallbackCount.store(
        static_cast<int>(EngineInternal::actorTick.Callbacks.size()),
        std::memory_order_relaxed);
}

void Engine::OnBonesTick(BonesTickCallback callback) {
    std::lock_guard<std::mutex> lock(EngineInternal::bonesTick.Mutex);
    EngineInternal::bonesTick.Callbacks.push_back(callback);
    EngineInternal::bonesTick.CallbackCount.store(
        static_cast<int>(EngineInternal::bonesTick.Callbacks.size()),
        std::memory_order_relaxed);
}

void Engine::OnTick(TickCallback callback) {
    std::lock_guard<std::mutex> lock(EngineInternal::tick.Mutex);
    EngineInternal::tick.Callbacks.push_back(callback);
}

void Engine::OnInput(InputCallback callback) {
    EnginePresentationInternal::window.InputCallbacks.push_back(callback);
}

void Engine::OnSuperInput(InputCallback callback) {
    EnginePresentationInternal::window.SuperInputCallbacks.push_back(callback);
}

void Engine::BlockInput(bool block) {
    if (EngineInternal::hostedMode.load()) {
        const auto *bridge = EngineCoreBridge::Get();
        if (bridge && bridge->host && bridge->host->BlockInput) {
            bridge->host->BlockInput(block);
            return;
        }
    }

    EnginePresentationInternal::window.BlockInput = block;
}

void Engine::BeginInitialization() { EngineInternal::modInitializing = true; }

bool Engine::IsInitializing() { return EngineInternal::modInitializing; }

bool Engine::IsModReady() { return EngineInternal::modReady; }

void Engine::MarkReady() {
    EngineInternal::modInitializing = false;
    EngineInternal::modReady = true;

    EngineDebugTrace::Event("engine.cpp:MarkReady", "mod_ready", "H-INIT", 1, 0, 0, 0);

    if (!modReadyEvent) {
        modReadyEvent = CreateEventW(nullptr, TRUE, FALSE, ModReadyEventName());
    }
    if (modReadyEvent) {
        SetEvent(modReadyEvent);
    }
}

bool Engine::IsGameReadyForModInit() { return MeSdk::ProbeGlobals(); }

void Engine::SetDeferredInitCallback(MainThreadTask initCallback) {
    EnginePresentationInternal::deferredInitTask = initCallback;
    if (EnginePresentationInternal::injectTick == 0) {
        EnginePresentationInternal::injectTick = GetTickCount();
    }

    if (!modReadyEvent) {
        modReadyEvent = CreateEventW(nullptr, TRUE, FALSE, ModReadyEventName());
    }
    if (modReadyEvent) {
        ResetEvent(modReadyEvent);
    }
}

void Engine::SetHostedMode(bool hosted) {
    EngineInternal::hostedMode = hosted;
    if (hosted) {
        EngineInternal::hostedGameplayLive = false;
    }
}

bool Engine::IsHostedMode() { return EngineInternal::hostedMode.load(); }

void Engine::SetHostedGameplayLive(bool live) { EngineInternal::hostedGameplayLive = live; }

bool Engine::IsHostedGameplayLive() {
    return !EngineInternal::hostedMode.load() || EngineInternal::hostedGameplayLive.load();
}

void Engine::ClearFeaturePluginCallbacks() {
    {
        std::lock_guard<std::mutex> lock(EngineInternal::tick.Mutex);
        EngineInternal::tick.Callbacks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(EngineInternal::levelLoad.Mutex);
        EngineInternal::levelLoad.PreCallbacks.clear();
        EngineInternal::levelLoad.PostCallbacks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(EngineInternal::death.Mutex);
        EngineInternal::death.PreCallbacks.clear();
        EngineInternal::death.PostCallbacks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(EngineInternal::actorTick.Mutex);
        EngineInternal::actorTick.Callbacks.clear();
        EngineInternal::actorTick.CallbackCount.store(0, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lock(EngineInternal::bonesTick.Mutex);
        EngineInternal::bonesTick.Callbacks.clear();
        EngineInternal::bonesTick.CallbackCount.store(0, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lock(EngineInternal::processEvent.Mutex);
        EngineInternal::processEvent.Callbacks.clear();
        EngineInternal::processEvent.CallbackCount.store(0, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lock(
            EnginePresentationInternal::renderScene.Mutex);
        EnginePresentationInternal::renderScene.Callbacks.clear();
    }
    EnginePresentationInternal::window.InputCallbacks.clear();
    EnginePresentationInternal::window.SuperInputCallbacks.clear();
    EngineInternal::hostedGameplayLive = false;
}

bool Engine::InstallRendererCapture() {
    if (EngineInternal::hostedMode.load()) {
        return true;
    }

    if (EnginePresentationInternal::rendererManagedByProxy.load() || IsModD3D9ProxyActive()) {
        return true;
    }

    if (EnginePresentationInternal::presentationHooksInstalled.load()) {
        return true;
    }

    // InitWorker waits for d3d9.dll before calling here. Patching Direct3DCreate9 or
    // probing the live device from the inject worker crashes the render thread.
    // Presentation hooks install later via PeekMessage + EnginePresentationInternal::TryLazyPresentationHook.
    //
    // Early-return if d3d9 is already present (common proxy path). The atomic exchange
    // below serialises racing threads: the loser returns immediately.
    if (GetModuleHandleW(L"d3d9.dll")) {
        return true;
    }

    if (EnginePresentationInternal::d3d9ExportHooked.exchange(true)) {
        return true;
    }

    // TOCTOU re-check after winning the exchange: d3d9 could theoretically have
    // been unloaded (not in practice — it stays resident once loaded).
    const auto d3d9 = GetModuleHandleW(L"d3d9.dll");
    if (!d3d9) {
        EnginePresentationInternal::d3d9ExportHooked = false;
        return false;
    }

    const auto exportAddr = GetProcAddress(d3d9, "Direct3DCreate9");
    if (!exportAddr) {
        EnginePresentationInternal::d3d9ExportHooked = false;
        return false;
    }

    if (!Hook::TrampolineHookNoSuspend(
            reinterpret_cast<void *>(EnginePresentationInternal::Direct3DCreate9Hook), exportAddr,
            reinterpret_cast<void **>(&EnginePresentationInternal::Direct3DCreate9Original))) {
        EnginePresentationInternal::d3d9ExportHooked = false;
        return false;
    }

    return true;
}

bool Engine::TryCaptureRenderer() {
    return EnginePresentationInternal::TryLazyPresentationHook();
}

bool Engine::HookDirect3D9Interface(IDirect3D9 *d3d) {
    if (!d3d) {
        return false;
    }

    EnginePresentationInternal::rendererManagedByProxy = true;
    EnginePresentationInternal::HookIDirect3D9CreateDevice(d3d);
    return true;
}

void Engine::OnProxyDeviceCreated(IDirect3DDevice9 *device) {
    if (!device) {
        return;
    }

    EngineDebugTrace::Event("engine.cpp:OnProxyDeviceCreated", "proxy_device", "H-D3D",
                            reinterpret_cast<uintptr_t>(device), 0, 0, 0);
    EnginePresentationInternal::rendererManagedByProxy = true;
    EnginePresentationInternal::bootstrapDevice = device;
    EnginePresentationInternal::HookDevicePresentation(device);
}

bool Engine::IsModD3D9ProxyActive() {
    if (EnginePresentationInternal::rendererManagedByProxy.load()) {
        return true;
    }

    return EnginePresentationInternal::IsModD3D9ProxyModule(GetModuleHandleW(L"d3d9.dll"));
}

bool Engine::ArePresentationHooksInstalled() {
    if (EngineInternal::hostedMode.load()) {
        const auto *bridge = EngineCoreBridge::Get();
        if (bridge && bridge->host && bridge->host->ArePresentationHooksInstalled) {
            return bridge->host->ArePresentationHooksInstalled();
        }
        return true;
    }

    return EnginePresentationInternal::presentationHooksInstalled.load();
}

bool Engine::InstallPeekMessageBootstrap() {
    return EnginePresentationInternal::InstallPeekMessageBootstrapImpl();
}

void Engine::QueueMainThreadTask(MainThreadTask task) {
    if (!task) {
        return;
    }

    if (EngineInternal::hostedMode.load()) {
        const auto *bridge = EngineCoreBridge::Get();
        if (bridge && bridge->host && bridge->host->QueueMainThreadTask) {
            bridge->host->QueueMainThreadTask(task);
            return;
        }
    }

    std::lock_guard<std::mutex> lock(EnginePresentationInternal::mainThreadTaskMutex);
    EnginePresentationInternal::mainThreadTasks.push_back(task);
}

bool Engine::InitializeSDK() {
    if (MeSdk::InitializeGlobals()) {
        // Resolve UTdGameEngine class inside safe_gui's TU so EndScene warm
        // never calls FindClass (per-TU StaticClass statics are not shared).
        MeSdk::Safe::Gui::PrewarmTdGameEngineClass();
        MeSdk::Safe::Gui::PrewarmWorldInfoClass();
        // Gameplay::PrewarmClasses lives in safe_gameplay.cpp (linked by
        // feature DLLs, not engine.dll). Engine prewarms TdPawn below; MP
        // TryReadPawnPose skips TryIsA when its per-module class cache is cold.
        PrewarmTdPlayerControllerClass();
        PrewarmTdPlayerPawnClass();
        PrewarmPawnClass();
        EngineDebugTrace::Event("engine.cpp:InitializeSDK", "sdk_ok", "H-SDK", 1, 0,
                                0, 0);
        return true;
    }

    ReportInitFailure("init: Failed to find GNames/GObjects");
    EngineDebugTrace::Event("engine.cpp:InitializeSDK", "sdk_fail", "H-SDK", 0, 0, 0,
                            static_cast<int>(MeSdk::GetLastSdkError()));
    return false;
}


bool Engine::Initialize() {
    return InitializeSDK() && InstallGameplayHooks();
}

bool Engine::InstallPresentationHooks(IDirect3DDevice9 *device) {
    if (EnginePresentationInternal::presentationHooksInstalled.load()) {
        return true;
    }

    if (!device) {
        device = EnginePresentationInternal::capturedDevice ? EnginePresentationInternal::capturedDevice : EnginePresentationInternal::bootstrapDevice;
    }
    if (!device) {
        device = EnginePresentationInternal::FindExistingD3D9Device();
    }
    if (!device) {
        return false;
    }

    return EnginePresentationInternal::HookDevicePresentation(device);
}

