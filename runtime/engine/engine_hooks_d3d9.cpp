#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <vector>

#include "engine.h"
#include "engine_presentation_internal.h"
#include "engine_internal.h"
#include "debug_trace.h"
#include "engine_core_bridge.h"
#include "hook.h"
#include "me_sdk/runtime/pattern.h"
#include "plugin_seh_guard.h"
#include "plugin_ui.h"
#include "timing_constants.h"

namespace EnginePresentationInternal {

namespace {

struct RenderSceneCallbackContext {
    RenderSceneCallback callback = nullptr;
    IDirect3DDevice9 *device = nullptr;
};

void InvokeRenderSceneCallback(void *data) {
    auto *ctx = static_cast<RenderSceneCallbackContext *>(data);
    ctx->callback(ctx->device);
}

void LogRenderCallbackFault(DWORD exceptionCode) {
    char message[160] = {};
    snprintf(message, sizeof(message),
             "engine: plugin render callback crashed (0x%08lX); callback removed",
             static_cast<unsigned long>(exceptionCode));
    EngineCoreBridge::Log(message);
}

void DispatchRenderSceneCallbacks(IDirect3DDevice9 *device) {
    std::vector<RenderSceneCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(renderScene.Mutex);
        callbacks = renderScene.Callbacks;
    }

    std::vector<RenderSceneCallback> crashedCallbacks;
    for (const auto callback : callbacks) {
        RenderSceneCallbackContext ctx = {callback, device};
        DWORD exceptionCode = 0;
        if (!PluginSehGuard::InvokeVoid(
                "plugin_render_scene",
                "engine_hooks_d3d9.cpp:DispatchRenderSceneCallbacks",
                InvokeRenderSceneCallback, &ctx, &exceptionCode)) {
            crashedCallbacks.push_back(callback);
            LogRenderCallbackFault(exceptionCode);
        }
    }

    for (const auto callback : crashedCallbacks) {
        std::lock_guard<std::mutex> lock(renderScene.Mutex);
        renderScene.Callbacks.erase(
            std::remove(renderScene.Callbacks.begin(), renderScene.Callbacks.end(),
                        callback),
            renderScene.Callbacks.end());
    }
}

} // namespace

enum { IDirect3D9_CreateDevice = 16 };
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE *)(IDirect3D9 *, UINT,
                                                    D3DDEVTYPE, HWND, DWORD,
                                                    D3DPRESENT_PARAMETERS *,
                                                    IDirect3DDevice9 **);
static CreateDeviceFn CreateDeviceOriginal = nullptr;
HWND GetGameWindowHint() {
    if (window.Window && IsWindow(window.Window)) {
        return window.Window;
    }

    if (const auto device = FindExistingD3D9Device()) {
        D3DDEVICE_CREATION_PARAMETERS params = {};
        if (SUCCEEDED(device->GetCreationParameters(&params)) &&
            params.hFocusWindow) {
            window.Window = params.hFocusWindow;
            return params.hFocusWindow;
        }
    }

    // Exact title match fails under borderless ("Mirror's EdgeT"); use engine
    // PID enum via GetWindow() instead.
    return Engine::GetWindow();
}

bool TryLazyPresentationHook() {
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

void NotifyFocusTransition() {
    presentStableFrames = 0;
    if (presentationHooksInstalled.load()) {
        focusCooldownFrames = Timing::kTargetFps;
    } else {
        preModFocusCooldown = 20;
    }
}

void UpdateDeviceStability(IDirect3DDevice9 *device) {
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

bool IsDeviceReadyForOverlay(IDirect3DDevice9 *device) {
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

bool InstallPresentationHooksInternal(IDirect3DDevice9 *device) {
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

bool IsModD3D9ProxyModule(HMODULE module) {
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

HRESULT WINAPI EndSceneHook(IDirect3DDevice9 *device);
HRESULT WINAPI PresentHook(IDirect3DDevice9 *device, const RECT *sourceRect,
                           const RECT *destRect, HWND destWindowOverride,
                           const RGNDATA *dirtyRegion);

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

IDirect3DDevice9 *FindExistingD3D9Device() {
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

bool HookDevicePresentation(IDirect3DDevice9 *device) {
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

void OnD3D9DeviceCreated(IDirect3DDevice9 *device) {
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

void HookIDirect3D9CreateDevice(IDirect3D9 *d3d) {
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

IDirect3D9 *WINAPI Direct3DCreate9Hook(UINT sdkVersion) {
    if (!Direct3DCreate9Original) {
        return nullptr;
    }

    const auto d3d = Direct3DCreate9Original(sdkVersion);
    if (d3d) {
        HookIDirect3D9CreateDevice(d3d);
    }

    return d3d;
}

static void RenderModOverlay(IDirect3DDevice9 *device) {
    if (EngineInternal::hostedMode.load() || !EngineInternal::modReady) {
        return;
    }

    PumpMainThreadTasks();
    DispatchRenderSceneCallbacks(device);
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
} // namespace EnginePresentationInternal

