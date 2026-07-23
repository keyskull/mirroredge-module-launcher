#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <vector>

#include "engine_presentation_internal.h"
#include "engine_internal.h"
#include "engine_core_bridge.h"
#include "hook.h"
#include "plugin_seh_guard.h"
#include "win_input.h"

namespace EnginePresentationInternal {

namespace {

struct InputCallbackContext {
    InputCallback callback = nullptr;
    unsigned int *message = nullptr;
    int keycode = 0;
};

void InvokeInputCallback(void *data) {
    auto *ctx = static_cast<InputCallbackContext *>(data);
    ctx->callback(*ctx->message, ctx->keycode);
}

void LogInputCallbackFault(const char *context, DWORD exceptionCode) {
    char message[160] = {};
    snprintf(message, sizeof(message),
             "engine: plugin input callback crashed in %s (0x%08lX); callback removed",
             context ? context : "input",
             static_cast<unsigned long>(exceptionCode));
    EngineCoreBridge::Log(message);
}

void DispatchInputCallbacks(std::vector<InputCallback> &callbacks, UINT &msg,
                            WPARAM wParam, const char *context) {
    std::vector<InputCallback> crashedCallbacks;
    for (const auto callback : callbacks) {
        InputCallbackContext ctx = {callback, &msg, static_cast<int>(wParam)};
        DWORD exceptionCode = 0;
        if (!PluginSehGuard::InvokeVoid(
                "plugin_input", "engine_hooks_bootstrap.cpp:HandleMessage",
                InvokeInputCallback, &ctx, &exceptionCode)) {
            crashedCallbacks.push_back(callback);
            LogInputCallbackFault(context, exceptionCode);
        }
    }

    for (const auto callback : crashedCallbacks) {
        callbacks.erase(std::remove(callbacks.begin(), callbacks.end(), callback),
                        callbacks.end());
    }
}

} // namespace

void HandleMessage(HWND hWnd, UINT &msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam < sizeof(window.KeysDown)) {
            const auto k = &window.KeysDown[wParam];
            if (!*k) {
                const auto block = window.BlockInput;

                DispatchInputCallbacks(window.SuperInputCallbacks, msg, wParam,
                                       "super_keydown");

                if (!block) {
                    DispatchInputCallbacks(window.InputCallbacks, msg, wParam,
                                           "keydown");
                }

                *k = 1;
            }
        }

        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam < sizeof(window.KeysDown)) {
            const auto k = &window.KeysDown[wParam];
            if (*k) {
                const auto block = window.BlockInput;

                DispatchInputCallbacks(window.SuperInputCallbacks, msg, wParam,
                                       "super_keyup");

                if (!block) {
                    DispatchInputCallbacks(window.InputCallbacks, msg, wParam,
                                           "keyup");
                }

                *k = 0;
            }
        }

        break;
    }
}

LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam,
                             LPARAM lParam) {

    if (window.BlockInput) {
        HandleMessage(hWnd, msg, wParam, lParam);
        return 0;
    }

    HandleMessage(hWnd, msg, wParam, lParam);

    if (window.WndProc) {
        return CallWindowProc(window.WndProc, hWnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

namespace {
void SafeExecuteEngineTask(void (*task)()) {
    if (!task) {
        return;
    }

    __try {
        task();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        const auto code = GetExceptionCode();
        char buf[128] = {};
        snprintf(buf, sizeof(buf),
                 "engine: main-thread task crashed (0x%08lX); removed",
                 static_cast<unsigned long>(code));
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }
}
} // namespace

void PumpMainThreadTasks() {
    if (focusCooldownFrames.load() > 0) {
        return;
    }

    static thread_local bool pumping = false;
    if (pumping) {
        return;
    }

    pumping = true;

    std::vector<void (*)()> tasks;
    {
        std::lock_guard<std::mutex> lock(mainThreadTaskMutex);
        tasks.swap(mainThreadTasks);
    }

    for (const auto task : tasks) {
        SafeExecuteEngineTask(task);
    }

    pumping = false;
}

void HandleFocusMessage(LPMSG lpMsg) {
    if (!lpMsg) {
        return;
    }

    switch (lpMsg->message) {
    case WM_ACTIVATEAPP:
        if (lpMsg->wParam) {
            gameForegroundSinceInject = true;
        } else {
            Engine::BlockInput(false);
            WinInput_CancelImeComposition(window.Window);
        }
        NotifyFocusTransition();
        break;
    case WM_ACTIVATE:
        if (LOWORD(lpMsg->wParam) != WA_INACTIVE) {
            gameForegroundSinceInject = true;
        } else {
            Engine::BlockInput(false);
            WinInput_CancelImeComposition(window.Window);
        }
        NotifyFocusTransition();
        break;
    case WM_SETFOCUS:
        gameForegroundSinceInject = true;
        NotifyFocusTransition();
        break;
    case WM_KILLFOCUS:
        Engine::BlockInput(false);
        WinInput_CancelImeComposition(window.Window);
        NotifyFocusTransition();
        break;
    case WM_SIZE:
    case WM_ENTERSIZEMOVE:
    case WM_EXITSIZEMOVE:
        NotifyFocusTransition();
        break;
    default:
        break;
    }
}

void PumpMessageBootstrap() {
    TryLazyPresentationHook();
    EngineCoreBridge::PumpIpc();

    if (preModFocusCooldown.load() > 0) {
        preModFocusCooldown--;
    }

    PumpMainThreadTasks();

    // Hosted spawn drain is owned by EndScene MMOD_EngineDrainSpawnQueue only.
    // Do NOT spawn from PeekMessage — UE3 Spawn re-enters PeekMessage and
    // deadlocks (2026-07-19). TickHook hosted path also defers to EndScene.

    if (EngineInternal::modReady) {
        EngineCoreBridge::PollMenu();
    }
}

void HandleModInputMessage(LPMSG lpMsg) {
    if (!lpMsg || !EngineInternal::modReady) {
        return;
    }

    if (window.BlockInput) {
        if (!WinInput_MustPassThrough(lpMsg->message)) {
            HandleMessage(lpMsg->hwnd, lpMsg->message, lpMsg->wParam,
                          lpMsg->lParam);
        }

        if (lpMsg->message == WM_KEYDOWN || lpMsg->message == WM_SYSKEYDOWN) {
            TranslateMessage(lpMsg);
            MSG charMsg;
            while (PeekMessage(&charMsg, lpMsg->hwnd, WM_CHAR, WM_DEADCHAR,
                               PM_REMOVE)) {
                (void)charMsg;
            }
        }

        if (WinInput_ShouldSwallowForBlockInput(lpMsg->message) ||
            WinInput_IsKeyboardInput(lpMsg->message)) {
            lpMsg->message = WM_NULL;
        }
    } else {
        HandleMessage(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    }
}

BOOL WINAPI PeekMessageHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                            UINT wMsgFilterMax, UINT wRemoveMsg) {

    if (!window.PeekMessage) {
        return PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    }

    PumpMessageBootstrap();

    const auto ret = window.PeekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax,
                                  wRemoveMsg);

    if (lpMsg && (wRemoveMsg & PM_REMOVE)) {
        HandleFocusMessage(lpMsg);
        HandleModInputMessage(lpMsg);
    }

    return ret;
}

BOOL WINAPI GetMessageHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                           UINT wMsgFilterMax) {

    if (!window.GetMessage) {
        return GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    }

    const auto ret = window.GetMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    if (ret > 0 && lpMsg) {
        PumpMessageBootstrap();
        HandleFocusMessage(lpMsg);
        HandleModInputMessage(lpMsg);
    }

    return ret;
}
} // namespace EnginePresentationInternal

namespace EngineInternal {

bool EnsurePeekMessageHookForGameplay() {
    using namespace EnginePresentationInternal;
    if (hostedMode.load()) {
        return true;
    }
    if (window.PeekMessage) {
        return true;
    }
    if (!Hook::TrampolineHook(PeekMessageHook, PeekMessageW,
                              reinterpret_cast<void **>(&window.PeekMessage))) {
        EngineCoreBridge::Log("engine: Failed to hook PeekMessage");
        return false;
    }
    return true;
}

} // namespace EngineInternal

bool EnginePresentationInternal::InstallPeekMessageBootstrapImpl() {
    using namespace EnginePresentationInternal;
    if (EngineInternal::hostedMode.load()) {
        return true;
    }

    const auto module = GetModuleHandle(nullptr);
    auto hooked = false;

    if (!window.PeekMessage) {
        if (Hook::ImportHook(module, "user32.dll", "PeekMessageW",
                             reinterpret_cast<void *>(PeekMessageHook),
                             reinterpret_cast<void **>(&window.PeekMessage)) ||
            Hook::ImportHook(module, "user32.dll", "PeekMessageA",
                             reinterpret_cast<void *>(PeekMessageHook),
                             reinterpret_cast<void **>(&window.PeekMessage)) ||
            Hook::TrampolineHookNoSuspend(
                PeekMessageHook, PeekMessageW,
                reinterpret_cast<void **>(&window.PeekMessage))) {
            hooked = true;
        }
    } else {
        hooked = true;
    }

    if (!window.GetMessage) {
        if (Hook::ImportHook(module, "user32.dll", "GetMessageW",
                             reinterpret_cast<void *>(GetMessageHook),
                             reinterpret_cast<void **>(&window.GetMessage)) ||
            Hook::ImportHook(module, "user32.dll", "GetMessageA",
                             reinterpret_cast<void *>(GetMessageHook),
                             reinterpret_cast<void **>(&window.GetMessage)) ||
            Hook::TrampolineHookNoSuspend(
                GetMessageHook, GetMessageW,
                reinterpret_cast<void **>(&window.GetMessage))) {
            hooked = true;
        }
    } else {
        hooked = true;
    }

    return hooked;
}
