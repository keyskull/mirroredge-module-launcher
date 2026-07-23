#pragma once

#include <Windows.h>
#include <imm.h>

#pragma comment(lib, "imm32.lib")

inline bool WinInput_IsImeOrRawInput(UINT msgId) {
    if (msgId == WM_INPUT) {
        return true;
    }
    return msgId >= WM_IME_SETCONTEXT && msgId <= WM_IME_KEYUP;
}

inline bool WinInput_IsMouseInput(UINT msgId) {
    switch (msgId) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

inline bool WinInput_IsKeyboardInput(UINT msgId) {
    switch (msgId) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_DEADCHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
    case WM_SYSDEADCHAR:
    case WM_UNICHAR:
        return true;
    default:
        return false;
    }
}

// IME and raw input must never be converted to WM_NULL. Doing so corrupts
// system-wide CJK IME until Text Input Application is restarted.
inline bool WinInput_MustPassThrough(UINT msgId) {
    return WinInput_IsImeOrRawInput(msgId);
}

// When blocking game input for an overlay menu, swallow mouse input messages.
// Callers should also check WinInput_IsKeyboardInput for keyboard messages.
// Keyboard messages must still reach DispatchMessage so IME composition can
// complete or cancel — callers forward keyboard to ImGui first, then let the
// original message loop translate/dispatch it.
inline bool WinInput_ShouldSwallowForBlockInput(UINT msgId) {
    if (WinInput_MustPassThrough(msgId)) {
        return false;
    }
    return WinInput_IsMouseInput(msgId);
}

inline void WinInput_CancelImeComposition(HWND hwnd) {
    if (!hwnd) {
        return;
    }

    const HIMC himc = ImmGetContext(hwnd);
    if (!himc) {
        return;
    }

    ImmNotifyIME(himc, NI_CLOSECANDIDATE, 0, 0);
    ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
    ImmReleaseContext(hwnd, himc);
}

inline void WinInput_ReleaseOverlayCapture(HWND hwnd) {
    (void)hwnd;
    if (const HWND cap = GetCapture()) {
        DWORD pid = 0;
        GetWindowThreadProcessId(cap, &pid);
        if (pid == GetCurrentProcessId()) {
            ReleaseCapture();
        }
    }
    ClipCursor(nullptr);
}

inline void WinInput_DisassociateIme(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    WinInput_CancelImeComposition(hwnd);
    ImmAssociateContext(hwnd, nullptr);
}

inline void WinInput_RestoreSystemCursorVisible() {
    CURSORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info)) {
        return;
    }
    if (!(info.flags & CURSOR_SHOWING)) {
        int visibility = 0;
        do {
            visibility = ShowCursor(TRUE);
        } while (visibility < 0);
    }
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

struct WinInput_EnumCtx {
    DWORD pid = 0;
};

inline BOOL WINAPI WinInput_EnumProcessWindowProc(HWND hwnd, LPARAM lParam) {
    auto *ctx = reinterpret_cast<WinInput_EnumCtx *>(lParam);
    if (!ctx || !hwnd || !IsWindow(hwnd)) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) {
        return TRUE;
    }

    WinInput_DisassociateIme(hwnd);
    if (GetCapture() == hwnd) {
        ReleaseCapture();
    }
    return TRUE;
}

// Process detach / game exit: cancel IME on every HWND owned by this process
// and restore capture/cursor so Text Input Application is not left dirty.
inline void WinInput_ShutdownForProcessDetach(HWND gameHwnd) {
    const WinInput_EnumCtx ctx = {GetCurrentProcessId()};
    EnumWindows(WinInput_EnumProcessWindowProc, reinterpret_cast<LPARAM>(
                    const_cast<WinInput_EnumCtx *>(&ctx)));

    WinInput_DisassociateIme(gameHwnd);

    if (const HWND fg = GetForegroundWindow()) {
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == GetCurrentProcessId()) {
            WinInput_DisassociateIme(fg);
        } else {
            WinInput_CancelImeComposition(fg);
        }
        if (GetCapture() == fg) {
            ReleaseCapture();
        }
    }

    WinInput_ReleaseOverlayCapture(gameHwnd);
    WinInput_RestoreSystemCursorVisible();
}

// Run from the launcher after the game process exits. Nudges shell focus (same
// effect as clicking the taskbar) and clears local capture / IME state.
inline void WinInput_RestoreDesktopInput(HWND preferForegroundHwnd) {
    WinInput_RestoreSystemCursorVisible();
    ClipCursor(nullptr);
    ReleaseCapture();

    if (preferForegroundHwnd && IsWindow(preferForegroundHwnd)) {
        WinInput_DisassociateIme(preferForegroundHwnd);
    }

    const HWND shell = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!shell) {
        return;
    }

    DWORD timeout = 0;
    SystemParametersInfoW(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &timeout, 0);
    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, nullptr,
                          SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);

    AllowSetForegroundWindow(ASFW_ANY);

    DWORD_PTR sendResult = 0;
    SendMessageTimeoutW(shell, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, &sendResult);

    HWND target = preferForegroundHwnd;
    if (!target || !IsWindow(target)) {
        target = shell;
    }
    SetForegroundWindow(shell);
    SetForegroundWindow(target);
    BringWindowToTop(target);

    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0,
                          reinterpret_cast<PVOID>(static_cast<ULONG_PTR>(timeout)),
                          SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);

    if (const HWND fg = GetForegroundWindow()) {
        WinInput_CancelImeComposition(fg);
    }
}
