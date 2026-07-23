#include <windows.h>

#include "host_api.h"
#include "menu.h"
#include "mod_console.h"
#include "mod_ipc.h"
#include "mod_log.h"
#include "mod_registry.h"
#include "module_contract.h"
#include "presentation.h"
#include "debug_trace.h"
#include "hook.h"
#include "win_input.h"
#include "window_layout_settings.h"
#include "timing_constants.h"

static HANDLE g_readyEvent = nullptr;

static DWORD WINAPI PumpThread(LPVOID) {
    // #region agent log
    DebugTrace::SessionEvent("main.cpp:PumpThread", "thread_start", "H3", GetCurrentThreadId(),
                  0, 0);
    // #endregion
    auto ticks = 0;
    while (true) {
        ModRegistry::ProcessPendingOperations();
        ModIpc::Pump();
        Presentation::Pump();
        if ((++ticks % 90) == 0) {
            // #region agent log
            DebugTrace::SessionEvent("main.cpp:PumpThread", "heartbeat", "H3",
                          static_cast<uintptr_t>(ticks), 0, 0);
            // #endregion
        }
        ModRegistry::WaitForPendingOperations(8);
    }

    return 0;
}

static DWORD WINAPI AutoLoadBootstrapThread(LPVOID) {
    ModRegistry::BootstrapAutoLoad();
    return 0;
}

static void WaitForD3D9BeforeBootstrap() {
    if (GetModuleHandleW(L"d3d9.dll")) {
        // Proxy path: module_manager loads from d3d9 after CreateDevice.
        Sleep(WindowLayout_IsEnabled() ? 16u : 32u);
        return;
    }

    const DWORD maxWaitMs = WindowLayout_IsEnabled() ? 500u : 8000u;
    const DWORD deadline = GetTickCount() + maxWaitMs;
    while (!GetModuleHandleW(L"d3d9.dll") && GetTickCount() < deadline) {
        Sleep(Timing::kD3D9PollMs);
    }
}

static bool IsD3D9ProxyActive() {
    const auto d3d9 = GetModuleHandleW(L"d3d9.dll");
    return d3d9 &&
           GetProcAddress(d3d9, "MmProxyGetCachedPresentParameters") != nullptr;
}

static DWORD WINAPI InitWorker(LPVOID) {
    ModLog::Initialize();
    // #region agent log
    DebugTrace::Event("main.cpp:InitWorker", "dll_init_start", "baseline", 0, 0,
                      0, 0);
    // #endregion
    ModLog::Write("module_manager: starting");

    WaitForD3D9BeforeBootstrap();

    // Belt-and-suspenders: WaitForD3D9BeforeBootstrap() already timed out
    // after 8 s.  d3d9.dll stays resident once loaded, so this second loop
    // is a no-op in practice — kept for safety in rare edge cases.
    while (!GetModuleHandleW(L"d3d9.dll")) {
        Sleep(Timing::kD3D9PollMs);
    }
    // #region agent log
    DebugTrace::Event("main.cpp:InitWorker", "d3d9_loaded", "F", 0, 0, 0, 0);
    // #endregion
    if (IsD3D9ProxyActive()) {
        Presentation::NoteInjectTime();
        ModLog::Write(
            "module_manager: d3d9 proxy active; message bootstrap skipped");
    } else {
        ModLog::Write("module_manager: d3d9.dll loaded, installing message bootstrap");
        Presentation::InstallBootstrap();
    }
    // #region agent log
    DebugTrace::Event("main.cpp:InitWorker", "bootstrap_done", "F", 0, 0, 0, 0);
    // #endregion

    if (const auto d3d9 = GetModuleHandleW(L"d3d9.dll")) {
        using RetryNotifyFn = void(__stdcall *)();
        const auto retry = reinterpret_cast<RetryNotifyFn>(
            GetProcAddress(d3d9, "MmProxyRetryDeviceNotify"));
        if (retry) {
            retry();
        }
    }

    HostMenu::Initialize();
    ModConsole::Initialize();
    ModRegistry::DiscoverModules();

    const auto pumpThread =
        CreateThread(nullptr, 0, PumpThread, nullptr, 0, nullptr);
    if (pumpThread) {
        SetThreadPriority(pumpThread, THREAD_PRIORITY_ABOVE_NORMAL);
        CloseHandle(pumpThread);
        Sleep(Timing::kThreadStartSettleMs);
    }

    ModIpc::Start();

    g_readyEvent =
        CreateEventW(nullptr, TRUE, FALSE, MMOD_MANAGER_READY_EVENT_NAME);
    if (g_readyEvent) {
        SetEvent(g_readyEvent);
        ModLog::Write("module_manager: bootstrap ready");
    } else {
        ModLog::Writef("module_manager: ready event failed err=%lu",
                       GetLastError());
    }

    const auto autoLoadThread =
        CreateThread(nullptr, 0, AutoLoadBootstrapThread, nullptr, 0, nullptr);
    if (autoLoadThread) {
        CloseHandle(autoLoadThread);
    }

    ModLog::Write(
        "module_manager: focus game window to install overlay hooks");

    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH && GetModuleHandle(L"MirrorsEdge.exe")) {
        (void)module;
        DisableThreadLibraryCalls(module);

        const auto thread =
            CreateThread(nullptr, 0, InitWorker, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    }

    if (reason == DLL_PROCESS_DETACH) {
        Hook::ReleaseProcessThreadSuspensions();
        Presentation::ShutdownOnProcessDetach();
    }

    return TRUE;
}
