#include "mod_registry.h"
#include "mod_registry_internal.h"

#include "debug_trace.h"
#include "game_paths.h"
#include "host_api.h"
#include "mod_console.h"
#include "mod_load_safe.h"
#include "mod_log.h"
#include "mod_security.h"
#include "mod_plugin_info.h"
#include "module_contract.h"
#include "mod_host_api.h"
#include "mmultiplayer_api.h"
#include "memory_fault_log.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace ModRegistryInternal {

void ClearEngineFeaturePluginCallbacks() {
    const HMODULE core = GetModuleHandleW(L"core.dll");
    if (!core) {
        return;
    }

    const auto getApi = reinterpret_cast<MMOD_GetMmultiplayerApiFn>(
        GetProcAddress(core, MMULTIPLAYER_API_EXPORT));
    if (!getApi) {
        return;
    }

    const MmultiplayerApi *api = getApi();
    if (api && api->version >= 2 && api->ClearFeaturePluginCallbacks) {
        api->ClearFeaturePluginCallbacks();
    }
}

void SetLoadStatus(ModuleEntry &entry, const char *status) {
    entry.status = status;
    ModLog::Writef("mod_registry: [%ls] %s", entry.id.c_str(), status);
}

const char *PhaseName(LoadPhase phase) {
    switch (phase) {
    case LoadPhase::Idle:
        return "idle";
    case LoadPhase::Queued:
        return "queued";
    case LoadPhase::Validating:
        return "validating";
    case LoadPhase::PrepDeps:
        return "prep_deps";
    case LoadPhase::LoadLibrary:
        return "load_library";
    case LoadPhase::ResolveExport:
        return "resolve_export";
    case LoadPhase::PluginInit:
        return "plugin_init";
    }
    return "unknown";
}

void LogPhaseTransition(ModuleEntry &entry, LoadPhase from, LoadPhase to) {
    const DWORD elapsed =
        entry.loadStartedTick ? GetTickCount() - entry.loadStartedTick : 0;
    ModLog::Writef("mod_registry: [%ls] phase %s -> %s (%lums)",
                   entry.id.c_str(), PhaseName(from), PhaseName(to), elapsed);
}

bool IsInProgressStatus(const std::string &status) {
    return status == "Queued for inject" || status == "Unloading..." ||
           status.rfind("Loading", 0) == 0;
}

HANDLE LoaderWakeEvent() {
    static HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) {
        OutputDebugStringA("mod_registry_load: CreateEventW for loader wake failed\n");
    }
    return eventHandle;
}

void SignalPendingOperations() {
    if (const auto eventHandle = LoaderWakeEvent()) {
        SetEvent(eventHandle);
    }
}

DWORD WaitForPendingOperationsImpl(DWORD timeoutMs) {
    const auto eventHandle = LoaderWakeEvent();
    if (!eventHandle) {
        Sleep(timeoutMs);
        return WAIT_TIMEOUT;
    }
    return WaitForSingleObject(eventHandle, timeoutMs);
}

void FailLoad(ModuleEntry &entry, const std::string &status) {
    if (entry.loadStartedTick) {
        ModLog::Writef("mod_registry: [%ls] load failed after %lums: %s",
                       entry.id.c_str(),
                       GetTickCount() - entry.loadStartedTick, status.c_str());
        entry.loadStartedTick = 0;
    }
    if (entry.module) {
        g_moduleHandles.erase(entry.module);
        FreeLibrary(entry.module);
        entry.module = nullptr;
    }
    entry.pendingInit = nullptr;
    entry.loaded = false;
    entry.busy = false;
    entry.loadPhase = LoadPhase::Idle;
    SetLoadStatus(entry, status.c_str());
}

static void RunLoadLibraryPhase(const std::wstring &moduleId) {
    std::wstring dllPath;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto *entry = FindEntryById(moduleId);
        if (!entry || entry->loadPhase != LoadPhase::LoadLibrary) {
            return;
        }
        SetLoadStatus(*entry, "Loading: loading library...");
        dllPath = entry->dllPath;
    }

    DebugTrace::Event("mod_registry.cpp:LoadModule", "load_start", "H6",
                      GetCurrentThreadId(),
                      reinterpret_cast<uintptr_t>(dllPath.c_str()), 0, 0);

    DWORD exceptionCode = 0;
    const auto module =
        ModLoadSafe::LoadModuleLibrary(dllPath.c_str(), &exceptionCode);

    std::lock_guard<std::mutex> lock(g_mutex);
    auto *entry = FindEntryById(moduleId);
    if (!entry || entry->loadPhase != LoadPhase::LoadLibrary) {
        if (module) {
            FreeLibrary(module);
        }
        return;
    }

    if (!module) {
        if (exceptionCode != 0) {
            char codeText[16] = {};
            snprintf(codeText, sizeof(codeText), "%08lX",
                     static_cast<unsigned long>(exceptionCode));
            MemoryFaultLog_Record(exceptionCode, 0, 0, "load_library",
                                  "mod_registry_load.cpp:RunLoadLibraryPhase");
            FailLoad(*entry, std::string("LoadLibrary crashed (0x") + codeText + ")");
            ModLog::Writef("mod_registry: LoadLibrary exception for %ls code=0x%08lx",
                           entry->dllPath.c_str(), exceptionCode);
            DebugTrace::Event("mod_registry.cpp:LoadModule", "load_library_crash",
                              "H1", exceptionCode, 0, 0, 0);
            return;
        }
        const auto err = GetLastError();
        FailLoad(*entry, "LoadLibrary failed err=" + std::to_string(err));
        ModLog::Writef("mod_registry: failed to load %ls err=%lu", entry->dllPath.c_str(),
                       err);
        DebugTrace::Event("mod_registry.cpp:LoadModule", "load_library_fail", "H1", err,
                          0, 0, 0);
        return;
    }

    entry->module = module;
    g_moduleHandles[module] = entry->id;
    ModLog::Writef("mod_registry: LoadLibrary OK %ls hModule=0x%p thread=%lu",
                   entry->dllPath.c_str(), module, GetCurrentThreadId());
    DebugTrace::Event("mod_registry.cpp:LoadModule", "load_library_ok", "H1",
                      reinterpret_cast<uintptr_t>(module), 0, 0, 0);
    LogPhaseTransition(*entry, LoadPhase::LoadLibrary, LoadPhase::ResolveExport);
    entry->loadPhase = LoadPhase::ResolveExport;
}

static void RunPluginInitPhase(const std::wstring &moduleId) {
    MMOD_PluginInitializeFn initFn = nullptr;
    HMODULE module = nullptr;
    std::wstring dllPath;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto *entry = FindEntryById(moduleId);
        if (!entry || entry->loadPhase != LoadPhase::PluginInit) {
            return;
        }
        SetLoadStatus(*entry, "Loading: initializing plugin...");
        initFn = entry->pendingInit;
        module = entry->module;
        dllPath = entry->dllPath;
    }

    DWORD exceptionCode = 0;
    const bool initOk = ModLoadSafe::CallPluginInitialize(
        initFn, HostApi_Get(), module, &exceptionCode);

    std::lock_guard<std::mutex> lock(g_mutex);
    auto *entry = FindEntryById(moduleId);
    if (!entry || entry->loadPhase != LoadPhase::PluginInit) {
        return;
    }

    if (!initOk) {
        if (exceptionCode != 0) {
            char codeText[16] = {};
            snprintf(codeText, sizeof(codeText), "%08lX",
                     static_cast<unsigned long>(exceptionCode));
            MemoryFaultLog_Record(exceptionCode, 0, 0, "plugin_init",
                                  "mod_registry_load.cpp:RunPluginInitPhase");
            FailLoad(*entry, std::string("PluginInitialize crashed (0x") + codeText +
                                   ")");
            ModLog::Writef("mod_registry: plugin init exception for %ls code=0x%08lx",
                           entry->dllPath.c_str(), exceptionCode);
            DebugTrace::Event("mod_registry.cpp:LoadModule", "plugin_init_crash", "H3",
                              exceptionCode, 0, 0, 0);
        } else {
            FailLoad(*entry, "PluginInitialize returned false");
            ModLog::Writef("mod_registry: plugin init failed for %ls", dllPath.c_str());
            DebugTrace::Event("mod_registry.cpp:LoadModule", "plugin_init_fail", "H3", 0,
                              0, 0, 0);
        }
        return;
    }

    DebugTrace::Event("mod_registry.cpp:LoadModule", "plugin_init_ok", "H3",
                      reinterpret_cast<uintptr_t>(module), 0, 0, 0);

    const DWORD totalMs =
        entry->loadStartedTick ? GetTickCount() - entry->loadStartedTick : 0;
    ModLog::Writef("mod_registry: PluginInitialize OK %ls (%lums, hModule=0x%p)",
                   entry->dllPath.c_str(), totalMs, entry->module);

    entry->pendingInit = nullptr;
    entry->loaded = true;
    entry->busy = false;
    entry->loadPhase = LoadPhase::Idle;
    entry->loadStartedTick = 0;
    SetLoadStatus(*entry, "Loaded");
}

void AdvanceLoadPhase(ModuleEntry &entry) {
    switch (entry.loadPhase) {
    case LoadPhase::Idle:
        return;

    case LoadPhase::Queued:
        entry.busy = true;
        if (!entry.loadStartedTick) {
            entry.loadStartedTick = GetTickCount();
        }
        LogPhaseTransition(entry, LoadPhase::Queued, LoadPhase::Validating);
        entry.loadPhase = LoadPhase::Validating;
        return;

    case LoadPhase::Validating: {
        SetLoadStatus(entry, "Loading: validating module...");
        std::wstring modulesDir;
        if (!GamePaths::GetModulesDirectory(modulesDir)) {
            FailLoad(entry, "Modules directory unavailable");
            return;
        }

        std::wstring moduleId;
        std::string rejectReason;
        if (!ModSecurity::ValidateModuleDllPath(modulesDir, entry.dllPath, moduleId,
                                                rejectReason)) {
            FailLoad(entry, rejectReason);
            return;
        }

        if (_wcsicmp(moduleId.c_str(), entry.id.c_str()) != 0) {
            FailLoad(entry, "Module id/path mismatch");
            ModLog::Writef("mod_registry: [%ls] id mismatch (folder=%ls path=%ls)",
                           entry.id.c_str(), entry.id.c_str(), moduleId.c_str());
            return;
        }

        LogPhaseTransition(entry, LoadPhase::Validating, LoadPhase::PrepDeps);
        entry.loadPhase = LoadPhase::PrepDeps;
        return;
    }

    case LoadPhase::PrepDeps: {
        SetLoadStatus(entry, "Loading: preparing dependencies...");

        if (entry.requiresId.empty() && !entry.hasPluginInfo) {
            ProbePluginInfoFromDll(entry);
        }

        if (!entry.requiresId.empty()) {
            const std::wstring requireWide(entry.requiresId.begin(),
                                           entry.requiresId.end());
            EnsureModuleEntryLocked(requireWide);

            const auto *dep = FindEntryById(requireWide);
            if (!dep || !dep->loaded) {
                if (dep && dep->loadPhase == LoadPhase::Idle && !dep->busy) {
                    ModLog::Writef("mod_registry: [%ls] queuing dependency %s",
                                   entry.id.c_str(), entry.requiresId.c_str());
                    QueueLoadModule(requireWide, "dependency");
                }

                SetLoadStatus(entry, "Loading: waiting for dependency...");
                return;
            }

            if (entry.requiresMinVersion != 0) {
                if (!dep->hasPluginInfo) {
                    FailLoad(entry, "Requires " + entry.requiresId + " plugin info");
                    return;
                }

                const unsigned loadedVersion = MMOD_PACK_VERSION(
                    dep->pluginMajor, dep->pluginMinor, dep->pluginPatch);
                if (loadedVersion < entry.requiresMinVersion) {
                    char buf[128] = {};
                    snprintf(buf, sizeof(buf), "Requires %s >= %u.%u.%u",
                             entry.requiresId.c_str(),
                             MMOD_UNPACK_VERSION_MAJOR(entry.requiresMinVersion),
                             MMOD_UNPACK_VERSION_MINOR(entry.requiresMinVersion),
                             MMOD_UNPACK_VERSION_PATCH(entry.requiresMinVersion));
                    FailLoad(entry, buf);
                    return;
                }
            }
        }

        LogPhaseTransition(entry, LoadPhase::PrepDeps, LoadPhase::LoadLibrary);
        entry.loadPhase = LoadPhase::LoadLibrary;
        return;
    }

    case LoadPhase::LoadLibrary:
        return;

    case LoadPhase::ResolveExport: {
        SetLoadStatus(entry, "Loading: resolving plugin export...");
        entry.pendingInit = reinterpret_cast<MMOD_PluginInitializeFn>(
            GetProcAddress(entry.module, MMOD_PLUGIN_INIT_EXPORT));
        if (!entry.pendingInit) {
            entry.pendingInit = reinterpret_cast<MMOD_PluginInitializeFn>(
                GetProcAddress(entry.module, "_MMOD_PluginInitialize"));
        }

        if (!entry.pendingInit) {
            FailLoad(entry, "Missing MMOD_PluginInitialize export");
            ModLog::Writef("mod_registry: rejected %ls (no plugin export)",
                           entry.dllPath.c_str());
            DebugTrace::Event("mod_registry.cpp:LoadModule", "plugin_export_missing",
                              "H2", GetLastError(), 0, 0, 0);
            return;
        }

        ModLog::Writef(
            "mod_registry: resolved %s at 0x%p for %ls",
            MMOD_PLUGIN_INIT_EXPORT, reinterpret_cast<void *>(entry.pendingInit),
            entry.dllPath.c_str());
        DebugTrace::Event("mod_registry.cpp:LoadModule", "plugin_export_ok", "H2",
                          reinterpret_cast<uintptr_t>(entry.pendingInit),
                          reinterpret_cast<uintptr_t>(HostApi_Get()), 0, 0);

        TryReadPluginInfo(entry.module, entry);
        const auto depError = ValidatePluginDependenciesLocked(entry);
        if (!depError.empty()) {
            FailLoad(entry, depError);
            return;
        }

        LogPhaseTransition(entry, LoadPhase::ResolveExport, LoadPhase::PluginInit);
        entry.loadPhase = LoadPhase::PluginInit;
        return;
    }

    case LoadPhase::PluginInit:
        return;
    }
}

bool UnloadModuleLocked(ModuleEntry &entry) {
    if (!entry.module) {
        entry.loaded = false;
        entry.status = "Not loaded";
        entry.busy = false;
        return true;
    }
    if (entry.busy) {
        return false;
    }

    entry.busy = true;
    entry.status = "Unloading...";
    ModLog::Writef("mod_registry: [%ls] unload start %ls", entry.id.c_str(),
                   entry.dllPath.c_str());

    auto shutdown = reinterpret_cast<MMOD_PluginShutdownFn>(
        GetProcAddress(entry.module, MMOD_PLUGIN_SHUTDOWN_EXPORT));
    if (!shutdown) {
        shutdown = reinterpret_cast<MMOD_PluginShutdownFn>(
            GetProcAddress(entry.module, "_MMOD_PluginShutdown"));
    }

    DWORD exceptionCode = 0;
    if (shutdown) {
        ModLog::Writef("mod_registry: [%ls] calling PluginShutdown", entry.id.c_str());
        ModLoadSafe::CallPluginShutdown(shutdown, entry.module, &exceptionCode);
        if (exceptionCode != 0) {
            MemoryFaultLog_Record(exceptionCode, 0, 0, "plugin_shutdown",
                                  "mod_registry_load.cpp:RunUnloadPhase");
            ModLog::Writef(
                "mod_registry: plugin shutdown exception for %ls code=0x%08lx",
                entry.dllPath.c_str(), exceptionCode);
        }
    } else {
        ModLog::Writef("mod_registry: [%ls] no PluginShutdown export", entry.id.c_str());
    }

    ClearEngineFeaturePluginCallbacks();

    FreeLibrary(entry.module);
    g_moduleHandles.erase(entry.module);
    entry.module = nullptr;
    entry.loaded = false;
    entry.status = "Not loaded";
    entry.busy = false;
    ModLog::Writef("mod_registry: unloaded %ls", entry.dllPath.c_str());
    return true;
}

void ProcessPendingOperationsImpl() {
    std::wstring unloadId;
    std::wstring loadingId;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        unloadId = g_pendingUnloadId;
        g_pendingUnloadId.clear();

        std::wstring waitingPrepDepsId;
        for (const auto &entry : g_modules) {
            if (entry.loadPhase == LoadPhase::Idle) {
                continue;
            }
            if (entry.loadPhase == LoadPhase::PrepDeps) {
                if (waitingPrepDepsId.empty()) {
                    waitingPrepDepsId = entry.id;
                }
                continue;
            }
            loadingId = entry.id;
            break;
        }
        if (loadingId.empty()) {
            loadingId = waitingPrepDepsId;
        }
    }

    if (!loadingId.empty()) {
        // #region agent log
        DebugTrace::SessionEvent("mod_registry.cpp:ProcessPending", "pending_ops",
                                 "H3", 1u, 0u,
                                 static_cast<int>(GetCurrentThreadId()));
        // #endregion

        for (int burst = 0; burst < 12; ++burst) {
            LoadPhase phase = LoadPhase::Idle;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (auto *entry = FindEntryById(loadingId)) {
                    if (entry->loadPhase == LoadPhase::Idle) {
                        break;
                    }
                    phase = entry->loadPhase;
                } else {
                    break;
                }
            }

            const auto phaseBefore = phase;
            if (phase == LoadPhase::LoadLibrary) {
                RunLoadLibraryPhase(loadingId);
            } else if (phase == LoadPhase::PluginInit) {
                RunPluginInitPhase(loadingId);
            } else {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (auto *entry = FindEntryById(loadingId)) {
                    if (entry->loadPhase == LoadPhase::Idle) {
                        break;
                    }
                    const auto before = entry->loadPhase;
                    AdvanceLoadPhase(*entry);
                    if (entry->loadPhase == LoadPhase::Idle ||
                        entry->loadPhase == before) {
                        break;
                    }
                    continue;
                }
                break;
            }

            LoadPhase after = LoadPhase::Idle;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (auto *entry = FindEntryById(loadingId)) {
                    after = entry->loadPhase;
                }
            }
            if (after == LoadPhase::Idle || after == phaseBefore) {
                if (after == LoadPhase::PrepDeps && phaseBefore == LoadPhase::PrepDeps) {
                    SignalPendingOperations();
                }
                break;
            }
        }
    }

    if (!unloadId.empty()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto *entry = FindEntryById(unloadId)) {
            UnloadModuleLocked(*entry);
        }
    }

    ModRegistry::TryQueueSettingsAutoLoadMods();
}

void QueueLoadModule(const std::wstring &moduleId, const char *source) {
    bool started = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto *entry = FindEntryById(moduleId)) {
            if (entry->busy || entry->loaded ||
                entry->loadPhase != LoadPhase::Idle) {
                ModLog::Writef(
                    "mod_registry: queue inject ignored %ls via %s (busy=%d "
                    "loaded=%d phase=%s)",
                    moduleId.c_str(), source ? source : "unknown", entry->busy ? 1 : 0,
                    entry->loaded ? 1 : 0, PhaseName(entry->loadPhase));
                return;
            }
            entry->loadPhase = LoadPhase::Queued;
            entry->loadStartedTick = GetTickCount();
            ModLog::Writef("mod_registry: queue inject %ls via %s path=%ls",
                           moduleId.c_str(), source ? source : "unknown",
                           entry->dllPath.c_str());
            SetLoadStatus(*entry, "Queued for inject");
            started = true;
            // #region agent log
            DebugTrace::SessionEvent("mod_registry.cpp:QueueLoad", "queued", "H4",
                                     entry->busy ? 1u : 0u, 1u,
                                     static_cast<int>(moduleId.size()));
            // #endregion
        } else {
            ModLog::Writef("mod_registry: queue inject failed %ls via %s (not found)",
                           moduleId.c_str(), source ? source : "unknown");
        }
    }

    if (started && (!source || strcmp(source, "auto-bootstrap") != 0)) {
        ModConsole::Show();
    }

    if (started) {
        SignalPendingOperations();
    }
}

void QueueUnloadModule(const std::wstring &moduleId) {
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pendingUnloadId = moduleId;
        if (auto *entry = FindEntryById(moduleId)) {
            entry->status = "Queued for unload";
            queued = true;
        }
    }
    if (queued) {
        SignalPendingOperations();
    }
}
} // namespace ModRegistryInternal

