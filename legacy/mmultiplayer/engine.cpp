#include <Windows.h>
#include <Psapi.h>
#include <atomic>
#include <mutex>
#include <vector>

#include "engine.h"
#include "modhost.h"
#include "mod_host_api.h"
#include "hook.h"
#include "module_contract.h"
#include "mod_log.h"
#include "menu.h"
#include "mod_ipc.h"
#include "me_sdk/runtime/init.h"
#include "me_sdk/runtime/safe_access.h"
#include "me_sdk/runtime/pattern.h"
#include "me_sdk/patterns/hooks.h"
#include "win_input.h"

#include "plugin_ui.h"

// D3D9 and window hooks
static struct {
    std::vector<RenderSceneCallback> Callbacks;
    HRESULT(WINAPI *Original)(IDirect3DDevice9 *) = nullptr;
} renderScene;

static struct {
    HRESULT(WINAPI *Original)(IDirect3DDevice9 *, const RECT *, const RECT *,
                              HWND, const RGNDATA *) = nullptr;
} presentScene;

static struct {
    bool BlockInput = false;
    byte KeysDown[0x100] = {0};
    std::vector<InputCallback> InputCallbacks;
    std::vector<InputCallback> SuperInputCallbacks;

    HWND Window;
    WNDPROC WndProc = nullptr;
    BOOL(WINAPI *PeekMessage)(LPMSG, HWND, UINT, UINT, UINT) = nullptr;
    BOOL(WINAPI *GetMessage)(LPMSG, HWND, UINT, UINT) = nullptr;
} window;

static HMODULE(WINAPI *LoadLibraryAOriginal)(const char *) = nullptr;

// Engine hooks
static struct {
    std::vector<std::wstring> Queue;
    std::mutex Mutex;
} commands;

static struct {
    std::vector<
        std::pair<Engine::Character, Classes::ASkeletalMeshActorSpawnable *&>>
        Queue;
    std::mutex Mutex;
} spawns;

static struct {
    std::vector<ProcessEventCallback> Callbacks;
    int(__thiscall *Original)(Classes::UObject *, class Classes::UFunction *,
                              void *, void *) = nullptr;
} processEvent;

static struct {
    bool Loading = false;
    void *Base = nullptr;
    std::vector<LevelLoadCallback> PreCallbacks;
    std::vector<LevelLoadCallback> PostCallbacks;
    int(__thiscall *Original)(void *, void *, unsigned long long arg);
} levelLoad;

static struct {
    void *PreBase = nullptr;
    void *PostBase = nullptr;
    std::vector<DeathCallback> PreCallbacks;
    std::vector<DeathCallback> PostCallbacks;
    int (*PreOriginal)();
    int (*PostOriginal)();
} death;

static struct {
    std::vector<ActorTickCallback> Callbacks;
    void *(__thiscall *Original)(Classes::AActor *, void *) = nullptr;
} actorTick;

static struct {
    std::vector<BonesTickCallback> Callbacks;
    void *(__thiscall *Original)(void *, void *) = nullptr;
} bonesTick;

static struct {
    D3DXMATRIX *Matrix;
    int *(__thiscall *Original)(Classes::FMatrix *, void *) = nullptr;
} projectionTick;

static struct {
    std::vector<TickCallback> Callbacks;
    void(__thiscall *Original)(float *, int, float) = nullptr;
} tick;

// D3D9 and window hook implementations
static std::atomic<bool> modReady{false};
static std::atomic<bool> modInitializing{false};
static std::atomic<bool> hostedMode{false};
static std::atomic<bool> hostedGameplayLive{false};

static void ReportInitFailure(const char *message) {
    ModLog::Write(message);
}

static std::mutex mainThreadTaskMutex;
static std::vector<void (*)()> mainThreadTasks;

static bool IsAddressInExecutableModule(void *address);
static bool IsPlausibleDeviceVtable(void **vtable);
static bool IsValidD3D9Device(IDirect3DDevice9 *device);
static void **GetDeviceVtable(IDirect3DDevice9 *device);
static IDirect3DDevice9 *FindExistingD3D9Device();
static bool HookDevicePresentation(IDirect3DDevice9 *device);
static bool InstallPresentationHooksInternal(IDirect3DDevice9 *device);
static void OnD3D9DeviceCreated(IDirect3DDevice9 *device);
static void HookIDirect3D9CreateDevice(IDirect3D9 *d3d);

static std::atomic<bool> presentationHooksInstalled{false};
static std::atomic<bool> gameplayHooksInstalled{false};
static std::mutex presentationHookMutex;
static IDirect3DDevice9 *capturedDevice = nullptr;

static Engine::MainThreadTask deferredInitTask = nullptr;
static std::atomic<bool> deferredInitRan{false};
static std::atomic<bool> deferredInitQueued{false};
static std::atomic<int> presentStableFrames{0};
static std::atomic<int> focusCooldownFrames{0};
static std::atomic<int> preModFocusCooldown{0};
static std::atomic<bool> gameForegroundSinceInject{false};
static HRESULT lastCooperativeLevel = D3D_OK;
static DWORD injectTick = 0;

static HWND GetGameWindowHint() {
    if (window.Window) {
        return window.Window;
    }

    if (const auto device = FindExistingD3D9Device()) {
        D3DDEVICE_CREATION_PARAMETERS params = {};
        if (SUCCEEDED(device->GetCreationParameters(&params)) &&
            params.hFocusWindow) {
            return params.hFocusWindow;
        }
    }

    return FindWindowW(nullptr, L"Mirror's Edge");
}

static bool TryLazyPresentationHook() {
    if (presentationHooksInstalled.load()) {
        return true;
    }

    if (injectTick == 0) {
        injectTick = GetTickCount();
    }

    if (GetTickCount() - injectTick < 12000) {
        return false;
    }

    if (!gameForegroundSinceInject.load()) {
        return false;
    }

    if (preModFocusCooldown.load() > 0) {
        return false;
    }

    const auto gameWnd = GetGameWindowHint();
    if (!gameWnd || GetForegroundWindow() != gameWnd) {
        return false;
    }

    if (const auto device = FindExistingD3D9Device()) {
        return InstallPresentationHooksInternal(device);
    }

    return false;
}

static void NotifyFocusTransition() {
    presentStableFrames = 0;
    if (presentationHooksInstalled.load()) {
        focusCooldownFrames = 60;
    } else {
        preModFocusCooldown = 20;
    }
}

static void UpdateDeviceStability(IDirect3DDevice9 *device) {
    if (!device) {
        presentStableFrames = 0;
        return;
    }

    if (focusCooldownFrames.load() > 0) {
        focusCooldownFrames--;
        presentStableFrames = 0;
        return;
    }

    const HRESULT level = device->TestCooperativeLevel();
    if (level == D3DERR_DEVICELOST) {
        presentStableFrames = 0;
        lastCooperativeLevel = level;
        return;
    }

    if (level == D3DERR_DEVICENOTRESET) {
        presentStableFrames = 0;
        lastCooperativeLevel = level;
        return;
    }

    if (lastCooperativeLevel == D3DERR_DEVICENOTRESET && level == D3D_OK) {
        presentStableFrames = 0;
    }

    lastCooperativeLevel = level;

    if (FAILED(level)) {
        presentStableFrames = 0;
        return;
    }

    presentStableFrames++;
}

static bool IsDeviceReadyForOverlay(IDirect3DDevice9 *device) {
    if (!device) {
        return false;
    }
    if (focusCooldownFrames.load() > 0) {
        return false;
    }
    if (FAILED(device->TestCooperativeLevel())) {
        return false;
    }
    return presentStableFrames.load() >= 45;
}

static bool InstallPresentationHooksInternal(IDirect3DDevice9 *device) {
    if (!device) {
        return false;
    }

    if (presentationHooksInstalled.load()) {
        return true;
    }

    return HookDevicePresentation(device);
}

static bool InstallPresentationHooksSafe(IDirect3DDevice9 *device) {
    return InstallPresentationHooksInternal(device);
}

static IDirect3D9 *(WINAPI *Direct3DCreate9Original)(UINT) = nullptr;
static std::atomic<bool> d3d9ExportHooked{false};
static std::atomic<bool> rendererManagedByProxy{false};
static HANDLE modReadyEvent = nullptr;

static const wchar_t *ModReadyEventName() { return L"Local\\mmultiplayer_ready"; }

static bool IsModD3D9ProxyModule(HMODULE module) {
	if (!module) {
		return false;
	}

	wchar_t path[MAX_PATH] = {};
	if (!GetModuleFileNameW(module, path, MAX_PATH)) {
		return false;
	}

	WIN32_FILE_ATTRIBUTE_DATA info = {};
	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info)) {
		return false;
	}

	const auto size =
	    (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
	return size < 512 * 1024;
}

enum { IDirect3D9_CreateDevice = 16 };
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3D9 *, UINT,
                                                    D3DDEVTYPE, HWND, DWORD,
                                                    D3DPRESENT_PARAMETERS *,
                                                    IDirect3DDevice9 **);
static CreateDeviceFn CreateDeviceOriginal = nullptr;

static IDirect3DDevice9 *bootstrapDevice = nullptr;

HRESULT WINAPI EndSceneHook(IDirect3DDevice9 *device);
HRESULT WINAPI PresentHook(IDirect3DDevice9 *device, const RECT *sourceRect,
                           const RECT *destRect, HWND destWindowOverride,
                           const RGNDATA *dirtyRegion);

static void PumpMainThreadTasks();

LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool HookVTableEntry(void **vtable, unsigned index, void *hook,
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
    VirtualProtect(&vtable[index], sizeof(void *), protect, &protect);
    return true;
}

static bool IsPlausibleDeviceVtable(void **vtable) {
    if (!vtable || !vtable[D3D9_EXPORT_PRESENT] ||
        !vtable[D3D9_EXPORT_ENDSCENE]) {
        return false;
    }

    return IsAddressInExecutableModule(vtable[D3D9_EXPORT_PRESENT]) &&
           IsAddressInExecutableModule(vtable[D3D9_EXPORT_ENDSCENE]);
}

static void **GetDeviceVtable(IDirect3DDevice9 *device) {
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

static bool IsValidD3D9Device(IDirect3DDevice9 *device) {
    if (!device || reinterpret_cast<uintptr_t>(device) < 0x10000) {
        return false;
    }

    const auto vtable = GetDeviceVtable(device);
    if (!vtable) {
        return false;
    }

    D3DDEVICE_CREATION_PARAMETERS params = {};
    if (FAILED(device->GetCreationParameters(&params))) {
        return false;
    }

    return params.hFocusWindow != nullptr;
}

static bool IsValidD3D9DeviceSafe(IDirect3DDevice9 *device) {
    __try {
        return IsValidD3D9Device(device);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static IDirect3DDevice9 *FindExistingD3D9Device() {
    const auto exe = GetModuleHandle(nullptr);
    if (!exe) {
        return nullptr;
    }

    MODULEINFO info = {};
    if (!GetModuleInformation(GetCurrentProcess(), exe, &info, sizeof(info))) {
        return nullptr;
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
        auto cursor = reinterpret_cast<byte *>(exe);
        auto remaining = static_cast<int>(info.SizeOfImage);

        while (remaining > 9) {
            auto match = Pattern::FindPattern(cursor, remaining, pattern.bytes,
                                              pattern.mask);
            if (!match) {
                break;
            }

            IDirect3DDevice9 **global = nullptr;
            const auto insn = reinterpret_cast<byte *>(match);
            if (pattern.bytes[0] == '\xA1') {
                global = reinterpret_cast<IDirect3DDevice9 **>(
                    *reinterpret_cast<uintptr_t *>(insn + 1));
            } else {
                global = reinterpret_cast<IDirect3DDevice9 **>(
                    *reinterpret_cast<uintptr_t *>(insn + 2));
            }

            MEMORY_BASIC_INFORMATION mbi = {};
            if (global && VirtualQuery(global, &mbi, sizeof(mbi)) != 0 &&
                (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE |
                                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
                const auto device = *global;
                if (IsValidD3D9DeviceSafe(device)) {
                    return device;
                }
            }

            const auto next = reinterpret_cast<byte *>(match) + 1;
            remaining -= static_cast<int>(next - cursor);
            cursor = next;
        }
    }

    return nullptr;
}

static bool HookDevicePresentationInternal(IDirect3DDevice9 *device) {
    if (!device) {
        return false;
    }

    if (presentationHooksInstalled.load()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(presentationHookMutex);
    if (presentationHooksInstalled.load()) {
        return true;
    }

    const auto vtable = GetDeviceVtable(device);
    if (!vtable || !vtable[D3D9_EXPORT_ENDSCENE] ||
        !vtable[D3D9_EXPORT_PRESENT]) {
        return false;
    }

    capturedDevice = device;
    bootstrapDevice = device;

    renderScene.Original =
        reinterpret_cast<decltype(renderScene.Original)>(
            vtable[D3D9_EXPORT_ENDSCENE]);
    presentScene.Original = reinterpret_cast<decltype(presentScene.Original)>(
        vtable[D3D9_EXPORT_PRESENT]);

    if (!HookVTableEntry(vtable, D3D9_EXPORT_ENDSCENE,
                         reinterpret_cast<void *>(EndSceneHook), nullptr)) {
        renderScene.Original = nullptr;
        presentScene.Original = nullptr;
        return false;
    }

    if (!HookVTableEntry(vtable, D3D9_EXPORT_PRESENT,
                         reinterpret_cast<void *>(PresentHook), nullptr)) {
        DWORD protect = 0;
        if (VirtualProtect(&vtable[D3D9_EXPORT_ENDSCENE], sizeof(void *),
                           PAGE_READWRITE, &protect)) {
            vtable[D3D9_EXPORT_ENDSCENE] =
                reinterpret_cast<void *>(renderScene.Original);
            VirtualProtect(&vtable[D3D9_EXPORT_ENDSCENE], sizeof(void *),
                           protect, &protect);
        }

        renderScene.Original = nullptr;
        presentScene.Original = nullptr;
        return false;
    }

    presentationHooksInstalled = true;
    return true;
}

static IDirect3DDevice9 *g_presentationHookDevice = nullptr;

static bool HookDevicePresentation(IDirect3DDevice9 *device) {
    if (!device) {
        return false;
    }

    if (presentationHooksInstalled.load()) {
        return true;
    }

    if (HookDevicePresentationInternal(device)) {
        return true;
    }

    g_presentationHookDevice = device;
    // Retry once without process-wide SuspendThread (race on vtable patch).
    return HookDevicePresentationInternal(device);
}

static void OnD3D9DeviceCreated(IDirect3DDevice9 *device) {
    if (!device) {
        return;
    }

    bootstrapDevice = device;
}

static HRESULT STDMETHODCALLTYPE CreateDeviceHook(
    IDirect3D9 *self, UINT adapter, D3DDEVTYPE deviceType, HWND focusWindow,
    DWORD behaviorFlags, D3DPRESENT_PARAMETERS *presentationParameters,
    IDirect3DDevice9 **returnedDeviceInterface) {

    if (!CreateDeviceOriginal) {
        return D3DERR_INVALIDCALL;
    }

    const auto hr = CreateDeviceOriginal(
        self, adapter, deviceType, focusWindow, behaviorFlags,
        presentationParameters, returnedDeviceInterface);

    if (SUCCEEDED(hr) && returnedDeviceInterface &&
        *returnedDeviceInterface) {
        OnD3D9DeviceCreated(*returnedDeviceInterface);
    }

    return hr;
}

static void HookIDirect3D9CreateDevice(IDirect3D9 *d3d) {
    if (!d3d || CreateDeviceOriginal) {
        return;
    }

    const auto vtable = *reinterpret_cast<void ***>(d3d);
    if (!vtable || !vtable[IDirect3D9_CreateDevice]) {
        return;
    }

    CreateDeviceOriginal = reinterpret_cast<CreateDeviceFn>(
        vtable[IDirect3D9_CreateDevice]);

    HookVTableEntry(vtable, IDirect3D9_CreateDevice,
                    reinterpret_cast<void *>(CreateDeviceHook), nullptr);
}

static IDirect3D9 *WINAPI Direct3DCreate9Hook(UINT sdkVersion) {
    if (!Direct3DCreate9Original) {
        return nullptr;
    }

    const auto d3d = Direct3DCreate9Original(sdkVersion);
    if (d3d) {
        HookIDirect3D9CreateDevice(d3d);
    }

    return d3d;
}

static bool IsAddressInExecutableModule(void *address) {
    if (!address) {
        return false;
    }

    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address), &module)) {
        return false;
    }

    return module != nullptr;
}

static void RenderModOverlay(IDirect3DDevice9 *device) {
    (void)device;
    if (hostedMode.load() || !modReady) {
        return;
    }

    PumpMainThreadTasks();
}

HRESULT WINAPI EndSceneHook(IDirect3DDevice9 *device) {
    if (!renderScene.Original) {
        return D3D_OK;
    }

    if (device) {
        const HRESULT level = device->TestCooperativeLevel();
        if (level == D3DERR_DEVICELOST || level == D3DERR_DEVICENOTRESET) {
            return renderScene.Original(device);
        }
    }

    if (presentationHooksInstalled.load() && focusCooldownFrames.load() == 0) {
        if (deferredInitTask && !deferredInitRan.load() &&
            !deferredInitQueued.load() && IsDeviceReadyForOverlay(device) &&
            Engine::IsGameReadyForModInit()) {
            deferredInitQueued = true;
            Engine::QueueMainThreadTask([]() {
                deferredInitQueued = false;
                if (!deferredInitTask || deferredInitRan.load()) {
                    return;
                }

                deferredInitTask();
                if (Engine::IsModReady()) {
                    deferredInitRan = true;
                    deferredInitTask = nullptr;
                }
            });
        }

        if (Engine::IsModReady() && IsDeviceReadyForOverlay(device)) {
            RenderModOverlay(device);
        }
    }

    return renderScene.Original(device);
}

HRESULT WINAPI PresentHook(IDirect3DDevice9 *device, const RECT *sourceRect,
                           const RECT *destRect, HWND destWindowOverride,
                           const RGNDATA *dirtyRegion) {
    UpdateDeviceStability(device);

    if (!presentScene.Original) {
        return D3D_OK;
    }

    return presentScene.Original(device, sourceRect, destRect, destWindowOverride,
                                 dirtyRegion);
}

void HandleMessage(HWND hWnd, UINT &msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam < sizeof(window.KeysDown)) {
            const auto k = &window.KeysDown[wParam];
            if (!*k) {
                const auto block = window.BlockInput;

                for (const auto &callback : window.SuperInputCallbacks) {
                    callback(msg, wParam);
                }

                if (!block) {
                    for (const auto &callback : window.InputCallbacks) {
                        callback(msg, wParam);
                    }
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

                for (const auto &callback : window.SuperInputCallbacks) {
                    callback(msg, wParam);
                }

                if (!block) {
                    for (const auto &callback : window.InputCallbacks) {
                        callback(msg, wParam);
                    }
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

static void PumpMainThreadTasks() {
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
        if (task) {
            task();
        }
    }

    pumping = false;
}

static void HandleFocusMessage(LPMSG lpMsg) {
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

static void PumpMessageBootstrap() {
    TryLazyPresentationHook();
    ModIpc::Pump();

    if (preModFocusCooldown.load() > 0) {
        preModFocusCooldown--;
    }

    PumpMainThreadTasks();

    if (modReady) {
        Menu::PollToggle();
    }
}

static void HandleModInputMessage(LPMSG lpMsg) {
    if (!lpMsg || !modReady) {
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

// Engine hook implementations
int __fastcall ProcessEventHook(Classes::UObject *object, void *idle,
                                class Classes::UFunction *function, void *args,
                                void *result) {

    if (!modReady || modInitializing || !Engine::IsHostedGameplayLive()) {
        return processEvent.Original(object, function, args, result);
    }

    auto sum = 0;
    for (auto callback : processEvent.Callbacks) {
        sum += callback(object, function, args, result);
    }

    return sum == 0 ? processEvent.Original(object, function, args, result) : 0;
}

int __fastcall LevelLoadHook(void *this_, void *idle, void **levelInfo,
                             unsigned long long arg) {

    if (!modReady) {
        return levelLoad.Original(this_, levelInfo, arg);
    }

    const auto levelName = reinterpret_cast<const wchar_t *>(levelInfo[7]);

    for (const auto &callback : levelLoad.PreCallbacks) {
        callback(levelName);
    }

    spawns.Mutex.lock();
    spawns.Queue.clear();
    spawns.Queue.shrink_to_fit();

    levelLoad.Loading = true;
    const auto ret = levelLoad.Original(this_, levelInfo, arg);
    levelLoad.Loading = false;

    spawns.Mutex.unlock();

    for (const auto &callback : levelLoad.PostCallbacks) {
        callback(levelName);
    }

    return ret;
}

int PreDeathHook() {
    if (!modReady) {
        return death.PreOriginal();
    }

    for (const auto &callback : death.PreCallbacks) {
        callback();
    }

    return death.PreOriginal();
}

int PostDeathHook() {
    const auto ret = death.PostOriginal();

    if (!modReady) {
        return ret;
    }

    for (const auto &callback : death.PostCallbacks) {
        callback();
    }

    return ret;
}

HMODULE WINAPI LoadLibraryAHook(const char *module) {
    if (strstr(module, "menl_hooks.dll")) {
        Hook::UnTrampolineHook(levelLoad.Base, levelLoad.Original);
        Hook::UnTrampolineHook(death.PreBase, death.PreOriginal);
        Hook::UnTrampolineHook(death.PostBase, death.PostOriginal);

        std::thread([]() {
            for (;;) {
                if (*reinterpret_cast<byte *>(death.PostBase) == 0xE9) {
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

                Sleep(1);
            }
        }).detach();
    }

    return LoadLibraryAOriginal(module);
}

void *__fastcall ActorTickHook(Classes::AActor *actor, void *idle, void *arg) {
    if (!modReady || modInitializing || !Engine::IsHostedGameplayLive()) {
        return actorTick.Original(actor, arg);
    }

    if (levelLoad.Loading) {
        return actorTick.Original(actor, arg);
    }

    const auto controller = Engine::GetPlayerController(false);
    if (!controller || controller->IsInMainMenu()) {
        return actorTick.Original(actor, arg);
    }

    for (const auto &callback : actorTick.Callbacks) {
        callback(actor);
    }

    return actorTick.Original(actor, arg);
}

void *__fastcall BonesTickHook(void *this_, void *idle, void *arg) {
    const auto bones = static_cast<Classes::TArray<Classes::FBoneAtom> *>(
        bonesTick.Original(this_, arg));

    if (!modReady || levelLoad.Loading || !Engine::IsHostedGameplayLive()) {
        return bones;
    }

    const auto controller = Engine::GetPlayerController(false);
    if (!controller || controller->IsInMainMenu()) {
        return bones;
    }

    if (bones->Num()) {
        for (const auto &callback : bonesTick.Callbacks) {
            callback(bones);
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

    const auto player = Engine::GetPlayerPawn();
    if (!player) {
        return nullptr;
    }

    const auto actor = static_cast<Classes::ASkeletalMeshActorSpawnable *>(
        player->Spawn(Classes::ASkeletalMeshActorSpawnable::StaticClass(), 0, 0,
                      {0}, {0}, 0, true));

    actor->SetCollisionType(Classes::ECollisionType::COLLIDE_NoCollision);

    const auto mesh = actor->SkeletalMeshComponent;
    mesh->SetSkeletalMesh(
        static_cast<Classes::USkeletalMesh *>(actor->STATIC_DynamicLoadObject(
            meshes[static_cast<size_t>(character)],
            Classes::USkeletalMesh::StaticClass(), false)),
        false);

    const auto mats = materials[static_cast<size_t>(character)];
    for (auto i = 0UL; i < mats.size(); ++i) {
        mesh->SetMaterial(
            i, static_cast<Classes::UMaterialInterface *>(
                   actor->STATIC_DynamicLoadObject(
                       mats[i].c_str(),
                       Classes::UMaterialInterface::StaticClass(), false)));
    }

    if (character == Engine::Character::Kate ||
        character == Engine::Character::Miller ||
        character == Engine::Character::Kreeg) {

        actor->PrePivot.Z = 94;
    }

    mesh->bUpdateSkelWhenNotRendered = true;
    return actor;
}

void __fastcall TickHook(float *scales, void *idle, int arg, float delta) {
    if (!modReady || modInitializing || !Engine::IsHostedGameplayLive()) {
        tick.Original(scales, arg, delta);
        return;
    }

    const auto inGameplay = [&]() {
        if (levelLoad.Loading) {
            return false;
        }

        const auto controller = Engine::GetPlayerController(false);
        if (!controller || controller->IsInMainMenu()) {
            return false;
        }

        return true;
    }();

    if (inGameplay && Engine::GetPlayerPawn(true)) {
        // Queues must be executed inside the context of an engine thread in
        // sync with a tick
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
            spawns.Mutex.lock();

            for (auto &spawn : spawns.Queue) {
                if (!spawn.second) {
                    spawn.second = SpawnCharacter(spawn.first);
                }
            }

            spawns.Queue.clear();
            spawns.Queue.shrink_to_fit();

            spawns.Mutex.unlock();
        }
    }

    if (inGameplay) {
        for (auto callback : tick.Callbacks) {
            callback(delta);
        }
    }

    tick.Original(scales, arg, delta);
}

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
    commands.Mutex.lock();
    commands.Queue.push_back(command);
    commands.Mutex.unlock();
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

Classes::AWorldInfo *Engine::GetWorld(bool update) {
    static Classes::AWorldInfo *cache = nullptr;

    if (levelLoad.Loading) {
        return nullptr;
    }

    if (!cache || update) {
        const auto objects = Classes::UObject::GetGlobalObjects();
        for (auto i = 0UL; i < objects.Num(); ++i) {
            const auto object = objects.GetByIndex(i);
            if (!(object && object->IsA(Classes::AWorldInfo::StaticClass()))) {
                continue;
            }

            const auto world = static_cast<Classes::AWorldInfo *>(object);

            for (auto controller = world->ControllerList; controller;
                 controller = controller->NextController) {

                if (controller->IsA(
                        Classes::ATdPlayerController::StaticClass())) {

                    cache = world;
                    return cache;
                }
            }
        }
    }

    return cache;
}

static bool IsControllablePlayerPawn(Classes::ATdPlayerPawn *pawn) {
    if (!MeSdk::Safe::IsPlausibleUObject(pawn)) {
        return false;
    }

    if (pawn->bDeleteMe || pawn->bPendingDelete) {
        return false;
    }

    return pawn->IsA(Classes::ATdPlayerPawn::StaticClass());
}

Classes::ATdPlayerController *Engine::GetPlayerController(bool update) {
    static Classes::ATdPlayerController *cache = nullptr;

    if (levelLoad.Loading) {
        cache = nullptr;
        return nullptr;
    }

    if (!cache || update) {
        cache = nullptr;

        auto world = GetWorld(update);
        if (world) {
            for (auto controller = world->ControllerList; controller;
                 controller = controller->NextController) {

                if (!MeSdk::Safe::IsPlausibleUObject(controller) ||
                    !controller->IsA(
                        Classes::ATdPlayerController::StaticClass())) {
                    continue;
                }

                auto playerController =
                    static_cast<Classes::ATdPlayerController *>(controller);

                if (!playerController->PlayerCamera ||
                    playerController->IsInMainMenu()) {
                    continue;
                }

                cache = playerController;
                break;
            }
        }
    } else if (!MeSdk::Safe::IsPlausibleUObject(cache) ||
               !cache->IsA(Classes::ATdPlayerController::StaticClass()) ||
               cache->IsInMainMenu()) {
        cache = nullptr;
    }

    return cache;
}

Classes::ATdPlayerPawn *Engine::GetPlayerPawn(bool update) {
    static Classes::ATdPlayerPawn *cache = nullptr;

    if (levelLoad.Loading) {
        cache = nullptr;
        return nullptr;
    }

    if (!cache || update) {
        cache = nullptr;

        auto controller = GetPlayerController(update);
        if (controller) {
            if (controller->IsInMainMenu()) {
                return nullptr;
            }

            auto pawn = static_cast<Classes::ATdPlayerPawn *>(
                controller->AcknowledgedPawn);
            if (!IsControllablePlayerPawn(pawn)) {
                pawn = static_cast<Classes::ATdPlayerPawn *>(controller->Pawn);
            }

            if (IsControllablePlayerPawn(pawn)) {
                cache = pawn;
            }
        }
    } else if (!IsControllablePlayerPawn(cache)) {
        cache = nullptr;
    }

    return cache;
}

bool Engine::CanSafelyUsePlayerPawn() {
    if (!modReady || levelLoad.Loading) {
        return false;
    }

    const auto controller = GetPlayerController(true);
    if (!controller) {
        return false;
    }

    if (controller->IsInMainMenu()) {
        return false;
    }

    return GetPlayerPawn(true) != nullptr;
}

void Engine::SpawnCharacter(Character character,
                            Classes::ASkeletalMeshActorSpawnable *&spawned) {
    spawned = nullptr;

    spawns.Mutex.lock();
    spawns.Queue.push_back({character, spawned});
    spawns.Mutex.unlock();
}

void Engine::Despawn(Classes::ASkeletalMeshActorSpawnable *actor) {
    if (!actor) {
        return;
    }

    actor->ShutDown();
}

void Engine::TransformBones(Character character,
                            Classes::TArray<Classes::FBoneAtom> *destBones,
                            Classes::FBoneAtom *src) {

    const auto dest = destBones->Buffer();
    const auto destCount = destBones->Num();

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
    return !window.BlockInput && vk >= 0 && vk < ARRAYSIZE(window.KeysDown) &&
           window.KeysDown[vk];
}

bool Engine::WorldToScreen(IDirect3DDevice9 *device,
                           Classes::FVector &inOutLocation) {
    const auto controller = Engine::GetPlayerController();
    if (!controller || !projectionTick.Matrix) {
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
    proj = *projectionTick.Matrix;

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

HWND Engine::GetWindow() { return window.Window; }

void Engine::OnRenderScene(RenderSceneCallback callback) {
    if (hostedMode.load()) {
        if (const auto *host = ModHost::Get()) {
            if (host->OnRenderScene) {
                host->OnRenderScene(ModHost::WrapRenderCallback(callback));
                return;
            }
        }
    }

    renderScene.Callbacks.push_back(callback);
}

void Engine::OnProcessEvent(ProcessEventCallback callback) {
    processEvent.Callbacks.push_back(callback);
}

void Engine::OnPreLevelLoad(LevelLoadCallback callback) {
    levelLoad.PreCallbacks.push_back(callback);
}

void Engine::OnPostLevelLoad(LevelLoadCallback callback) {
    levelLoad.PostCallbacks.push_back(callback);
}

void Engine::OnPreDeath(DeathCallback callback) {
    death.PreCallbacks.push_back(callback);
}

void Engine::OnPostDeath(DeathCallback callback) {
    death.PostCallbacks.push_back(callback);
}

void Engine::OnActorTick(ActorTickCallback callback) {
    actorTick.Callbacks.push_back(callback);
}

void Engine::OnBonesTick(BonesTickCallback callback) {
    bonesTick.Callbacks.push_back(callback);
}

void Engine::OnTick(TickCallback callback) {
    tick.Callbacks.push_back(callback);
}

void Engine::OnInput(InputCallback callback) {
    window.InputCallbacks.push_back(callback);
}

void Engine::OnSuperInput(InputCallback callback) {
    window.SuperInputCallbacks.push_back(callback);
}

void Engine::BlockInput(bool block) {
    if (hostedMode.load()) {
        if (const auto *host = ModHost::Get()) {
            if (host->BlockInput) {
                host->BlockInput(block);
                return;
            }
        }
    }

    window.BlockInput = block;
}

void Engine::BeginInitialization() { modInitializing = true; }

bool Engine::IsInitializing() { return modInitializing; }

bool Engine::IsModReady() { return modReady; }

void Engine::MarkReady() {
    modInitializing = false;
    modReady = true;

    if (!modReadyEvent) {
        modReadyEvent = CreateEventW(nullptr, TRUE, FALSE, ModReadyEventName());
    }
    if (modReadyEvent) {
        SetEvent(modReadyEvent);
    }

    ModIpc::Start();
}

bool Engine::IsGameReadyForModInit() { return MeSdk::ProbeGlobals(); }

void Engine::SetDeferredInitCallback(MainThreadTask initCallback) {
    deferredInitTask = initCallback;
    if (injectTick == 0) {
        injectTick = GetTickCount();
    }

    if (!modReadyEvent) {
        modReadyEvent = CreateEventW(nullptr, TRUE, FALSE, ModReadyEventName());
    }
}

void Engine::SetHostedMode(bool hosted) {
    hostedMode = hosted;
    if (hosted) {
        hostedGameplayLive = false;
    }
}

bool Engine::IsHostedMode() { return hostedMode.load(); }

void Engine::SetHostedGameplayLive(bool live) { hostedGameplayLive = live; }

bool Engine::IsHostedGameplayLive() {
    return !hostedMode.load() || hostedGameplayLive.load();
}

bool Engine::InstallRendererCapture() {
    if (hostedMode.load()) {
        return true;
    }

    if (rendererManagedByProxy.load() || IsModD3D9ProxyActive()) {
        return true;
    }

    if (presentationHooksInstalled.load()) {
        return true;
    }

    // InitWorker waits for d3d9.dll before calling here. Patching Direct3DCreate9 or
    // probing the live device from the inject worker crashes the render thread.
    // Presentation hooks install later via PeekMessage + TryLazyPresentationHook.
    if (GetModuleHandleA("d3d9.dll")) {
        return true;
    }

    if (d3d9ExportHooked.exchange(true)) {
        return true;
    }

    const auto d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9) {
        d3d9ExportHooked = false;
        return false;
    }

    const auto exportAddr = GetProcAddress(d3d9, "Direct3DCreate9");
    if (!exportAddr) {
        d3d9ExportHooked = false;
        return false;
    }

    if (!Hook::TrampolineHookNoSuspend(
            reinterpret_cast<void *>(Direct3DCreate9Hook), exportAddr,
            reinterpret_cast<void **>(&Direct3DCreate9Original))) {
        d3d9ExportHooked = false;
        return false;
    }

    return true;
}

bool Engine::TryCaptureRenderer() {
    return TryLazyPresentationHook();
}

bool Engine::HookDirect3D9Interface(IDirect3D9 *d3d) {
    if (!d3d) {
        return false;
    }

    rendererManagedByProxy = true;
    HookIDirect3D9CreateDevice(d3d);
    return true;
}

void Engine::OnProxyDeviceCreated(IDirect3DDevice9 *device) {
    if (!device) {
        return;
    }

    rendererManagedByProxy = true;
    bootstrapDevice = device;
    HookDevicePresentation(device);
}

bool Engine::IsModD3D9ProxyActive() {
    if (rendererManagedByProxy.load()) {
        return true;
    }

    return IsModD3D9ProxyModule(GetModuleHandleA("d3d9.dll"));
}

bool Engine::ArePresentationHooksInstalled() {
    if (hostedMode.load()) {
        if (const auto *host = ModHost::Get()) {
            if (host->ArePresentationHooksInstalled) {
                return host->ArePresentationHooksInstalled();
            }
        }
        return true;
    }

    return presentationHooksInstalled.load();
}

bool Engine::InstallPeekMessageBootstrap() {
    if (hostedMode.load()) {
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

void Engine::QueueMainThreadTask(MainThreadTask task) {
    if (!task) {
        return;
    }

    if (hostedMode.load()) {
        if (const auto *host = ModHost::Get()) {
            if (host->QueueMainThreadTask) {
                host->QueueMainThreadTask(task);
                return;
            }
        }
    }

    std::lock_guard<std::mutex> lock(mainThreadTaskMutex);
    mainThreadTasks.push_back(task);
}

bool Engine::InitializeSDK() {
    if (MeSdk::InitializeGlobals()) {
        return true;
    }

    ReportInitFailure("init: Failed to find GNames/GObjects");
    return false;
}

static bool InstallGameplayHooksInternal() {
    void *ptr = nullptr;

    if (!Hook::TrampolineHook(LoadLibraryAHook, LoadLibraryA,
                              reinterpret_cast<void **>(&LoadLibraryAOriginal))) {
        return false;
    }

    if (!hostedMode.load()) {
        if (!window.PeekMessage &&
            !Hook::TrampolineHook(PeekMessageHook, PeekMessageW,
                                  reinterpret_cast<void **>(&window.PeekMessage))) {

            ModLog::Write("mmultiplayer: Failed to hook PeekMessage");
            return false;
        }
    }

    // ProcessEvent
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::ProcessEvent,
                                      MeSdk::Patterns::Hooks::ProcessEventMask))) {

        ModLog::Write("mmultiplayer: Failed to find ProcessEvent");
        return false;
    }

    if (!Hook::TrampolineHook(
            ProcessEventHook, ptr,
            reinterpret_cast<void **>(&processEvent.Original))) {

        ModLog::Write("mmultiplayer: Failed to hook ProcessEvent");
        return false;
    }

    // LevelLoad
    if (!(ptr = levelLoad.Base = Pattern::FindPattern(MeSdk::Patterns::Hooks::LevelLoad,
                                                      MeSdk::Patterns::Hooks::LevelLoadMask))) {

        ModLog::Write("mmultiplayer: Failed to find LevelLoad");
        return false;
    }

    if (!Hook::TrampolineHook(LevelLoadHook, ptr,
                              reinterpret_cast<void **>(&levelLoad.Original))) {

        ModLog::Write("mmultiplayer: Failed to hook LevelLoad");
        return false;
    }

    // PreDeath
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::PreDeathAnchor,
                                      MeSdk::Patterns::Hooks::PreDeathAnchorMask))) {

        ModLog::Write("mmultiplayer: Failed to find PreDeath (1)");
        return false;
    }

    if (!(ptr = death.PreBase = Pattern::FindPattern(
              ptr, 0x1000, MeSdk::Patterns::Hooks::PreDeath,
              MeSdk::Patterns::Hooks::PreDeathMask))) {

        ModLog::Write("mmultiplayer: Failed to find PreDeath (2)");
        return false;
    }

    if (!Hook::TrampolineHook(PreDeathHook, ptr,
                              reinterpret_cast<void **>(&death.PreOriginal))) {

        ModLog::Write("mmultiplayer: Failed to hook PreDeath");
        return false;
    }

    // PostDeath
    if (!(ptr = death.PostBase = Pattern::FindPattern(
              ptr, 0x1000, MeSdk::Patterns::Hooks::PostDeath,
              MeSdk::Patterns::Hooks::PostDeathMask))) {

        ModLog::Write("mmultiplayer: Failed to find PostDeath");
        return false;
    }

    if (!Hook::TrampolineHook(PostDeathHook, ptr,
                              reinterpret_cast<void **>(&death.PostOriginal))) {

        ModLog::Write("mmultiplayer: Failed to hook PreDeath");
        return false;
    }

    // ActorTick
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::ActorTick,
                                      MeSdk::Patterns::Hooks::ActorTickMask))) {

        ModLog::Write("mmultiplayer: Failed to find ActorTick");
        return false;
    }

    if (!Hook::TrampolineHook(ActorTickHook, ptr,
                              reinterpret_cast<void **>(&actorTick.Original))) {

        ModLog::Write("mmultiplayer: Failed to hook ActorTick");
        return false;
    }

    // BonesTick
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::BonesTick,
                                      MeSdk::Patterns::Hooks::BonesTickMask))) {

        ModLog::Write("mmultiplayer: Failed to find BonesTick");
        return false;
    }

    if (!Hook::TrampolineHook(BonesTickHook, RELATIVE_ADDR(ptr, 5),
                              reinterpret_cast<void **>(&bonesTick.Original))) {

        ModLog::Write("mmultiplayer: Failed to hook BonesTick");
        return false;
    }

    // ProjectionTick
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::ProjectionTick,
                                      MeSdk::Patterns::Hooks::ProjectionTickMask))) {
        ModLog::Write("mmultiplayer: Failed to find ProjectionTick");
        return false;
    }

    if (!Hook::TrampolineHook(
            ProjectionTick, ptr,
            reinterpret_cast<void **>(&projectionTick.Original))) {

        ModLog::Write("mmultiplayer: Failed to hook ProjectionTick");
        return false;
    }

    // Tick
    if (!(ptr = Pattern::FindPattern(MeSdk::Patterns::Hooks::Tick,
                                      MeSdk::Patterns::Hooks::TickMask))) {

        ModLog::Write("mmultiplayer: Failed to find Tick");
        return false;
    }

    if (!Hook::TrampolineHook(TickHook, ptr,
                              reinterpret_cast<void **>(&tick.Original))) {

        ModLog::Write("mmultiplayer: Failed to hook Tick");
        return false;
    }

    return true;
}

bool Engine::AreGameplayHooksInstalled() {
    return gameplayHooksInstalled.load();
}

static void InstallGameplayHooksOnMainThread() {
    if (gameplayHooksInstalled.load()) {
        return;
    }
    if (InstallGameplayHooksInternal()) {
        gameplayHooksInstalled = true;
    }
}

static bool WaitForGameplayHooks(DWORD timeoutMs) {
    const DWORD deadline = GetTickCount() + timeoutMs;
    while (!gameplayHooksInstalled.load()) {
        if (GetTickCount() >= deadline) {
            return false;
        }
        Sleep(5);
    }
    return true;
}

bool Engine::InstallGameplayHooks() {
    if (InstallGameplayHooksInternal()) {
        return true;
    }

    if (hostedMode.load()) {
        QueueMainThreadTask(InstallGameplayHooksOnMainThread);
        return false;
    }

    return InstallGameplayHooksInternal();
}

bool Engine::EnsureGameplayHooks() {
    if (gameplayHooksInstalled.load()) {
        return true;
    }

    if (TryInstallGameplayHooksSync()) {
        return true;
    }

    if (hostedMode.load()) {
        QueueMainThreadTask(InstallGameplayHooksOnMainThread);
        return WaitForGameplayHooks(30000);
    }

    if (InstallGameplayHooksInternal()) {
        gameplayHooksInstalled = true;
        return true;
    }

    return false;
}

bool Engine::TryInstallGameplayHooksSync() {
    if (gameplayHooksInstalled.load()) {
        return true;
    }

    if (hostedMode.load()) {
        QueueMainThreadTask(InstallGameplayHooksOnMainThread);
        return WaitForGameplayHooks(30000);
    }

    if (!InstallGameplayHooksInternal()) {
        return false;
    }

    gameplayHooksInstalled = true;
    return true;
}

bool Engine::TryInstallGameplayHooksHosted() {
    if (gameplayHooksInstalled.load()) {
        return true;
    }

    QueueMainThreadTask(InstallGameplayHooksOnMainThread);
    return WaitForGameplayHooks(30000);
}

bool Engine::Initialize() {
    return InitializeSDK() && InstallGameplayHooks();
}

bool Engine::InstallPresentationHooks(IDirect3DDevice9 *device) {
    if (presentationHooksInstalled.load()) {
        return true;
    }

    if (!device) {
        device = capturedDevice ? capturedDevice : bootstrapDevice;
    }
    if (!device) {
        device = FindExistingD3D9Device();
    }
    if (!device) {
        return false;
    }

    return HookDevicePresentation(device);
}