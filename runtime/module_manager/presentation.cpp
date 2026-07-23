#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

#include <d3d9.h>

#include "presentation_internal.h"
#include "hook.h"
#include "host_api.h"
#include "menu.h"
#include "mod_console.h"
#include "mod_log.h"
#include "presentation.h"
#include "debug_trace.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"
#include "win_input.h"
#include "window_layout_settings.h"

#pragma comment(lib, "d3d9.lib")

namespace {

std::atomic<bool> g_windowLayoutApplyRequested{false};

} // namespace

namespace {

void SafeExecuteTask(const Presentation::MainThreadTask &task) {
    if (!task) {
        return;
    }

    __try {
        task();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        const auto code = GetExceptionCode();
        char buf[128] = {};
        snprintf(buf, sizeof(buf),
                 "presentation: main-thread task crashed (0x%08lX); removed",
                 static_cast<unsigned long>(code));
        ModLog::Write(buf);
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }
}

} // namespace

namespace PresentationInternal {

void PumpTasks() {
    std::vector<Presentation::MainThreadTask> tasks;
    {
        std::lock_guard<std::mutex> lock(g_taskMutex);
        tasks.swap(g_tasks);
    }

    if (!tasks.empty()) {
        char buf[128] = {};
        snprintf(buf, sizeof(buf),
                 "presentation: PumpTasks processing %zu tasks", tasks.size());
        ModLog::Write(buf);
    }

    for (const auto task : tasks) {
        SafeExecuteTask(task);
    }
}

} // namespace PresentationInternal

namespace Presentation {

using namespace PresentationInternal;

void NoteInjectTime() {
    if (g_injectTick == 0) {
        g_injectTick = GetTickCount();
    }
}

bool InstallBootstrap() {
    NoteInjectTime();

    const auto module = GetModuleHandle(nullptr);
    auto hooked = false;

    if (!g_window.PeekMessage) {
		hooked = Hook::ImportHook(module, "user32.dll", "PeekMessageW",
                             reinterpret_cast<void *>(PeekMessageHook),
                             reinterpret_cast<void **>(&g_window.PeekMessage)) ||
            Hook::ImportHook(module, "user32.dll", "PeekMessageA",
                             reinterpret_cast<void *>(PeekMessageHook),
                             reinterpret_cast<void **>(&g_window.PeekMessage)) ||
            Hook::TrampolineHookNoSuspend(
                PeekMessageHook, PeekMessageW,
                reinterpret_cast<void **>(&g_window.PeekMessage));
    } else {
        hooked = true;
    }

    if (!g_window.GetMessage) {
		hooked = Hook::ImportHook(module, "user32.dll", "GetMessageW",
                             reinterpret_cast<void *>(GetMessageHook),
                             reinterpret_cast<void **>(&g_window.GetMessage)) ||
            Hook::ImportHook(module, "user32.dll", "GetMessageA",
                             reinterpret_cast<void *>(GetMessageHook),
                             reinterpret_cast<void **>(&g_window.GetMessage)) ||
            Hook::TrampolineHookNoSuspend(
                GetMessageHook, GetMessageW,
                reinterpret_cast<void **>(&g_window.GetMessage)) ||
            hooked;
    }

    g_bootstrapInstalled = hooked;
    // #region agent log
	DebugTrace::Event("presentation.cpp:InstallBootstrap",
	                  hooked ? "bootstrap_ok" : "bootstrap_fail", "D",
	                  reinterpret_cast<uintptr_t>(g_window.PeekMessage),
        reinterpret_cast<uintptr_t>(g_window.GetMessage), hooked, 0);
    // #endregion
    if (hooked) {
        ModLog::Write("module_manager: message bootstrap installed");
    } else {
        ModLog::Write("module_manager: message bootstrap failed");
    }

    return hooked;
}

bool AreHooksInstalled() { return g_hooksInstalled.load(); }

bool IsOverlayReady() { return g_hooksInstalled.load() && g_imguiReady.load(); }

int GetEndSceneCallCount() { return g_endSceneCalls.load(); }

int GetPresentCallCount() { return g_presentCalls.load(); }

HWND GetGameWindow() { return PresentationInternal::GetGameWindow(); }

HWND GetLayoutTargetWindow() {
    HWND hwnd = PresentationInternal::GetHookWindowHint(
        PresentationInternal::g_cachedDevice);
    if (!hwnd) {
        hwnd = GetGameWindow();
    }
    return hwnd;
}

void OnProxyDeviceCreated(IDirect3DDevice9 *device) {
    if (!device) {
        return;
    }

    g_cachedDevice = device;
    g_proxyDeviceReceived = true;
    g_proxyDeviceTick = GetTickCount();
    if (g_injectTick == 0) {
        g_injectTick = GetTickCount();
    }

    if (WindowLayout_IsEnabled()) {
		HostApi_PresentationTick(device);
    }

    ModLog::Write("module_manager: d3d9 proxy device received");
    // #region agent log
	DebugTrace::Event("presentation.cpp:OnProxyDeviceCreated", "proxy_device", "M",
	                  reinterpret_cast<uintptr_t>(device), 0, 0, 0);
    // #endregion
}

void RequestWindowLayoutApply() { g_windowLayoutApplyRequested = true; }

static void SyncWindowLayoutIfNeededImpl() {
    if (!WindowLayout_IsEnabled()) {
        return;
    }

    // Defer layout work until core.dll is mapped — 1号机 borderless IPC/layout
    // changes run on the message thread; applying chrome before core init wedged
    // 2号机 boot (see test-logs/alerts/ and KI-2026-006).
    if (!GetModuleHandleW(L"core.dll")) {
        return;
    }

    const bool forced = g_windowLayoutApplyRequested.exchange(false);
    static DWORD lastAttemptTick = 0;
    const DWORD now = GetTickCount();
    if (!forced && lastAttemptTick != 0 && now - lastAttemptTick < 500) {
        return;
    }

    HWND hwnd = GetLayoutTargetWindow();
    if (!hwnd) {
        return;
    }

    const auto style = GetWindowLongW(hwnd, GWL_STYLE);
    const bool hasChrome =
        (style & (WS_CAPTION | WS_THICKFRAME | WS_BORDER)) != 0;

    bool needsResize = forced || hasChrome;
    if (!needsResize) {
        RECT monitorRect = {};
        RECT windowRect = {};
        if (WindowLayout_GetMonitorRect(hwnd, monitorRect) &&
            GetWindowRect(hwnd, &windowRect)) {
            int targetW = 0;
            int targetH = 0;
            const float scale = WindowLayout_GetScale();
            if (!WindowLayout_ComputeWindowSize(hwnd, scale, targetW, targetH)) {
                RECT targetRect = {};
                WindowLayout_ComputeTargetRect(monitorRect, scale, targetRect);
                targetW = targetRect.right - targetRect.left;
                targetH = targetRect.bottom - targetRect.top;
            }

            const int windowW = windowRect.right - windowRect.left;
            const int windowH = windowRect.bottom - windowRect.top;
            needsResize =
                abs(windowW - targetW) > 8 || abs(windowH - targetH) > 8;
        }
    }

    if (!needsResize) {
        return;
    }

    lastAttemptTick = now;
    WindowLayout_ApplyToWindow(hwnd);

    const HWND titleHwnd = GetGameWindow();
    if (titleHwnd && titleHwnd != hwnd) {
        WindowLayout_ApplyToWindow(titleHwnd);
    }
}

void SyncWindowLayoutIfNeeded() { SyncWindowLayoutIfNeededImpl(); }

void PumpFromMessageThread() {
    if (g_preModFocusCooldown.load() > 0) {
        g_preModFocusCooldown--;
    }

    UpdateForegroundHeuristic();
    TryInstallHooks();
    PumpTasks();
    SyncInputBlockWithForeground(g_cachedDevice);
    SyncWindowLayoutIfNeededImpl();

    if (g_bootstrapInstalled.load() || g_hooksInstalled.load()) {
        HostMenu::PollToggle();
        ModConsole::PollToggle();
    }
}

void Pump() {
    TryInstallHooks();
    PumpTasks();
}

void QueueMainThreadTask(MainThreadTask task) {
    if (!task) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_taskMutex);
    g_tasks.push_back(task);
    char buf[128] = {};
    snprintf(buf, sizeof(buf),
             "presentation: QueueMainThreadTask queued (total=%zu)", g_tasks.size());
    ModLog::Write(buf);
}

void RenderOverlay(IDirect3DDevice9 *device) {
    if (!device) {
        return;
    }

    if (FAILED(device->TestCooperativeLevel())) {
        return;
    }

    static thread_local bool rendering = false;
    if (rendering) {
        return;
    }
    rendering = true;

    EnsureImGui(device);
    if (!g_imguiReady) {
        rendering = false;
        return;
    }

    if (g_pendingImGuiReset.exchange(false)) {
        ApplyImGuiInputReset();
        ClearPendingImGuiMessages();
    }

    SyncImGuiDisplaySize();

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    PollImGuiMouseButtons();
    PollImGuiKeyboardInput();
    ImGui::NewFrame();

    HostMenu::Render(device);
    ModConsole::Render(device);
    HostApi_RenderScene(device);

    if (!HostMenu::IsOpen() && !ModConsole::IsOpen()) {
        static std::atomic<int> hintHeartbeat{0};
        const auto hb = ++hintHeartbeat;
        if (hb % 120 == 0) {
            // #region agent log
			DebugTrace::Event("presentation.cpp:RenderOverlay", "status_hint", "H-HINT",
			                  hb, g_imguiReady.load() ? 1u : 0u, g_stableFrames.load(),
			                  0);
            // #endregion
        }
    }

    ImGui::Render();
    if (const auto drawData = ImGui::GetDrawData()) {
        if (drawData->DisplaySize.x > 0.0f && drawData->DisplaySize.y > 0.0f) {
            if (!RenderImGuiDrawDataSafe(drawData)) {
                InvalidateImGuiDeviceObjects();
            } else if (g_endSceneCalls.load() <= 5) {
                // #region agent log
                DebugTrace::Event("presentation.cpp:RenderOverlay", "draw", "L",
                                  reinterpret_cast<uintptr_t>(device),
                                  static_cast<uintptr_t>(drawData->CmdListsCount),
				                  drawData->TotalVtxCount, HostMenu::IsOpen() ? 1 : 0);
                // #endregion
            }
        }
    }

    rendering = false;
}

void ShutdownOnProcessDetach() {
    static std::atomic<bool> done{false};
    if (done.exchange(true)) {
        return;
    }

    g_window.blockInput = false;
    HostMenu::Hide();
    ModConsole::Hide();
    ClearPendingImGuiMessages();
    if (g_imguiReady) {
        ApplyImGuiInputReset();
    }
    WinInput_ShutdownForProcessDetach(GetGameWindow());
}

} // namespace Presentation
