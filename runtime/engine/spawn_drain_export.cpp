#include "engine_internal.h"
#include "engine_core_bridge.h"
#include "me_sdk/runtime/safe_gui.h"

#include <cstdio>

namespace EngineInternal {
std::atomic<bool> inEndSceneSpawnDrain{false};
std::atomic<bool> pendingIdlePcSeed{false};
}

// No-unwind helper: wrap EngineInternal::SpawnCharacter with C++ try/catch
// to catch C++ exceptions from UE3 UObject access (which __try/__except misses).
static void SpawnCharacterSafe(Engine::Character character,
                               Classes::ASkeletalMeshActorSpawnable **outActor) {
    try {
        *outActor = EngineInternal::SpawnCharacter(character);
    } catch (...) {
        char crashBuf[128] = {};
        snprintf(crashBuf, sizeof(crashBuf),
                 "SpawnCharacterSafe: CXX_CRASH char=%d\n",
                 static_cast<int>(character));
        OutputDebugStringA(crashBuf);

        char tmpPath[MAX_PATH] = {};
        if (GetTempPathA(sizeof(tmpPath), tmpPath)) {
            char drainLog[MAX_PATH] = {};
            snprintf(drainLog, sizeof(drainLog), "%sspawn_drain_trace.txt", tmpPath);
            HANDLE h = CreateFileA(drainLog, FILE_APPEND_DATA, FILE_SHARE_READ,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                SYSTEMTIME now = {};
                GetLocalTime(&now);
                char line[256] = {};
                int len = snprintf(line, sizeof(line),
                    "%02u:%02u:%02u.%03u SPAWN_CXX_CRASH char=%d\r\n",
                    now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                    static_cast<int>(character));
                if (len > 0) {
                    DWORD written = 0;
                    WriteFile(h, line, static_cast<DWORD>(len), &written, nullptr);
                }
                CloseHandle(h);
            }
        }
        *outActor = nullptr;
    }
}

extern "C" {

// Direct spawn: called from multiplayer.dll client lambda to spawn a character
// immediately on the main thread, bypassing the internal queue which depends
// on TickHook (not firing in all hosted-mode states).

// Inner helper: catches C++ exceptions from UE3 UObject access.
// Must be in a separate function from the SEH __try/__except because MSVC
// cannot mix both forms of exception handling in the same function.
static Classes::ASkeletalMeshActorSpawnable *
SpawnCharacterDirectInner(Engine::Character character) {
    try {
        return EngineInternal::SpawnCharacter(character);
    } catch (...) {
        char buf[128] = {};
        snprintf(buf, sizeof(buf),
                 "MMOD_EngineSpawnCharacterDirect: CXX_CRASH char=%d\n",
                 static_cast<int>(character));
        OutputDebugStringA(buf);
        return nullptr;
    }
}

// Outer wrapper: catches SEH (structured) exceptions.
__declspec(dllexport) Classes::ASkeletalMeshActorSpawnable *__cdecl
MMOD_EngineSpawnCharacterDirect(Engine::Character character) {
    Classes::ASkeletalMeshActorSpawnable *result = nullptr;
    __try {
        result = SpawnCharacterDirectInner(character);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char buf[128] = {};
        snprintf(buf, sizeof(buf),
                 "MMOD_EngineSpawnCharacterDirect: SEH_CRASH char=%d code=0x%08lx\n",
                 static_cast<int>(character),
                 GetExceptionCode());
        OutputDebugStringA(buf);
        result = nullptr;
    }
    return result;
}

// Called from module_manager's EndScene hook on the main thread to drain
// EngineInternal::spawns.Queue when TickHook is not firing (hosted mode where
// modReady is managed by module_manager, not by engine.dll itself).
__declspec(dllexport) void __cdecl MMOD_EngineDrainSpawnQueue() {
    static int callCount = 0;
    static int spawnCount = 0;
    static int pendingEnterLogs = 0;

    size_t pending = 0;
    {
        EngineInternal::spawns.Mutex.lock();
        pending = EngineInternal::spawns.Queue.size();
        EngineInternal::spawns.Mutex.unlock();
    }
    if (pending == 0) {
        // Empty-queue TdEngine idle warm moved to TickHook (game thread only).
        // EndScene GObjects walk concurrent with game tick hung after idle.cont
        // (rerun16/17). EndScene only queues PC seed once engine is warm.
        if (EngineInternal::hostedGameplayLive.load() &&
            MeSdk::Safe::Gui::TryFindTdGameEngine(false)) {
            EngineInternal::RequestIdlePcSeed();
            EngineInternal::SetPhase("drain.warm.engine.idle.done");
        }
        ++callCount;
        return;
    }

    // Block GamePlayers while draining/spawning (EndScene render thread).
    EngineInternal::inEndSceneSpawnDrain.store(true);
    struct DrainGuard {
        ~DrainGuard() { EngineInternal::inEndSceneSpawnDrain.store(false); }
    } drainGuard;

    // Always log the first 40 pending ENTERs (callCount is huge after ~60s idle
    // warm; the old callCount%60 gate hid rem=2/sp=0 with no drain file).
    if (pendingEnterLogs < 40 || (callCount % 60) == 0) {
        ++pendingEnterLogs;
        char tmpPath[MAX_PATH] = {};
        if (GetTempPathA(sizeof(tmpPath), tmpPath)) {
            char drainLog[MAX_PATH] = {};
            snprintf(drainLog, sizeof(drainLog), "%sspawn_drain_trace.txt", tmpPath);
            HANDLE h = CreateFileA(drainLog, FILE_APPEND_DATA, FILE_SHARE_READ,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                SYSTEMTIME now = {};
                GetLocalTime(&now);
                char line[256] = {};
                int len = snprintf(line, sizeof(line),
                                   "%02u:%02u:%02u.%03u ENTER call=%d pending=%zu\r\n",
                                   now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                                   callCount + 1, pending);
                if (len > 0) {
                    DWORD written = 0;
                    WriteFile(h, line, static_cast<DWORD>(len), &written, nullptr);
                }
                CloseHandle(h);
            }
        }
    }

    std::vector<std::pair<Engine::Character, Classes::ASkeletalMeshActorSpawnable **>>
        batch;
    {
        EngineInternal::spawns.Mutex.lock();
        batch.swap(EngineInternal::spawns.Queue);
        EngineInternal::spawns.Mutex.unlock();
    }
    const size_t qsize = batch.size();
    EngineInternal::SetPhase("drain.enter");

    // Spawn outside the mutex — holding it across UE3 SpawnCharacter re-enters
    // PeekMessage/EndScene and deadlocks on the non-recursive lock.
    // At most one UE3 spawn attempt per EndScene — retries pile up otherwise
    // and SpawnActor/GetWorld work freezes the game.
    std::vector<std::pair<Engine::Character, Classes::ASkeletalMeshActorSpawnable **>>
        remaining;
    bool attempted = false;
    for (size_t i = 0; i < batch.size(); ++i) {
        auto &s = batch[i];
        if (!s.second || *s.second) {
            continue;
        }

        // Deduplicate identical out pointers within this batch.
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            if (batch[j].second == s.second) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        if (attempted) {
            remaining.push_back(s);
            continue;
        }
        attempted = true;

        // Need World or a Tick-seeded PC before SpawnCharacter. Requiring
        // World alone left rem=2/sp=0 after A1 pre-bot pose (PC+camera live,
        // PC->WorldInfo still empty; drain.wait.world forever; 2026-07-20).
        // EndScene GetPlayerController is peek/GamePlayers-skipped — safe.
        // Do NOT Call TryWarmActiveWorldInfoIncremental from EndScene drain.
        if (!Engine::GetWorld(false) &&
            !Engine::GetPlayerController(false)) {
            static ULONGLONG s_lastColdWarmMs = 0;
            static int s_skipColdLogs = 0;
            const ULONGLONG nowMs = GetTickCount64();
            if (s_skipColdLogs < 20) {
                ++s_skipColdLogs;
                char tmpPathSkip[MAX_PATH] = {};
                if (GetTempPathA(sizeof(tmpPathSkip), tmpPathSkip)) {
                    char drainLog[MAX_PATH] = {};
                    snprintf(drainLog, sizeof(drainLog),
                             "%sspawn_drain_trace.txt", tmpPathSkip);
                    HANDLE hs = CreateFileA(drainLog, FILE_APPEND_DATA,
                                            FILE_SHARE_READ, nullptr,
                                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                            nullptr);
                    if (hs != INVALID_HANDLE_VALUE) {
                        SYSTEMTIME now = {};
                        GetLocalTime(&now);
                        char line[192] = {};
                        int len = snprintf(
                            line, sizeof(line),
                            "%02u:%02u:%02u.%03u SPAWN_SKIP_COLD char=%d "
                            "tdEngine=%d\r\n",
                            now.wHour, now.wMinute, now.wSecond,
                            now.wMilliseconds, static_cast<int>(s.first),
                            MeSdk::Safe::Gui::TryFindTdGameEngine(false) ? 1
                                                                         : 0);
                        if (len > 0) {
                            DWORD written = 0;
                            WriteFile(hs, line, static_cast<DWORD>(len),
                                      &written, nullptr);
                        }
                        CloseHandle(hs);
                    }
                }
            }
            if (!MeSdk::Safe::Gui::TryFindTdGameEngine(false) &&
                (nowMs - s_lastColdWarmMs) >= 500) {
                s_lastColdWarmMs = nowMs;
                EngineInternal::SetPhase("drain.wait.pc");
                // Cold GObjects warm runs on game thread (WarmTdGameEngineOnGameThread).
            } else if (MeSdk::Safe::Gui::TryFindTdGameEngine(false)) {
                EngineInternal::RequestIdlePcSeed();
                EngineInternal::SetPhase("drain.wait.pc");
            }
            remaining.push_back(s);
            continue;
        }

        EngineInternal::SetPhase("drain.spawn");
        {
            char tmpPath3[MAX_PATH] = {};
            if (GetTempPathA(sizeof(tmpPath3), tmpPath3)) {
                char drainLog[MAX_PATH] = {};
                snprintf(drainLog, sizeof(drainLog), "%sspawn_drain_trace.txt",
                         tmpPath3);
                HANDLE h3 = CreateFileA(drainLog, FILE_APPEND_DATA,
                                       FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h3 != INVALID_HANDLE_VALUE) {
                    char line[256] = {};
                    SYSTEMTIME now = {};
                    GetLocalTime(&now);
                    int len = snprintf(
                        line, sizeof(line),
                        "%02u:%02u:%02u.%03u SPAWN_BEGIN i=%zu char=%d out=%p\r\n",
                        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, i,
                        static_cast<int>(s.first),
                        static_cast<const void *>(s.second));
                    if (len > 0) {
                        DWORD written = 0;
                        WriteFile(h3, line, static_cast<DWORD>(len), &written,
                                  nullptr);
                    }
                    CloseHandle(h3);
                }
            }
        }
        SpawnCharacterSafe(s.first, s.second);
        EngineInternal::SetPhase("drain.spawn.done");

        if (s.second && *s.second) {
            ++spawnCount;
            char tmpPath2[MAX_PATH] = {};
            if (GetTempPathA(sizeof(tmpPath2), tmpPath2)) {
                char drainLog[MAX_PATH] = {};
                snprintf(drainLog, sizeof(drainLog), "%sspawn_drain_trace.txt", tmpPath2);
                HANDLE h2 = CreateFileA(drainLog, FILE_APPEND_DATA, FILE_SHARE_READ,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h2 != INVALID_HANDLE_VALUE) {
                    char line[256] = {};
                    SYSTEMTIME now = {};
                    GetLocalTime(&now);
                    int len = snprintf(line, sizeof(line),
                        "%02u:%02u:%02u.%03u SPAWN_OK i=%zu char=%d out=%p actor=%p\r\n",
                        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                        i, static_cast<int>(s.first),
                        static_cast<const void *>(s.second),
                        static_cast<const void *>(*s.second));
                    if (len > 0) {
                        DWORD written = 0;
                        WriteFile(h2, line, static_cast<DWORD>(len), &written, nullptr);
                    }
                    CloseHandle(h2);
                }
            }
        } else {
            remaining.push_back(s);
            char tmpPath4[MAX_PATH] = {};
            if (GetTempPathA(sizeof(tmpPath4), tmpPath4)) {
                char drainLog[MAX_PATH] = {};
                snprintf(drainLog, sizeof(drainLog), "%sspawn_drain_trace.txt",
                         tmpPath4);
                HANDLE h4 = CreateFileA(drainLog, FILE_APPEND_DATA,
                                       FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h4 != INVALID_HANDLE_VALUE) {
                    char line[256] = {};
                    SYSTEMTIME now = {};
                    GetLocalTime(&now);
                    int len = snprintf(
                        line, sizeof(line),
                        "%02u:%02u:%02u.%03u SPAWN_FAIL i=%zu char=%d out=%p\r\n",
                        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, i,
                        static_cast<int>(s.first),
                        static_cast<const void *>(s.second));
                    if (len > 0) {
                        DWORD written = 0;
                        WriteFile(h4, line, static_cast<DWORD>(len), &written,
                                  nullptr);
                    }
                    CloseHandle(h4);
                }
            }
        }
    }

    if (!remaining.empty()) {
        EngineInternal::spawns.Mutex.lock();
        EngineInternal::spawns.Queue.insert(EngineInternal::spawns.Queue.end(),
                                           remaining.begin(), remaining.end());
        EngineInternal::spawns.Mutex.unlock();
    }

    bool needWarm = !remaining.empty();

    if (needWarm) {
        // Progress TdEngine cache for the next frame's fallback chain — never
        // run this before SpawnCharacter (a hung slice used to starve spawn).
        // Also never call TryFindTdPlayerController from here (full GObjects).
        // Throttle: failed-spawn warm every EndScene previously re-entered
        // deep UE3 paths and could hang (2026-07-19).
        static ULONGLONG s_lastFailWarmMs = 0;
        const ULONGLONG nowFail = GetTickCount64();
        if (!MeSdk::Safe::Gui::TryFindTdGameEngine(false) &&
            (nowFail - s_lastFailWarmMs) >= 750) {
            s_lastFailWarmMs = nowFail;
            EngineInternal::SetPhase("drain.warm.slice");
            if (auto *pc = Engine::GetPlayerController(false)) {
                MeSdk::Safe::Gui::TrySeedTdGameEngineFromPlayerController(pc);
            }
            MeSdk::Safe::Gui::TryWarmTdGameEngineIncremental(2000);
            EngineInternal::SetPhase(
                MeSdk::Safe::Gui::TryFindTdGameEngine(false)
                    ? "drain.warm.slice.done"
                    : "drain.warm.slice.cont");
        }
    }
    // Empty-queue WorldInfo warm is banned (IsHungAppWindow; 2026-07-20).
    // Empty-queue TdEngine-only warm (budget 800 / 500ms) runs in the
    // pending==0 early path above for A1/A2 pre-bot host pose.

    ++callCount;

    if (spawnCount > 0 || callCount <= 5 || callCount % 60 == 0) {
        char buf[128] = {};
        snprintf(buf, sizeof(buf),
                 "engine: MmodDrainSpawnQueue %d queue=%zu spawned=%d",
                 callCount, qsize, spawnCount);
        EngineCoreBridge::Log(buf);
    }
}

}
