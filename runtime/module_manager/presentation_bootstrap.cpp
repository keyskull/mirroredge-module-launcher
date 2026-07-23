#include "presentation_internal.h"

#include "debug_trace.h"
#include "host_api.h"
#include "menu.h"
#include "mod_console.h"
#include "win_input.h"

namespace PresentationInternal {
void PumpPreHookBootstrap() {
    if (g_hooksInstalled.load()) {
        return;
    }

    static DWORD lastPumpTick = 0;
    const DWORD now = GetTickCount();
    if (lastPumpTick != 0 && now - lastPumpTick < 8) {
        return;
    }
    lastPumpTick = now;

    if (g_preModFocusCooldown.load() > 0) {
        g_preModFocusCooldown--;
    }

    UpdateForegroundHeuristic();
    TryInstallHooks();
    PumpTasks();
}

BOOL WINAPI PeekMessageHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                            UINT wMsgFilterMax, UINT wRemoveMsg) {
    static thread_local int depth = 0;
    ++depth;
    // #region agent log
    if (depth > 1) {
        DebugTrace::Event("presentation.cpp:PeekMessageHook", "reenter", "H-F2",
                          static_cast<uintptr_t>(depth), 0, 0, 0);
    }
    // #endregion

    static std::atomic<int> calls{0};
    const auto n = ++calls;
    if (n <= 3) {
        // #region agent log
        DebugTrace::Event("presentation.cpp:PeekMessageHook", "hook_enter", "D",
                          n, reinterpret_cast<uintptr_t>(g_window.PeekMessage), 0,
                          0);
        // #endregion
    }

    if (!g_window.PeekMessage) {
        const auto fallback = PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax,
                                           wRemoveMsg);
        --depth;
        return fallback;
    }

    HostApi_PresentationInputSync();

    const auto ret = g_window.PeekMessage(lpMsg, hWnd, wMsgFilterMin,
                                          wMsgFilterMax, wRemoveMsg);
    if (n <= 3) {
        // #region agent log
        DebugTrace::Event("presentation.cpp:PeekMessageHook", "after_peek", "D",
                          n, ret, 0, 0);
        // #endregion
    }

    if (lpMsg && ret && (wRemoveMsg & PM_REMOVE)) {
        HandleFocus(lpMsg);
        HandleInput(lpMsg);
        PollUnfocusInputRelease();
    }

    PumpPreHookBootstrap();

    --depth;
    return ret;
}

BOOL WINAPI GetMessageHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                           UINT wMsgFilterMax) {
    static std::atomic<int> calls{0};
    const auto n = ++calls;
    if (n <= 3) {
        // #region agent log
        DebugTrace::Event("presentation.cpp:GetMessageHook", "hook_enter", "D",
                          n, reinterpret_cast<uintptr_t>(g_window.GetMessage), 0,
                          0);
        // #endregion
    }

    if (!g_window.GetMessage) {
        return GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    }

    HostApi_PresentationInputSync();

    const auto ret =
        g_window.GetMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    if (ret > 0 && lpMsg) {
        HandleFocus(lpMsg);
        HandleInput(lpMsg);
        PollUnfocusInputRelease();
        // #region agent log
        if (lpMsg->message == WM_ACTIVATEAPP) {
            DebugTrace::Event("presentation.cpp:GetMessageHook", "after_focus",
                              "H-F78", lpMsg->wParam ? 1u : 0u,
                              HostMenu::IsOpen() ? 1u : 0u, 0, 0);
        }
        // #endregion
    }

    PumpPreHookBootstrap();

    return ret;
}
} // namespace PresentationInternal

