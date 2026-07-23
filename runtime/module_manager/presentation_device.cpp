#include "presentation_internal.h"

#include <Psapi.h>

#include <algorithm>
#include <cstring>

#include "debug_trace.h"
#include "host_api.h"
#include "hook.h"
#include "me_sdk/runtime/pattern.h"
#include "menu.h"
#include "mod_console.h"
#include "mod_log.h"
#include "presentation.h"
#include "timing_constants.h"
#include "window_layout_settings.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"

#pragma comment(lib, "Psapi.lib")

namespace PresentationInternal {
bool IsAddressInModule(void *address) {
    if (!address) {
        return false;
    }

    HMODULE module = nullptr;
    return GetModuleHandleExA(
               GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
               reinterpret_cast<LPCSTR>(address), &module) &&
           module != nullptr;
}

bool HookVTableEntry(void **vtable, unsigned index, void *hook,
                     void **original) {
    if (!vtable || !vtable[index] || !hook) {
        return false;
    }

    if (original) {
        *original = vtable[index];
    }

    DWORD protect = 0;
    if (!VirtualProtect(&vtable[index], sizeof(void *), PAGE_READWRITE,
                        &protect)) {
        return false;
    }

    vtable[index] = hook;
    {
        DWORD unused = 0;
        VirtualProtect(&vtable[index], sizeof(void *), protect, &unused);
    }
    return true;
}

bool IsPlausibleDeviceVtable(void **vtable) {
    if (!vtable || !vtable[kPresent] || !vtable[kEndScene]) {
        return false;
    }

    return IsAddressInModule(vtable[kPresent]) &&
           IsAddressInModule(vtable[kEndScene]);
}

void **GetDeviceVtable(IDirect3DDevice9 *device) {
    if (!device) {
        return nullptr;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(device, &mbi, sizeof(mbi)) == 0 ||
        !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_WRITECOPY))) {
        return nullptr;
    }

    const auto vtable = *reinterpret_cast<void ***>(device);
    if (!vtable) {
        return nullptr;
    }

    if (VirtualQuery(vtable, &mbi, sizeof(mbi)) == 0 ||
        !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_WRITECOPY))) {
        return nullptr;
    }

    return IsPlausibleDeviceVtable(vtable) ? vtable : nullptr;
}

bool IsValidD3D9Device(IDirect3DDevice9 *device) {
    if (!device || reinterpret_cast<uintptr_t>(device) < 0x10000) {
        return false;
    }

    if (!GetDeviceVtable(device)) {
        return false;
    }

    D3DDEVICE_CREATION_PARAMETERS params = {};
    return SUCCEEDED(device->GetCreationParameters(&params)) &&
           params.hFocusWindow != nullptr;
}

bool IsValidD3D9DeviceSafe(IDirect3DDevice9 *device) {
    __try {
        return IsValidD3D9Device(device);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct DeviceScanStats {
    int patternMatches = 0;
    int candidates = 0;
    int rejectReason = 0;
    int modulesScanned = 0;
};

bool SafeReadPointer(const void *address, void **out) {
    __try {
        *out = *reinterpret_cast<void *const *>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

HWND FindGameWindow();

bool IsGameProxyD3D9Active() {
    const auto mod = GetModuleHandleW(L"d3d9.dll");
    if (!mod) {
        return false;
    }

    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(mod, path, MAX_PATH)) {
        return false;
    }

    wchar_t systemPath[MAX_PATH] = {};
    if (!GetSystemDirectoryW(systemPath, MAX_PATH)) {
        return false;
    }
    wcscat(systemPath, L"\\d3d9.dll");
    return _wcsicmp(path, systemPath) != 0;
}

IDirect3DDevice9 *FindDeviceInExe(DeviceScanStats *stats) {
    const auto exe = GetModuleHandle(nullptr);
    MODULEINFO info = {};
    if (!exe || !GetModuleInformation(GetCurrentProcess(), exe, &info,
                                      sizeof(info))) {
        return nullptr;
    }

    if (stats) {
        stats->modulesScanned = 1;
    }

    static const struct {
        const char *bytes;
        const char *mask;
    } patterns[] = {
        {"\x8B\x0D\x00\x00\x00\x00\x85\xC9\x74", "xx????xxx"},
        {"\x8B\x15\x00\x00\x00\x00\x85\xD2\x74", "xx????xxx"},
        {"\xA1\x00\x00\x00\x00\x85\xC0\x74", "x????xxx"},
    };

    for (const auto &pattern : patterns) {
        auto cursor = reinterpret_cast<BYTE *>(exe);
        auto remaining = static_cast<int>(info.SizeOfImage);

        while (remaining > 9) {
            const auto match = Pattern::FindPattern(
                cursor, remaining, pattern.bytes, pattern.mask);
            if (!match) {
                break;
            }

            if (stats) {
                stats->patternMatches++;
            }

            const auto insn = reinterpret_cast<BYTE *>(match);
            const auto immOffset = pattern.bytes[0] == '\xA1' ? 1 : 2;
            if (remaining <= immOffset + static_cast<int>(sizeof(uintptr_t))) {
                const auto next = reinterpret_cast<BYTE *>(match) + 1;
                remaining -= static_cast<int>(next - cursor);
                cursor = next;
                continue;
            }

            uintptr_t globalAddr = 0;
            memcpy(&globalAddr, insn + immOffset, sizeof(globalAddr));
            auto global =
                reinterpret_cast<IDirect3DDevice9 **>(globalAddr);

            MEMORY_BASIC_INFORMATION mbi = {};
            if (!global || VirtualQuery(global, &mbi, sizeof(mbi)) == 0 ||
                !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE |
                                 PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
                const auto next = reinterpret_cast<BYTE *>(match) + 1;
                remaining -= static_cast<int>(next - cursor);
                cursor = next;
                continue;
            }

            IDirect3DDevice9 *device = nullptr;
            if (!SafeReadPointer(global, reinterpret_cast<void **>(&device))) {
                const auto next = reinterpret_cast<BYTE *>(match) + 1;
                remaining -= static_cast<int>(next - cursor);
                cursor = next;
                continue;
            }

            if (stats) {
                stats->candidates++;
            }

            if (!device) {
                if (stats && stats->rejectReason == 0) {
                    stats->rejectReason = 1;
                }
            } else if (IsValidD3D9DeviceSafe(device)) {
                return device;
            } else if (stats && stats->rejectReason == 0) {
                stats->rejectReason = 2;
            }

            const auto next = reinterpret_cast<BYTE *>(match) + 1;
            remaining -= static_cast<int>(next - cursor);
            cursor = next;
        }
    }

    return nullptr;
}

IDirect3DDevice9 *FindDevice(DeviceScanStats *stats) {
    if (g_cachedDevice && g_proxyDeviceReceived.load()) {
        return g_cachedDevice;
    }

    if (g_cachedDevice && IsValidD3D9DeviceSafe(g_cachedDevice)) {
        return g_cachedDevice;
    }

    g_cachedDevice = nullptr;
    return FindDeviceInExe(stats);
}

// Historical wrapper kept for API compatibility. Identical to FindDevice() —
// the "Safe" suffix is misleading (no SEH guard, no extra validation).
IDirect3DDevice9 *FindDeviceSafe(DeviceScanStats *stats) {
    return FindDevice(stats);
}

struct GameWindowSearch {
    DWORD processId = 0;
    HWND best = nullptr;
    HWND fallback = nullptr;
};

BOOL CALLBACK EnumGameWindowsProc(HWND hwnd, LPARAM param) {
    auto *search = reinterpret_cast<GameWindowSearch *>(param);

    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != search->processId || !IsWindowVisible(hwnd)) {
        return TRUE;
    }

    if (!(GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE)) {
        return TRUE;
    }

    wchar_t title[256] = {};
    if (GetWindowTextW(hwnd, title, 255) > 0) {
        if (wcsstr(title, L"Mirror") != nullptr) {
            search->best = hwnd;
            return FALSE;
        }

        if (!search->fallback) {
            search->fallback = hwnd;
        }
    }

    return TRUE;
}

HWND FindGameWindow() {
    if (g_window.hwnd && IsWindow(g_window.hwnd)) {
        return g_window.hwnd;
    }

    GameWindowSearch search = {GetCurrentProcessId(), nullptr, nullptr};
    EnumWindows(EnumGameWindowsProc, reinterpret_cast<LPARAM>(&search));

    const auto found = search.best ? search.best : search.fallback;
    if (found) {
        g_window.hwnd = found;
    }

    return found;
}

bool IsGameWindowFocused(HWND hwnd) {
    if (!hwnd) {
        return false;
    }

    const auto fg = GetForegroundWindow();
    if (!fg) {
        return false;
    }

    if (fg == hwnd) {
        return true;
    }

    if (IsChild(hwnd, fg)) {
        return true;
    }

    const auto root = GetAncestor(fg, GA_ROOT);
    return root == hwnd || (root && IsChild(hwnd, root));
}

HWND GetHookWindowHint(IDirect3DDevice9 *device) {
    if (g_window.hwnd && IsWindow(g_window.hwnd)) {
        return g_window.hwnd;
    }

    if (device) {
        D3DDEVICE_CREATION_PARAMETERS params = {};
        if (SUCCEEDED(device->GetCreationParameters(&params)) &&
            params.hFocusWindow) {
            return g_window.hwnd = params.hFocusWindow;
        }
    }

    return FindGameWindow();
}

bool IsOurProcessForeground() {
    const auto foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    return pid == GetCurrentProcessId();
}

void UpdateForegroundHeuristic() {
    if (IsOurProcessForeground()) {
        g_gameForegroundSinceInject = true;
        FindGameWindow();
    }
}

HWND GetGameWindow() {
    if (g_window.hwnd && IsWindow(g_window.hwnd)) {
        return g_window.hwnd;
    }

    if (const auto found = FindGameWindow()) {
        return found;
    }

    return nullptr;
}

bool IsDeviceCooperativeReady(IDirect3DDevice9 *device) {
    if (!device) {
        return false;
    }

    return SUCCEEDED(device->TestCooperativeLevel());
}

void NotifyFocusTransition(IDirect3DDevice9 *device) {
    g_stableFrames = 0;
    g_postFocusWatch = 120;
    if (g_hooksInstalled.load()) {
        if (HostApi_WantsOverlay() && IsDeviceCooperativeReady(device)) {
            g_focusCooldown = 18;
        } else {
            g_focusCooldown = 0;
        }
    } else {
        g_preModFocusCooldown = 8;
    }
    // #region agent log
    DebugTrace::Event("presentation.cpp:NotifyFocusTransition", "focus_transition",
                      "H-F1", g_window.blockInput ? 1u : 0u,
                      g_focusCooldown.load(), g_preModFocusCooldown.load(),
                      HostMenu::IsOpen() ? 1 : 0);
    // #endregion
}

void UpdateStability(IDirect3DDevice9 *device) {
    if (!device) {
        g_stableFrames = 0;
        return;
    }

    const bool inFocusCooldown = g_focusCooldown.load() > 0;
    if (inFocusCooldown) {
        g_focusCooldown--;
    }

    const HRESULT level = device->TestCooperativeLevel();
    if (level == D3DERR_DEVICELOST) {
        g_stableFrames = 0;
        if (g_imguiReady && lastCoop != D3DERR_DEVICELOST) {
            InvalidateImGuiDeviceObjectsNow("imgui_lost_invalidate", "H-F82");
        }
        lastCoop = level;
        return;
    }

    if (level == D3DERR_DEVICENOTRESET) {
        g_stableFrames = 0;
        if (g_imguiReady) {
            InvalidateImGuiDeviceObjectsNow("imgui_pre_reset", "H-F81");
        }
        lastCoop = level;
        return;
    }

    if (lastCoop == D3DERR_DEVICENOTRESET && level == D3D_OK && g_imguiReady) {
        ImGui_ImplDX9_CreateDeviceObjects();
        g_stableFrames = 0;
        // #region agent log
        DebugTrace::Event("presentation.cpp:UpdateStability", "imgui_device_reset",
                          "H-F29", static_cast<uintptr_t>(level),
                          static_cast<uintptr_t>(lastCoop), 0, 0);
        // #endregion
    }

    lastCoop = level;
    if (FAILED(level)) {
        g_stableFrames = 0;
        return;
    }

    if (inFocusCooldown) {
        g_stableFrames = 0;
        return;
    }

    g_stableFrames++;
}

bool CanRenderOverlay(IDirect3DDevice9 *device) {
    if (!device || FAILED(device->TestCooperativeLevel())) {
        return false;
    }

    if (g_focusCooldown.load() > 0 && HostApi_WantsOverlay()) {
        return false;
    }

    return g_stableFrames.load() >= kStableFramesRequired;
}

bool WantsActiveOverlayDraw() {
    return HostMenu::IsOpen() || ModConsole::IsOpen() || HostApi_WantsOverlay();
}

bool HookDevicePresentationInternal(IDirect3DDevice9 *device) {
    if (!device) {
        return false;
    }

    if (g_hooksInstalled.load()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(g_presentationHookMutex);
    if (g_hooksInstalled.load()) {
        return true;
    }

    const auto vtable = GetDeviceVtable(device);
    if (!vtable || !vtable[kEndScene] || !vtable[kPresent]) {
        return false;
    }

    g_render.EndSceneOriginal =
        reinterpret_cast<decltype(g_render.EndSceneOriginal)>(
            vtable[kEndScene]);
    g_render.PresentOriginal =
        reinterpret_cast<decltype(g_render.PresentOriginal)>(
            vtable[kPresent]);

    // #region agent log
    DebugTrace::Event(
        "presentation.cpp:HookDevicePresentationInternal", "pre_vtable_patch",
        "A", reinterpret_cast<uintptr_t>(device),
        reinterpret_cast<uintptr_t>(vtable),
        reinterpret_cast<uintptr_t>(g_render.EndSceneOriginal), 0);
    // #endregion

    if (!HookVTableEntry(vtable, kEndScene,
                         reinterpret_cast<void *>(ModuleManager_EndScene),
                         nullptr)) {
        g_render.EndSceneOriginal = nullptr;
        g_render.PresentOriginal = nullptr;
        return false;
    }

    if (!HookVTableEntry(vtable, kPresent,
                         reinterpret_cast<void *>(ModuleManager_Present),
                         nullptr)) {
        DWORD rollbackProtect = 0;
        if (VirtualProtect(&vtable[kEndScene], sizeof(void *), PAGE_READWRITE,
                           &rollbackProtect)) {
            vtable[kEndScene] =
                reinterpret_cast<void *>(g_render.EndSceneOriginal);
            DWORD unused = 0;
            VirtualProtect(&vtable[kEndScene], sizeof(void *), rollbackProtect,
                           &unused);
        }

        g_render.EndSceneOriginal = nullptr;
        g_render.PresentOriginal = nullptr;
        return false;
    }

    g_hooksInstalled = true;
    g_cachedDevice = device;
    // #region agent log
    DebugTrace::Event(
        "presentation.cpp:HookDevicePresentationInternal", "hooks_installed",
        "B", reinterpret_cast<uintptr_t>(device),
        reinterpret_cast<uintptr_t>(g_render.EndSceneOriginal),
        reinterpret_cast<uintptr_t>(g_render.PresentOriginal), 0);
    // #endregion
    ModLog::Write("module_manager: presentation hooks installed");
    return true;
}

bool HookDevicePresentation(IDirect3DDevice9 *device) {
    return device && HookDevicePresentationInternal(device);
}

bool TryLazyInstallHooks() {
    if (g_hooksInstalled.load()) {
        return true;
    }

    if (g_injectTick == 0) {
        g_injectTick = GetTickCount();
    }

    const auto now = GetTickCount();
    const auto elapsed = now - g_injectTick;
    if (!g_proxyDeviceReceived.load()) {
        const DWORD lazyDelayMs = WindowLayout_IsEnabled() ? kLazyHookDelayBorderlessMs
                                                           : kLazyHookDelayMs;
        if (elapsed < lazyDelayMs) {
            return false;
        }
    } else {
        const DWORD deviceTick = g_proxyDeviceTick.load();
        const DWORD proxyElapsed = deviceTick != 0 ? now - deviceTick : 0;
        if (proxyElapsed < kProxyHookSettleMs) {
            return false;
        }
    }

    UpdateForegroundHeuristic();

    if (!g_lazyGateLogged.exchange(true)) {
        const auto gameWnd = FindGameWindow();
        // #region agent log
        DebugTrace::Event("presentation.cpp:TryLazyInstallHooks", "lazy_gate", "J",
                          g_gameForegroundSinceInject.load(),
                          reinterpret_cast<uintptr_t>(gameWnd),
                          IsOurProcessForeground() ? 1u : 0u,
                          static_cast<int>(elapsed));
        // #endregion
    }

    const DWORD retryMs = g_proxyDeviceReceived.load() ? kHookRetryIntervalProxyMs
                                                       : kHookRetryIntervalMs;
    if (now - g_lastHookAttemptTick < retryMs) {
        return false;
    }
    g_lastHookAttemptTick = now;

    DeviceScanStats stats = {};
    IDirect3DDevice9 *device = nullptr;
    if (g_proxyDeviceReceived.load() && g_cachedDevice) {
        device = g_cachedDevice;
        // #region agent log
        DebugTrace::Event("presentation.cpp:TryLazyInstallHooks", "proxy_device",
                          "M", reinterpret_cast<uintptr_t>(device), 0, 0, 0);
        // #endregion
    } else {
        if (IsGameProxyD3D9Active()) {
            if (!g_deviceMissLogged.exchange(true)) {
                ModLog::Write(
                    "module_manager: waiting for d3d9 proxy device (close game "
                    "and start via Module Launcher if this persists)");
                // #region agent log
                DebugTrace::Event("presentation.cpp:TryLazyInstallHooks",
                                  "wait_proxy_device", "N", 0, 0, 0, 0);
                // #endregion
            }
            return false;
        }

        // #region agent log
        DebugTrace::Event("presentation.cpp:TryLazyInstallHooks",
                          "before_device_scan", "A", 0, 0, 0, 0);
        // #endregion
        device = FindDeviceSafe(&stats);
        if (!device) {
            // #region agent log
            DebugTrace::Event("presentation.cpp:TryLazyInstallHooks",
                              "device_scan", "G",
                              static_cast<uintptr_t>(stats.patternMatches),
                              static_cast<uintptr_t>(stats.candidates),
                              static_cast<uintptr_t>(stats.rejectReason), 0);
            // #endregion
            if (!g_deviceMissLogged.exchange(true)) {
                ModLog::Write(
                    "module_manager: d3d9 device not found yet (focus game, wait "
                    "for main menu)");
                // #region agent log
                DebugTrace::Event("presentation.cpp:TryLazyInstallHooks",
                                  "device_not_found", "G", 0, 0, 0, 0);
                // #endregion
            }
            return false;
        }
        g_cachedDevice = device;
    }

    const auto hookWnd = GetHookWindowHint(device);
    const auto focused =
        IsGameWindowFocused(hookWnd) || IsOurProcessForeground();
    const bool proxyHookPath =
        g_proxyDeviceReceived.load() && device == g_cachedDevice;
    // #region agent log
    DebugTrace::Event("presentation.cpp:TryLazyInstallHooks", "hook_gate", "K",
                      reinterpret_cast<uintptr_t>(device),
                      reinterpret_cast<uintptr_t>(hookWnd),
                      reinterpret_cast<uintptr_t>(GetForegroundWindow()),
                      focused ? 1 : (proxyHookPath ? 2 : 0));
    // #endregion

    if (g_preModFocusCooldown.load() > 0 && !proxyHookPath) {
        return false;
    }

    if (!focused && !proxyHookPath) {
        return false;
    }

    // #region agent log
    DebugTrace::Event("presentation.cpp:TryLazyInstallHooks", "attempt_hook",
                      "A", reinterpret_cast<uintptr_t>(device),
                      reinterpret_cast<uintptr_t>(hookWnd), 0, 0);
    // #endregion

    return HookDevicePresentation(device);
}

bool TryInstallHooks() { return TryLazyInstallHooks(); }
HRESULT WINAPI ModuleManager_EndScene(IDirect3DDevice9 *device) {
    const auto call = ++g_endSceneCalls;
    if (call <= 5) {
        // #region agent log
        DebugTrace::Event("presentation.cpp:EndScene", "pre_coop", "C",
                          reinterpret_cast<uintptr_t>(device), call, 0, 0);
        // #endregion
    }

    if (!g_render.EndSceneOriginal) {
        return D3D_OK;
    }

    if (device) {
        const HRESULT level = device->TestCooperativeLevel();
        if (level == D3DERR_DEVICELOST || level == D3DERR_DEVICENOTRESET) {
            ProcessLostFrameSideEffects(device);
            const HRESULT hr = g_render.EndSceneOriginal(device);
            // #region agent log
            static std::atomic<int> lostBypassLog{0};
            if (++lostBypassLog <= 5 || lostBypassLog % 30 == 0) {
                DebugTrace::Event("presentation.cpp:EndScene", "lost_bypass",
                                  "H-F76", static_cast<uintptr_t>(level),
                                  static_cast<uintptr_t>(hr),
                                  g_pendingFocusSync.load() ? 1u : 0u, call);
            }
            // #endregion
            return hr;
        }
    }

    if (g_hooksInstalled.load()) {
        // #region agent log
        if (lastCoop == D3DERR_DEVICELOST ||
            lastCoop == D3DERR_DEVICENOTRESET) {
            DebugTrace::Event("presentation.cpp:EndScene", "device_recovered",
                              "H-F80",
                              device ? static_cast<uintptr_t>(
                                           device->TestCooperativeLevel())
                                       : 0u,
                              static_cast<uintptr_t>(lastCoop),
                              g_pendingFocusSync.load() ? 1u : 0u, call);
        }
        // #endregion

        Presentation::PumpFromMessageThread();

        // Drain engine spawn queue on the main thread.
        // Only runs once per second to avoid per-frame overhead during
        // level transitions where UObject state may be unstable.
        {
            static DWORD lastDrain = 0;
            const DWORD now = GetTickCount();
            if (now - lastDrain >= Timing::kSpawnDrainIntervalMs) {
                lastDrain = now;
                static HMODULE eng = nullptr;
                static void(__cdecl *drainFn)() = nullptr;
                if (!eng) eng = GetModuleHandleW(L"engine.dll");
                if (eng && !drainFn)
                    drainFn = (void(__cdecl *)())GetProcAddress(eng, "MMOD_EngineDrainSpawnQueue");
                if (drainFn) drainFn();
            }
        }

        ProcessImGuiRenderThreadEvents(device);

        if (g_pendingMenuHide.exchange(false)) {
            if (HostMenu::IsOpen()) {
                HostMenu::Hide();
            }
        }

        if (g_pendingFocusSync.exchange(false)) {
            NotifyFocusTransition(device);
        }
        // #region agent log
        if (g_postFocusWatch.load() >= 118) {
            DebugTrace::Event("presentation.cpp:EndScene", "post_sync", "H-F74",
                              g_window.blockInput ? 1u : 0u,
                              HostMenu::IsOpen() ? 1u : 0u,
                              device ? static_cast<uintptr_t>(
                                           device->TestCooperativeLevel())
                                       : 0u,
                              0);
        }
        // #endregion
        const auto watch = g_postFocusWatch.load();
        if (watch > 0) {
            g_postFocusWatch = watch - 1;
            if (call % 10 == 0) {
                // #region agent log
                DebugTrace::Event("presentation.cpp:EndScene", "post_focus_tick",
                                  "H-F29", IsOurProcessForeground() ? 1u : 0u,
                                  g_window.blockInput ? 1u : 0u,
                                  device ? static_cast<uintptr_t>(
                                               device->TestCooperativeLevel())
                                           : 0u,
                                  watch);
                // #endregion
            }
        } else if (HostMenu::IsOpen()) {
            static std::atomic<int> heartbeat{0};
            const auto hb = ++heartbeat;
            if (hb % Timing::kTargetFps == 0) {
                // #region agent log
                DebugTrace::Event("presentation.cpp:EndScene", "heartbeat", "H-F13",
                                  IsOurProcessForeground() ? 1u : 0u,
                                  g_window.blockInput ? 1u : 0u,
                                  g_focusCooldown.load(), hb);
                // #endregion
            }
        }
    }

    UpdateStability(device);

    if (g_hooksInstalled.load() && device &&
        SUCCEEDED(device->TestCooperativeLevel()) && !g_imguiReady.load() &&
        g_stableFrames.load() >= kImGuiInitFramesRequired &&
        !(g_focusCooldown.load() > 0 && HostApi_WantsOverlay())) {
        EnsureImGui(device);
    }

    const bool render = g_hooksInstalled.load() && device &&
                        CanRenderOverlay(device) && WantsActiveOverlayDraw();
    if (call <= 5) {
        // #region agent log
        DebugTrace::Event("presentation.cpp:EndScene", "hook_enter", "C",
                          reinterpret_cast<uintptr_t>(device), call, render,
                          g_stableFrames.load());
        // #endregion
    }

    if (render) {
        Presentation::RenderOverlay(device);
    }

    return g_render.EndSceneOriginal(device);
}

HRESULT WINAPI ModuleManager_Present(IDirect3DDevice9 *device,
                                     const RECT *sourceRect,
                                     const RECT *destRect,
                                     HWND destWindowOverride,
                                     const RGNDATA *dirtyRegion) {
    const auto call = ++g_presentCalls;
    if (call <= 5) {
        // #region agent log
        DebugTrace::Event("presentation.cpp:Present", "pre_update", "C",
                          reinterpret_cast<uintptr_t>(device), call, 0,
                          g_stableFrames.load());
        // #endregion
    }

    UpdateStability(device);

    if (!g_render.PresentOriginal) {
        return D3D_OK;
    }

    if (call <= 5) {
        // #region agent log
        DebugTrace::Event("presentation.cpp:Present", "hook_enter", "C",
                          reinterpret_cast<uintptr_t>(device), call, 0,
                          g_stableFrames.load());
        // #endregion
    }

    const auto presentHr = g_render.PresentOriginal
               ? g_render.PresentOriginal(device, sourceRect, destRect,
                                          destWindowOverride, dirtyRegion)
               : D3D_OK;

    if (g_hooksInstalled.load() && device && SUCCEEDED(presentHr)) {
        HostApi_PresentationTick(device);
    }

    return presentHr;
}
} // namespace PresentationInternal
