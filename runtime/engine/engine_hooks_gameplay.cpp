#include <Windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

#include "engine_internal.h"
#include "debug_trace.h"
#include "timing_constants.h"
#include "engine_core_bridge.h"
#include "hook.h"
#include "me_sdk/runtime/pattern.h"
#include "me_sdk/patterns/hooks.h"
#include "me_sdk/runtime/safe_access.h"
#include "me_sdk/runtime/safe_gui.h"
#include "plugin_seh_guard.h"

namespace EngineInternal {

namespace {

template <typename Callback>
void RemoveCrashedCallback(std::vector<Callback> &callbacks, Callback callback) {
    callbacks.erase(std::remove(callbacks.begin(), callbacks.end(), callback),
                    callbacks.end());
}

template <typename Callback>
void RemoveCrashedCallbackLocked(std::vector<Callback> &callbacks,
                                 std::mutex &mutex, Callback callback) {
    std::lock_guard<std::mutex> lock(mutex);
    RemoveCrashedCallback(callbacks, callback);
}

template <typename Callback>
std::vector<Callback> CopyCallbacks(const std::vector<Callback> &callbacks,
                                    std::mutex &mutex) {
    std::lock_guard<std::mutex> lock(mutex);
    return callbacks;
}

void LogCallbackFault(const char *context, DWORD exceptionCode) {
    char message[160] = {};
    snprintf(message, sizeof(message),
             "engine: plugin callback crashed in %s (0x%08lX); callback removed",
             context ? context : "unknown",
             static_cast<unsigned long>(exceptionCode));
    EngineCoreBridge::Log(message);
}

void DurableSpawnLogf(const char *format, ...) {
    if (!format || !format[0]) {
        return;
    }

    char message[512] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Rate-limit console bridge + file I/O — failed EndScene drains used to
    // flood Log/CreateFile every frame and hitch the game (user stutter).
    static char s_lastBridge[160] = {};
    static ULONGLONG s_lastBridgeMs = 0;
    const ULONGLONG nowMs = GetTickCount64();
    const bool same = (strncmp(s_lastBridge, message, sizeof(s_lastBridge) - 1) == 0);
    const bool emit = !same || (nowMs - s_lastBridgeMs) >= 2000;
    if (!emit) {
        return;
    }
    EngineCoreBridge::Log(message);
    strncpy_s(s_lastBridge, message, _TRUNCATE);
    s_lastBridgeMs = nowMs;

    // Always write the durable spawn log in Release too — hang diagnosis
    // previously had no file because this was #ifdef _DEBUG only.
    char tempPath[MAX_PATH] = {};
    if (!GetTempPathA(static_cast<DWORD>(sizeof(tempPath)), tempPath)) {
        return;
    }

    char logPath[MAX_PATH] = {};
    if (snprintf(logPath, sizeof(logPath), "%smirroredge-engine-spawn.log", tempPath) < 0) {
        return;
    }

    HANDLE file = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char line[640] = {};
    const int length = snprintf(line, sizeof(line),
                                "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
                                now.wYear, now.wMonth, now.wDay, now.wHour,
                                now.wMinute, now.wSecond, now.wMilliseconds,
                                message);
    if (length > 0) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
    }
    CloseHandle(file);
}

// Forward declaration: exported from spawn_actor_safe.cpp
extern "C" void *__cdecl MMOD_SpawnActorSafe(void *owner, const char *context);
extern "C" void *__cdecl MMOD_EnsureTdPawnMeshFromDonorSafe(void *pawn,
                                                            void *owner);

struct ProcessEventCallbackContext {
    ProcessEventCallback callback = nullptr;
    Classes::UObject *object = nullptr;
    Classes::UFunction *function = nullptr;
    void *args = nullptr;
    void *result = nullptr;
    bool handled = false;
};

struct LevelLoadCallbackContext {
    LevelLoadCallback callback = nullptr;
    const wchar_t *levelName = nullptr;
};

struct DeathCallbackContext {
    DeathCallback callback = nullptr;
};

struct ActorTickCallbackContext {
    ActorTickCallback callback = nullptr;
    Classes::AActor *actor = nullptr;
};

struct BonesTickCallbackContext {
    BonesTickCallback callback = nullptr;
    Classes::TArray<Classes::FBoneAtom> *bones = nullptr;
};

struct TickCallbackContext {
    TickCallback callback = nullptr;
    float delta = 0.f;
};

void InvokeProcessEventCallback(void *data) {
    auto *ctx = static_cast<ProcessEventCallbackContext *>(data);
    ctx->handled = ctx->callback(ctx->object, ctx->function, ctx->args, ctx->result);
}

void InvokeLevelLoadCallback(void *data) {
    auto *ctx = static_cast<LevelLoadCallbackContext *>(data);
    ctx->callback(ctx->levelName);
}

void InvokeDeathCallback(void *data) {
    auto *ctx = static_cast<DeathCallbackContext *>(data);
    ctx->callback();
}

void InvokeActorTickCallback(void *data) {
    auto *ctx = static_cast<ActorTickCallbackContext *>(data);
    ctx->callback(ctx->actor);
}

void InvokeBonesTickCallback(void *data) {
    auto *ctx = static_cast<BonesTickCallbackContext *>(data);
    ctx->callback(ctx->bones);
}

void InvokeTickCallback(void *data) {
    auto *ctx = static_cast<TickCallbackContext *>(data);
    ctx->callback(ctx->delta);
}

} // namespace

// Engine hook implementations
int __fastcall ProcessEventHook(Classes::UObject *object, void *idle,
                                class Classes::UFunction *function, void *args,
                                void *result) {

    if (!modReady || modInitializing || levelLoad.Loading ||
        !Engine::IsHostedGameplayLive() ||
        processEvent.CallbackCount.load(std::memory_order_relaxed) == 0) {
        return processEvent.Original(object, function, args, result);
    }

    auto sum = 0;
    std::vector<ProcessEventCallback> crashedCallbacks;
    const auto callbacks = CopyCallbacks(processEvent.Callbacks, processEvent.Mutex);
    for (const auto &callback : callbacks) {
        ProcessEventCallbackContext ctx = {callback, object, function, args, result};
        DWORD exceptionCode = 0;
        if (!PluginSehGuard::InvokeVoid(
                "plugin_process_event",
                "engine_hooks_gameplay.cpp:ProcessEventHook",
                InvokeProcessEventCallback, &ctx, &exceptionCode)) {
            crashedCallbacks.push_back(callback);
            LogCallbackFault("process_event", exceptionCode);
            continue;
        }
        sum += ctx.handled ? 1 : 0;
    }
    for (const auto callback : crashedCallbacks) {
        RemoveCrashedCallbackLocked(processEvent.Callbacks, processEvent.Mutex,
                                    callback);
        processEvent.CallbackCount.store(
            static_cast<int>(processEvent.Callbacks.size()),
            std::memory_order_relaxed);
    }

    return sum == 0 ? processEvent.Original(object, function, args, result) : 0;
}

static int CallLevelLoadOriginalSafe(void *this_, void *idle, void **levelInfo,
                                        unsigned long long arg) {
#ifdef _DEBUG
    HANDLE h = CreateFileW(L"C:\\Temp\\engine_levelload_trace.txt", FILE_APPEND_DATA,
                           FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        const char *msg = "=== CallLevelLoadOriginalSafe ENTER ===\r\n";
        DWORD written;
        WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
        CloseHandle(h);
    }
#endif
    __try {
        int ret = levelLoad.Original(this_, levelInfo, arg);
#ifdef _DEBUG
        h = CreateFileW(L"C:\\Temp\\engine_levelload_trace.txt", FILE_APPEND_DATA,
                        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char buf[64];
            int len = snprintf(buf, sizeof(buf), "=== CallLevelLoadOriginalSafe OK ret=%d ===\r\n", ret);
            DWORD written;
            WriteFile(h, buf, len, &written, nullptr);
            CloseHandle(h);
        }
#endif
        return ret;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("engine: LevelLoadHook SEH caught in Original\n");
#ifdef _DEBUG
        h = CreateFileW(L"C:\\Temp\\engine_levelload_trace.txt", FILE_APPEND_DATA,
                        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const char *msg = "=== CallLevelLoadOriginalSafe EXCEPTION ===\r\n";
            DWORD written;
            WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
            CloseHandle(h);
        }
#endif
        return 0;
    }
}

static void ProcessPostLevelCallbacks(void *this_, void *idle, void **levelInfo,
                                       unsigned long long arg) {
#ifdef _DEBUG
    HANDLE h = CreateFileW(L"C:\\Temp\\engine_levelload_trace.txt", FILE_APPEND_DATA,
                           FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "=== ProcessPostLevelCallbacks count=%zu ===\r\n", levelLoad.PostCallbacks.size());
        DWORD written;
        WriteFile(h, buf, len, &written, nullptr);
        CloseHandle(h);
    }
#endif

    const auto levelName = reinterpret_cast<const wchar_t *>(levelInfo[7]);

    std::vector<LevelLoadCallback> crashedPostCallbacks;
    const auto postCallbacks =
        CopyCallbacks(levelLoad.PostCallbacks, levelLoad.Mutex);
    for (const auto callback : postCallbacks) {
        LevelLoadCallbackContext ctx = {callback, levelName};
        DWORD exceptionCode = 0;
        if (!PluginSehGuard::InvokeVoid(
                "plugin_level_post",
                "engine_hooks_gameplay.cpp:LevelLoadHook:post",
                InvokeLevelLoadCallback, &ctx, &exceptionCode)) {
            crashedPostCallbacks.push_back(callback);
            LogCallbackFault("level_post", exceptionCode);
        }
    }
    for (const auto callback : crashedPostCallbacks) {
        RemoveCrashedCallbackLocked(levelLoad.PostCallbacks, levelLoad.Mutex,
                                    callback);
    }

#ifdef _DEBUG
    h = CreateFileW(L"C:\\Temp\\engine_levelload_trace.txt", FILE_APPEND_DATA,
                    FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        const char *msg = "=== ProcessPostLevelCallbacks DONE ===\r\n";
        DWORD written;
        WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
        CloseHandle(h);
    }
#endif
}

int __fastcall LevelLoadHook(void *this_, void *idle, void **levelInfo,
                             unsigned long long arg) {

    if (!modReady) {
        return levelLoad.Original(this_, levelInfo, arg);
    }

#ifdef _DEBUG
    {
        HANDLE h = CreateFileW(L"C:\\Temp\\engine_levelload_trace.txt", FILE_APPEND_DATA,
                               FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const char *msg = "=== LevelLoadHook ENTER ===\r\n";
            DWORD written;
            WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
            CloseHandle(h);
        }
    }
#endif

    const auto levelName = reinterpret_cast<const wchar_t *>(levelInfo[7]);

    std::vector<LevelLoadCallback> crashedPreCallbacks;
    const auto preCallbacks =
        CopyCallbacks(levelLoad.PreCallbacks, levelLoad.Mutex);
    for (const auto callback : preCallbacks) {
        LevelLoadCallbackContext ctx = {callback, levelName};
        DWORD exceptionCode = 0;
        if (!PluginSehGuard::InvokeVoid(
                "plugin_level_pre",
                "engine_hooks_gameplay.cpp:LevelLoadHook:pre",
                InvokeLevelLoadCallback, &ctx, &exceptionCode)) {
            crashedPreCallbacks.push_back(callback);
            LogCallbackFault("level_pre", exceptionCode);
        }
    }
    for (const auto callback : crashedPreCallbacks) {
        RemoveCrashedCallbackLocked(levelLoad.PreCallbacks, levelLoad.Mutex,
                                    callback);
    }

    // Clear the spawn queue under a short lock, then release before LoadMap.
    // Holding spawns.Mutex across CallLevelLoadOriginalSafe starved EndScene
    // drain and froze the render thread for the entire level load (2026-07-19).
    {
        spawns.Mutex.lock();
        spawns.Queue.clear();
        spawns.Queue.shrink_to_fit();
        spawns.Mutex.unlock();
    }

    levelLoad.Loading = true;
    levelLoad.loadingStartTick = GetTickCount64();
    const auto ret = CallLevelLoadOriginalSafe(this_, idle, levelInfo, arg);
    levelLoad.Loading = false;
    levelLoad.loadingStartTick = 0;

    // Drop anything queued while Loading was true.
    {
        spawns.Mutex.lock();
        spawns.Queue.clear();
        spawns.Queue.shrink_to_fit();
        spawns.Mutex.unlock();
    }

    ProcessPostLevelCallbacks(this_, idle, levelInfo, arg);

    return ret;
}

int PreDeathHook() {
    if (!modReady) {
        return death.PreOriginal();
    }

    std::vector<DeathCallback> crashedCallbacks;
    const auto preCallbacks = CopyCallbacks(death.PreCallbacks, death.Mutex);
    for (const auto callback : preCallbacks) {
        DeathCallbackContext ctx = {callback};
        DWORD exceptionCode = 0;
        if (!PluginSehGuard::InvokeVoid(
                "plugin_death_pre",
                "engine_hooks_gameplay.cpp:PreDeathHook",
                InvokeDeathCallback, &ctx, &exceptionCode)) {
            crashedCallbacks.push_back(callback);
            LogCallbackFault("death_pre", exceptionCode);
        }
    }
    for (const auto callback : crashedCallbacks) {
        RemoveCrashedCallbackLocked(death.PreCallbacks, death.Mutex, callback);
    }

    return death.PreOriginal();
}

int PostDeathHook() {
    const auto ret = death.PostOriginal();

    if (!modReady) {
        return ret;
    }

    std::vector<DeathCallback> crashedCallbacks;
    const auto postCallbacks = CopyCallbacks(death.PostCallbacks, death.Mutex);
    for (const auto callback : postCallbacks) {
        DeathCallbackContext ctx = {callback};
        DWORD exceptionCode = 0;
        if (!PluginSehGuard::InvokeVoid(
                "plugin_death_post",
                "engine_hooks_gameplay.cpp:PostDeathHook",
                InvokeDeathCallback, &ctx, &exceptionCode)) {
            crashedCallbacks.push_back(callback);
            LogCallbackFault("death_post", exceptionCode);
        }
    }
    for (const auto callback : crashedCallbacks) {
        RemoveCrashedCallbackLocked(death.PostCallbacks, death.Mutex, callback);
    }

    return ret;
}

HMODULE WINAPI LoadLibraryAHook(const char *module) {
    if (strstr(module, "menl_hooks.dll")) {
        Hook::UnTrampolineHook(levelLoad.Base, levelLoad.Original);
        Hook::UnTrampolineHook(death.PreBase, death.PreOriginal);
        Hook::UnTrampolineHook(death.PostBase, death.PostOriginal);

        std::thread([]() {
            // Reinstall hooks after menl_hooks.dll unloads + reloads.
            // Poll death.PostBase until the jmp (0xE9) reappears, with a 30 s timeout.
            const DWORD deadline = GetTickCount() + 30000;
            for (;;) {
                // volatile read prevents compiler from hoisting the byte check
                // out of the loop; on x86 the byte read is atomic by ISA.
                if (*reinterpret_cast<volatile byte *>(death.PostBase) == 0xE9) {
                    Hook::TrampolineHook(
                        LevelLoadHook, levelLoad.Base,
                        reinterpret_cast<void **>(&levelLoad.Original));

                    Hook::TrampolineHook(
                        PreDeathHook, death.PreBase,
                        reinterpret_cast<void **>(&death.PreOriginal));

                    Hook::TrampolineHook(
                        PostDeathHook, death.PostBase,
                        reinterpret_cast<void **>(&death.PostOriginal));

                    return;
                }

                if (GetTickCount() >= deadline) {
                    return;
                }

                Sleep(Timing::kHookPollMs);
            }
        }).detach();
    }

    return LoadLibraryAOriginal(module);
}

void *__fastcall ActorTickHook(Classes::AActor *actor, void *idle, void *arg) {
    if (!modReady || modInitializing || !Engine::IsHostedGameplayLive() ||
        levelLoad.Loading ||
        actorTick.CallbackCount.load(std::memory_order_relaxed) == 0) {
        return actorTick.Original(actor, arg);
    }

    // Prefer single-callback path (no per-actor vector alloc). Multiplayer no
    // longer registers here — poses run from OnTick once per frame.
    ActorTickCallback single = nullptr;
    std::vector<ActorTickCallback> many;
    {
        std::lock_guard<std::mutex> lock(actorTick.Mutex);
        const auto n = actorTick.Callbacks.size();
        if (n == 0) {
            return actorTick.Original(actor, arg);
        }
        if (n == 1) {
            single = actorTick.Callbacks[0];
        } else {
            many = actorTick.Callbacks;
        }
    }

    if (single) {
        DWORD exceptionCode = 0;
        ActorTickCallbackContext ctx = {single, actor};
        if (!PluginSehGuard::InvokeVoid(
                "plugin_actor_tick",
                "engine_hooks_gameplay.cpp:ActorTickHook",
                InvokeActorTickCallback, &ctx, &exceptionCode)) {
            LogCallbackFault("actor_tick", exceptionCode);
            RemoveCrashedCallbackLocked(actorTick.Callbacks, actorTick.Mutex,
                                        single);
            actorTick.CallbackCount.store(
                static_cast<int>(actorTick.Callbacks.size()),
                std::memory_order_relaxed);
        }
    } else {
        std::vector<ActorTickCallback> crashedCallbacks;
        for (const auto callback : many) {
            ActorTickCallbackContext ctx = {callback, actor};
            DWORD exceptionCode = 0;
            if (!PluginSehGuard::InvokeVoid(
                    "plugin_actor_tick",
                    "engine_hooks_gameplay.cpp:ActorTickHook",
                    InvokeActorTickCallback, &ctx, &exceptionCode)) {
                crashedCallbacks.push_back(callback);
                LogCallbackFault("actor_tick", exceptionCode);
            }
        }
        for (const auto callback : crashedCallbacks) {
            RemoveCrashedCallbackLocked(actorTick.Callbacks, actorTick.Mutex,
                                        callback);
        }
        if (!crashedCallbacks.empty()) {
            actorTick.CallbackCount.store(
                static_cast<int>(actorTick.Callbacks.size()),
                std::memory_order_relaxed);
        }
    }

    return actorTick.Original(actor, arg);
}

void *__fastcall BonesTickHook(void *this_, void *idle, void *arg) {
    const auto bones = static_cast<Classes::TArray<Classes::FBoneAtom> *>(
        bonesTick.Original(this_, arg));

    if (!modReady || levelLoad.Loading || !Engine::IsHostedGameplayLive() ||
        bonesTick.CallbackCount.load(std::memory_order_relaxed) == 0) {
        return bones;
    }

    if (!bones || !bones->Num()) {
        return bones;
    }

    BonesTickCallback single = nullptr;
    std::vector<BonesTickCallback> many;
    {
        std::lock_guard<std::mutex> lock(bonesTick.Mutex);
        const auto n = bonesTick.Callbacks.size();
        if (n == 0) {
            return bones;
        }
        if (n == 1) {
            single = bonesTick.Callbacks[0];
        } else {
            many = bonesTick.Callbacks;
        }
    }

    if (single) {
        DWORD exceptionCode = 0;
        BonesTickCallbackContext ctx = {single, bones};
        if (!PluginSehGuard::InvokeVoid(
                "plugin_bones_tick",
                "engine_hooks_gameplay.cpp:BonesTickHook",
                InvokeBonesTickCallback, &ctx, &exceptionCode)) {
            LogCallbackFault("bones_tick", exceptionCode);
            RemoveCrashedCallbackLocked(bonesTick.Callbacks, bonesTick.Mutex,
                                        single);
            bonesTick.CallbackCount.store(
                static_cast<int>(bonesTick.Callbacks.size()),
                std::memory_order_relaxed);
        }
    } else {
        std::vector<BonesTickCallback> crashedCallbacks;
        for (const auto callback : many) {
            BonesTickCallbackContext ctx = {callback, bones};
            DWORD exceptionCode = 0;
            if (!PluginSehGuard::InvokeVoid(
                    "plugin_bones_tick",
                    "engine_hooks_gameplay.cpp:BonesTickHook",
                    InvokeBonesTickCallback, &ctx, &exceptionCode)) {
                crashedCallbacks.push_back(callback);
                LogCallbackFault("bones_tick", exceptionCode);
            }
        }
        for (const auto callback : crashedCallbacks) {
            RemoveCrashedCallbackLocked(bonesTick.Callbacks, bonesTick.Mutex,
                                        callback);
        }
        if (!crashedCallbacks.empty()) {
            bonesTick.CallbackCount.store(
                static_cast<int>(bonesTick.Callbacks.size()),
                std::memory_order_relaxed);
        }
    }

    return bones;
}

int *__fastcall ProjectionTick(Classes::FMatrix *matrix, void *idle,
                               void *arg) {

    if (modReady) {
        projectionTick.Matrix = reinterpret_cast<D3DXMATRIX *>(matrix);
    }

    return projectionTick.Original(matrix, arg);
}

Classes::ASkeletalMeshActorSpawnable *
SpawnCharacter(Engine::Character character) {
    // UE3 Spawn can re-enter PeekMessage/Tick/EndScene. Nested SpawnCharacter
    // deadlocks the game thread (verified 2026-07-19: phase stuck at
    // spawn.char.begin with Responding=false after bots queued).
    static thread_local bool s_inSpawn = false;
    if (s_inSpawn) {
        SetPhase("spawn.char.reenter");
        return nullptr;
    }
    s_inSpawn = true;
    struct SpawnGuard {
        bool &flag;
        ~SpawnGuard() { flag = false; }
    } guard{s_inSpawn};

    SetPhase("spawn.char.begin");
    DurableSpawnLogf("engine: spawn stage begin character=%d",
                     static_cast<int>(character));

    static const wchar_t *meshes[] = {
        // Faith
        L"CH_TKY_Crim_Fixer.SK_TKY_Crim_Fixer",

        // Kate
        L"CH_TKY_Cop_Patrol_Female.SK_TKY_Cop_Patrol_Female",

        // Celeste
        L"CH_Celeste.SK_Celeste",

        // Assault Celeste
        L"CH_TKY_Cop_Pursuit_Female.SK_TKY_Cop_Pursuit_Female",

        // Jacknife
        L"CH_TKY_Crim_Jacknife.SK_TKY_Crim_Jacknife",

        // Miller
        L"CH_Miller.SK_Miller",

        // Kreeg
        L"CH_Kreeg.SK_Kreeg",

        // Pursuit Cop
        L"CH_TKY_Cop_Pursuit.SK_TKY_Cop_Pursuit",

        // Ghost
        L"TT_Ghost.GhostCharacter_01",
    };

    static const std::vector<std::wstring> materials[] = {
        // Faith
        {
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_69",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_70",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_71",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_72",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_73",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_74",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_75",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_76",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_77",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_78",
        },

        // Kate
        {
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Teeth",
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Eyes",
            L"Material CH_TKY_Crim_Fixer.unlitAlpha",
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Skin",
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Hair",
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Cloth",
        },

        // Celeste
        {
            L"Material CH_Celeste.alphablend",
            L"MaterialInstanceConstant CH_Celeste.MI_HairWTF",
            L"MaterialInstanceConstant CH_Celeste.MI_Celeste_Teeth",
            L"MaterialInstanceConstant CH_Celeste.MI_Celeste_Merged_ClothB",
            L"MaterialInstanceConstant CH_Celeste.MI_Celeste_Merged_SkinB",
            L"MaterialInstanceConstant CH_Celeste.MI_Celeste_Eyes",
        },

        // Assault Celeste
        {
            L"MaterialInstanceConstant "
            L"CH_TKY_Cop_Pursuit_Female.MI_CopPursuitFemale",
        },

        // Jacknife
        {
            L"MaterialInstanceConstant CH_TKY_Crim_Jacknife.MI_Jackknife_Teeth",
            L"MaterialInstanceConstant CH_TKY_Crim_Jacknife.MI_Jackknife_Cloth",
            L"MaterialInstanceConstant "
            L"CH_TKY_Crim_Jacknife.MI_TKY_Crim_Prowler_Eyes",
            L"Material CH_TKY_Crim_Jacknife.M_Jackknife_Eyeshade",
            L"MaterialInstanceConstant CH_TKY_Crim_Jacknife.MI_Jackknife_jSkin",
            L"MaterialInstanceConstant CH_TKY_Crim_Jacknife.MI_JackKnife_Hair",
        },

        // Miller
        {
            L"MaterialInstanceConstant CH_Miller.MI_Miller_Eyes",
            L"MaterialInstanceConstant CH_Miller.MI_Teeth",
            L"MaterialInstanceConstant CH_Miller.MI_Miller_Merged_Cloth",
            L"MaterialInstanceConstant CH_Miller.MI_Miller_Merged_Skin",
            L"Material CH_Miller.Unlit",
            L"Material CH_Miller.M_Miller_Brow",
            L"MaterialInstanceConstant CH_Miller.MI_MillerKlurre",
        },

        // Kreeg
        {
            L"MaterialInstanceConstant CH_Kreeg.MI_Kreeg_Teeth",
            L"MaterialInstanceConstant CH_Kreeg.MI_Kreeg_Cloth",
            L"MaterialInstanceConstant CH_Kreeg.MI_Kreeg_Skin",
            L"Material CH_Kreeg.M_Kreeg_Unlit",
            L"MaterialInstanceConstant CH_Kreeg.MI_Kreeg_Eyes",
        },

        // Pursuit Cop
        {
            L"MaterialInstanceConstant CH_TKY_Cop_Pursuit.MI_TKY_Cop_Pursuit",
        },

        // Ghost
        {
            L"Material TT_Ghost.Materials.M_GhostShader_01",
        },
    };

    if (character < Engine::Character::Faith ||
        character >= Engine::Character::Max) {
        DurableSpawnLogf("engine: spawn stage invalid character=%d",
                         static_cast<int>(character));
        return nullptr;
    }

    DurableSpawnLogf("engine: spawn stage getworld begin character=%d",
                     static_cast<int>(character));
    // Cached world only — GetWorld(true) freezes EndScene for tens of seconds.
    SetPhase("spawn.char.getworld");
    auto *world = Engine::GetWorld(false);
    // Fallback: hosted mode has no legacy init to warm GetWorld's cache.
    // Prefer GetPlayerController(false) (GamePlayers-seeded) over cold
    // TryFindTdGameEngine(false) — tutorial often has PC+camera while World
    // cache and TdEngine cache are still empty.
    // GetWorld(false) already seeds its static cache from PC->WorldInfo when
    // the PC cache is warm (including during EndScene).
    // Do NOT walk TdEngine->GamePlayers here from EndScene drain — that path
    // hangs the render thread after warm.cold.done (2026-07-19). Tick/PC
    // cache must already be populated before SpawnCharacter is attempted.
    DurableSpawnLogf("engine: spawn stage getworld end world=%p",
                     static_cast<const void *>(world));

    Classes::ASkeletalMeshActorSpawnable *actor = nullptr;

    // Strategy 1: spawn via WorldInfo (guarded against UE3 exceptions)
    if (world) {
        SetPhase("spawn.char.world.spawn");
        DurableSpawnLogf("engine: spawn stage world spawn attempt character=%d",
                         static_cast<int>(character));
        actor = static_cast<Classes::ASkeletalMeshActorSpawnable *>(
            MMOD_SpawnActorSafe(world, "world spawn"));
        DurableSpawnLogf("engine: spawn stage world spawn result actor=%p",
                         static_cast<const void *>(actor));
    }

    // Strategy 2: fallback to player pawn (cache-only).
    if (!actor) {
        SetPhase("spawn.char.pawn.fallback");
        DurableSpawnLogf("engine: spawn stage trying player pawn fallback");
        auto *player = Engine::GetPlayerPawn(false);
        DurableSpawnLogf("engine: spawn stage player pawn=%p",
                         static_cast<const void *>(player));
        if (player) {
            SetPhase("spawn.char.pawn.spawn");
            actor = static_cast<Classes::ASkeletalMeshActorSpawnable *>(
                MMOD_SpawnActorSafe(player, "player pawn"));
            DurableSpawnLogf("engine: spawn stage player spawn result actor=%p",
                             static_cast<const void *>(actor));
        }
    }

    // Strategy 3: tutorial often has PC + PlayerCamera before AcknowledgedPawn.
    // Spawn using camera (or PC) as owner so mesh can appear near host.
    if (!actor) {
        SetPhase("spawn.char.camera.fallback");
        DurableSpawnLogf("engine: spawn stage trying camera/pc fallback");
        if (auto *pc = Engine::GetPlayerController(false)) {
            Classes::ACamera *cam = nullptr;
            if (MeSdk::Safe::TryReadField(&pc->PlayerCamera, cam) && cam &&
                MeSdk::Safe::IsPlausibleUObject(cam)) {
                actor = static_cast<Classes::ASkeletalMeshActorSpawnable *>(
                    MMOD_SpawnActorSafe(cam, "player camera"));
                DurableSpawnLogf("engine: spawn stage camera spawn result actor=%p",
                                 static_cast<const void *>(actor));
            }
            if (!actor && MeSdk::Safe::IsPlausibleUObject(pc)) {
                actor = static_cast<Classes::ASkeletalMeshActorSpawnable *>(
                    MMOD_SpawnActorSafe(pc, "player controller"));
                DurableSpawnLogf("engine: spawn stage pc spawn result actor=%p",
                                 static_cast<const void *>(actor));
            }
        }
    }

    if (!actor) {
        DurableSpawnLogf("engine: spawn stage actor spawn returned null");
        return nullptr;
    }

    DurableSpawnLogf("engine: spawn stage actor spawned actor=%p", actor);
    actor->SetCollisionType(Classes::ECollisionType::COLLIDE_NoCollision);

    const auto mesh = actor->SkeletalMeshComponent;
    if (!mesh) {
        DurableSpawnLogf("engine: spawn stage actor has no mesh actor=%p", actor);
        actor->ShutDown();
        return nullptr;
    }

    DurableSpawnLogf("engine: spawn stage load mesh character=%d",
                     static_cast<int>(character));
    const auto skeletalMesh = static_cast<Classes::USkeletalMesh *>(
        actor->STATIC_DynamicLoadObject(
            meshes[static_cast<size_t>(character)],
            Classes::USkeletalMesh::StaticClass(), false));
    if (!skeletalMesh) {
        DurableSpawnLogf("engine: spawn stage mesh load failed character=%d",
                         static_cast<int>(character));
        actor->ShutDown();
        return nullptr;
    }

    DurableSpawnLogf("engine: spawn stage set skeletal mesh mesh=%p", skeletalMesh);
    mesh->SetSkeletalMesh(skeletalMesh, false);

    const auto &mats = materials[static_cast<size_t>(character)];
    for (auto i = 0UL; i < mats.size(); ++i) {
        DurableSpawnLogf("engine: spawn stage load material index=%lu",
                         static_cast<unsigned long>(i));
        // DynamicLoadObject wants "Package.Object". Legacy paths include a
        // class prefix ("MaterialInstanceConstant Package.Object") that
        // fails to resolve and left slots unset (console: material missing).
        std::wstring objectPath = mats[i];
        const size_t space = objectPath.find(L' ');
        if (space != std::wstring::npos && space + 1 < objectPath.size()) {
            objectPath = objectPath.substr(space + 1);
        }
        auto *material = static_cast<Classes::UMaterialInterface *>(
            actor->STATIC_DynamicLoadObject(
                objectPath.c_str(),
                Classes::UMaterialInterface::StaticClass(), true));
        if (!material) {
            material = static_cast<Classes::UMaterialInterface *>(
                actor->STATIC_DynamicLoadObject(
                    mats[i].c_str(),
                    Classes::UMaterialInterface::StaticClass(), true));
        }
        if (!material) {
            EngineCoreBridge::Log("engine: remote character material missing");
            continue;
        }

        DurableSpawnLogf("engine: spawn stage set material index=%lu material=%p",
                         static_cast<unsigned long>(i), material);
        mesh->SetMaterial(i, material);
    }

    if (character == Engine::Character::Kate ||
        character == Engine::Character::Miller ||
        character == Engine::Character::Kreeg) {

        actor->PrePivot.Z = 94;
    }

    // Remotes can sit just outside FOV (corridor stand-off); without this,
    // LocalAtoms go stale and Kate looks like an empty nametag (Mesh3p host
    // path already forces true for the same reason).
    mesh->bUpdateSkelWhenNotRendered = true;
    DurableSpawnLogf("engine: spawn stage ok actor=%p", actor);
    SetPhase("spawn.char.ok");
    return actor;
}

void __fastcall TickHook(float *scales, void *idle, int arg, float delta) {
    // Rate-limited diag — OutputDebugString every second still costs; keep rare.
    {
        static int diagFrameCounter = 0;
        static int diagFrameTotal = 0;
        ++diagFrameTotal;
        if (++diagFrameCounter >= Timing::kTargetFps * 5) {
            diagFrameCounter = 0;
            char diag[128] = {};
            const bool mrdy = modReady;
            const bool mInit = modInitializing;
            const bool hLive = Engine::IsHostedGameplayLive();
            const bool loading = levelLoad.Loading;
            snprintf(diag, sizeof(diag),
                     "engine: tick diag frame=%d ready=%d init=%d live=%d "
                     "loading=%d spawnQ=%zu cmdQ=%zu",
                     diagFrameTotal, mrdy ? 1 : 0, mInit ? 1 : 0,
                     hLive ? 1 : 0, loading ? 1 : 0,
                     spawns.Queue.size(), commands.Queue.size());
            OutputDebugStringA(diag);
            OutputDebugStringA("\n");
        }
    }

    if (!modReady || modInitializing || !Engine::IsHostedGameplayLive()) {
        tick.Original(scales, arg, delta);
        return;
    }

    SetPhase("tick.body");
    EngineInternal::DrainPendingIdlePcSeedOnGameThread();
    EngineInternal::WarmTdGameEngineOnGameThread();

    if (EngineInternal::inEndSceneSpawnDrain.load()) {
        SetPhase("tick.original");
        tick.Original(scales, arg, delta);
        SetPhase("tick.idle");
        return;
    }

    // TdEngine warm but PC/world not seeded yet — plugin ticks concurrent with
    // the last EndScene GObjects slice hung the next frame (rerun16 idle.cont).
    if (MeSdk::Safe::Gui::TryFindTdGameEngine(false) &&
        !Engine::GetPlayerController(false) && !Engine::GetWorld(false)) {
        SetPhase("tick.await_pc_seed");
        SetPhase("tick.original");
        tick.Original(scales, arg, delta);
        SetPhase("tick.idle");
        return;
    }

    if (EngineInternal::IsIdlePcSeedQuietPeriod()) {
        SetPhase("tick.quiet");
        SetPhase("tick.original");
        tick.Original(scales, arg, delta);
        SetPhase("tick.idle");
        return;
    }

    EngineInternal::CommitIdleWarmWorldAndPoseFromPc();
    DrainSoftDespawnQueue();

    // When hosted live, skip controller probes entirely — empty caches used
    // to trigger GetWorld GObjects walks and freeze right after Set Gameplay.
    const bool inGameplay = true;

    // Process spawns and console commands whenever hosted gameplay is
    // live, regardless of inGameplay.  During level transitions UObject
    // access (GetPlayerController/IsInMainMenu) crashes and returns null,
    // causing inGameplay=false and starving the spawn queue forever.
    // SpawnCharacter and console commands are SEH-guarded; failures stay
    // in the queue for retry.
    if (Engine::IsHostedGameplayLive()) {
        // Do NOT warm TdGameEngine from TickHook — a cold EnsureTdEngineNameIndex
        // / GObjects slice here freezes at tick.warm.slice (2026-07-19). EndScene
        // MMOD_EngineDrainSpawnQueue owns incremental warm when spawns are pending.

        // Console commands also need a valid game state
        if (commands.Queue.size() > 0) {
            auto console = Engine::GetConsole();

            if (console) {
                commands.Mutex.lock();

                for (auto &command : commands.Queue) {
                    console->ConsoleCommand(command.c_str());
                }

                commands.Queue.clear();
                commands.Queue.shrink_to_fit();

                commands.Mutex.unlock();
            }
        }

        if (spawns.Queue.size() > 0) {
            // Hosted mode: EndScene MMOD_EngineDrainSpawnQueue owns draining
            // (one spawn per call). Doing it again from TickHook re-enters UE3
            // Spawn via PeekMessage and hangs at spawn.char.begin (2026-07-19).
            if (hostedMode.load()) {
                SetPhase("tick.spawn.defer.hosted");
            } else {
            SetPhase("tick.spawn.drain");
            // Snapshot under lock, spawn outside — holding the mutex across
            // UE3 SpawnCharacter re-enters PeekMessage and deadlocks.
            std::vector<std::pair<Engine::Character, Classes::ASkeletalMeshActorSpawnable **>>
                batch;
            {
                spawns.Mutex.lock();
                batch.swap(spawns.Queue);
                spawns.Mutex.unlock();
            }

            std::vector<std::pair<Engine::Character, Classes::ASkeletalMeshActorSpawnable **>>
                remaining;
            for (auto &spawn : batch) {
                if (!spawn.second || *spawn.second) {
                    continue;
                }

                struct SpawnQueueContext {
                    Engine::Character character;
                    Classes::ASkeletalMeshActorSpawnable **out;
                } spawnCtx = {spawn.first, spawn.second};

                DWORD exceptionCode = 0;
                if (!PluginSehGuard::InvokeVoid(
                        "engine_spawn",
                        "engine_hooks_gameplay.cpp:TickHook",
                        [](void *data) {
                            auto *ctx = static_cast<SpawnQueueContext *>(data);
                            *ctx->out = SpawnCharacter(ctx->character);
                        },
                        &spawnCtx, &exceptionCode)) {
                    LogCallbackFault("spawn", exceptionCode);
                    // SEH crash: drop this entry
                } else if (spawnCtx.out && *spawnCtx.out) {
                    EngineCoreBridge::Log("engine: remote character spawn ok");
                } else {
                    remaining.push_back(spawn);
                }
            }

            if (!remaining.empty()) {
                spawns.Mutex.lock();
                spawns.Queue.insert(spawns.Queue.end(), remaining.begin(),
                                   remaining.end());
                spawns.Mutex.unlock();
            }
            } // !hostedMode
        }
    }

    // Hosted multiplayer needs tick callbacks (remote spawn/pose) even when
    // GetPlayerController/IsInMainMenu probes fail during intro/cinematics —
    // same rationale as draining spawns above without requiring inGameplay.
    if (inGameplay || Engine::IsHostedGameplayLive()) {
        SetPhase("tick.callbacks");
        std::vector<TickCallback> crashedCallbacks;
        const auto callbacks = CopyCallbacks(tick.Callbacks, tick.Mutex);
        int cbIndex = 0;
        for (const auto &callback : callbacks) {
            // Per-callback breadcrumb — identifies which plugin Tick freezes
            // after idle.done / PHYS_Falling (rem=2 sp=0; 2026-07-21).
            char cbPhase[32] = {};
            _snprintf_s(cbPhase, sizeof(cbPhase), _TRUNCATE, "tick.cb.%d",
                        cbIndex++);
            SetPhase(cbPhase);
            TickCallbackContext ctx = {callback, delta};
            DWORD exceptionCode = 0;
            if (!PluginSehGuard::InvokeVoid(
                    "plugin_tick", "engine_hooks_gameplay.cpp:TickHook",
                    InvokeTickCallback, &ctx, &exceptionCode)) {
                crashedCallbacks.push_back(callback);
                LogCallbackFault("tick", exceptionCode);
            }
        }
        SetPhase("tick.callbacks.done");
        for (const auto callback : crashedCallbacks) {
            RemoveCrashedCallbackLocked(tick.Callbacks, tick.Mutex, callback);
        }
    }

    SetPhase("tick.original");
    tick.Original(scales, arg, delta);
    SetPhase("tick.idle");
}
bool InstallGameplayHooksInternal() {
    void *ptr = nullptr;

    if (!Hook::TrampolineHook(LoadLibraryAHook, LoadLibraryA,
                              reinterpret_cast<void **>(&LoadLibraryAOriginal))) {
        return false;
    }

    if (!EnsurePeekMessageHookForGameplay()) {
        return false;
    }

    // ProcessEvent
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::ProcessEvent,
                                      MeSdk::Patterns::Hooks::ProcessEventMask))) {

        EngineCoreBridge::Log("engine: Failed to find ProcessEvent");
        return false;
    }

    if (!Hook::TrampolineHook(
            ProcessEventHook, ptr,
            reinterpret_cast<void **>(&processEvent.Original))) {

        EngineCoreBridge::Log("engine: Failed to hook ProcessEvent");
        return false;
    }

    // LevelLoad
    if (!(ptr = levelLoad.Base = Pattern::FindPattern(MeSdk::Patterns::Hooks::LevelLoad,
                                                      MeSdk::Patterns::Hooks::LevelLoadMask))) {

        EngineCoreBridge::Log("engine: Failed to find LevelLoad");
        return false;
    }

    if (!Hook::TrampolineHook(LevelLoadHook, ptr,
                              reinterpret_cast<void **>(&levelLoad.Original))) {

        EngineCoreBridge::Log("engine: Failed to hook LevelLoad");
        return false;
    }

    // PreDeath
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::PreDeathAnchor,
                                      MeSdk::Patterns::Hooks::PreDeathAnchorMask))) {

        EngineCoreBridge::Log("engine: Failed to find PreDeath (1)");
        return false;
    }

    if (!(ptr = death.PreBase = Pattern::FindPattern(
              ptr, 0x1000, MeSdk::Patterns::Hooks::PreDeath,
              MeSdk::Patterns::Hooks::PreDeathMask))) {

        EngineCoreBridge::Log("engine: Failed to find PreDeath (2)");
        return false;
    }

    if (!Hook::TrampolineHook(PreDeathHook, ptr,
                              reinterpret_cast<void **>(&death.PreOriginal))) {

        EngineCoreBridge::Log("engine: Failed to hook PreDeath");
        return false;
    }

    // PostDeath
    if (!(ptr = death.PostBase = Pattern::FindPattern(
              ptr, 0x1000, MeSdk::Patterns::Hooks::PostDeath,
              MeSdk::Patterns::Hooks::PostDeathMask))) {

        EngineCoreBridge::Log("engine: Failed to find PostDeath");
        return false;
    }

    if (!Hook::TrampolineHook(PostDeathHook, ptr,
                              reinterpret_cast<void **>(&death.PostOriginal))) {

        EngineCoreBridge::Log("engine: Failed to hook PostDeath");
        return false;
    }

    // ActorTick
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::ActorTick,
                                      MeSdk::Patterns::Hooks::ActorTickMask))) {

        EngineCoreBridge::Log("engine: Failed to find ActorTick");
        return false;
    }

    if (!Hook::TrampolineHook(ActorTickHook, ptr,
                              reinterpret_cast<void **>(&actorTick.Original))) {

        EngineCoreBridge::Log("engine: Failed to hook ActorTick");
        return false;
    }

    // BonesTick
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::BonesTick,
                                      MeSdk::Patterns::Hooks::BonesTickMask))) {

        EngineCoreBridge::Log("engine: Failed to find BonesTick");
        return false;
    }

    if (!Hook::TrampolineHook(BonesTickHook, RELATIVE_ADDR(ptr, 5),
                              reinterpret_cast<void **>(&bonesTick.Original))) {

        EngineCoreBridge::Log("engine: Failed to hook BonesTick");
        return false;
    }

    // ProjectionTick
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::ProjectionTick,
                                      MeSdk::Patterns::Hooks::ProjectionTickMask))) {
        EngineCoreBridge::Log("engine: Failed to find ProjectionTick");
        return false;
    }

    if (!Hook::TrampolineHook(
            ProjectionTick, ptr,
            reinterpret_cast<void **>(&projectionTick.Original))) {

        EngineCoreBridge::Log("engine: Failed to hook ProjectionTick");
        return false;
    }

    // Tick
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::Tick,
                                      MeSdk::Patterns::Hooks::TickMask))) {

        EngineCoreBridge::Log("engine: Failed to find Tick");
        return false;
    }

    if (!Hook::TrampolineHook(TickHook, ptr,
                              reinterpret_cast<void **>(&tick.Original))) {

        EngineCoreBridge::Log("engine: Failed to hook Tick");
        return false;
    }

    return true;
}
void InstallGameplayHooksOnMainThread() {
    if (gameplayHooksInstalled.load()) {
        return;
    }
    if (InstallGameplayHooksInternal()) {
        gameplayHooksInstalled = true;
        // In hosted mode, modReady is needed for TickHook/ProcessEventHook
        // guards.  engine.dll's MarkReady() is only called from the legacy
        // init path; in hosted mode module_manager manages readiness but
        // the hook guards still check modReady unconditionally.
        // MarkReady also signals modReadyEvent and clears modInitializing.
        if (hostedMode.load()) {
            Engine::MarkReady();
        }
        EngineDebugTrace::Event("engine.cpp:InstallGameplayHooksOnMainThread",
                                "gameplay_hooks_ok", "H-HOOKS", 1, 0, 0, 0);
    }
}

bool WaitForGameplayHooks(DWORD timeoutMs) {
    const DWORD deadline = GetTickCount() + timeoutMs;
    while (!gameplayHooksInstalled.load()) {
        if (GetTickCount() >= deadline) {
            return false;
        }
        Sleep(Timing::kLazyInstallPollMs);
    }
    return true;
}
} // namespace EngineInternal

bool Engine::AreGameplayHooksInstalled() {
    return EngineInternal::gameplayHooksInstalled.load();
}

bool Engine::InstallGameplayHooks() {
    if (EngineInternal::InstallGameplayHooksInternal()) {
        return true;
    }

    if (EngineInternal::hostedMode.load()) {
        QueueMainThreadTask(EngineInternal::InstallGameplayHooksOnMainThread);
        return false;
    }

    return EngineInternal::InstallGameplayHooksInternal();
}

bool Engine::EnsureGameplayHooks() {
    if (EngineInternal::gameplayHooksInstalled.load()) {
        return true;
    }

    if (TryInstallGameplayHooksSync()) {
        return true;
    }

    if (EngineInternal::hostedMode.load()) {
        QueueMainThreadTask(EngineInternal::InstallGameplayHooksOnMainThread);
	return EngineInternal::WaitForGameplayHooks(Timing::kGameplayHooksTimeoutMs);
    }

    if (EngineInternal::InstallGameplayHooksInternal()) {
        EngineInternal::gameplayHooksInstalled = true;
        return true;
    }

    return false;
}

bool Engine::TryInstallGameplayHooksSync() {
    if (EngineInternal::gameplayHooksInstalled.load()) {
        return true;
    }

    if (EngineInternal::hostedMode.load()) {
        QueueMainThreadTask(EngineInternal::InstallGameplayHooksOnMainThread);
	return EngineInternal::WaitForGameplayHooks(Timing::kGameplayHooksTimeoutMs);
    }

    if (!EngineInternal::InstallGameplayHooksInternal()) {
        return false;
    }

    EngineInternal::gameplayHooksInstalled = true;
    return true;
}

bool Engine::TryInstallGameplayHooksHosted() {
    if (EngineInternal::gameplayHooksInstalled.load()) {
        return true;
    }

    QueueMainThreadTask(EngineInternal::InstallGameplayHooksOnMainThread);
	return EngineInternal::WaitForGameplayHooks(Timing::kGameplayHooksTimeoutMs);
}
