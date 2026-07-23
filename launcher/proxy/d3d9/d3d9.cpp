#include <windows.h>
#include <unknwn.h>
#include <d3d9types.h>
#include <psapi.h>

#include <stdio.h>
#include <string.h>

#include "module_contract.h"
#include "debug_log.h"
#include "window_layout_settings.h"

struct IDirect3D9;
struct IDirect3DDevice9;

// Forward declarations
static void ProxyDebugLog(const char *message, uintptr_t a, uintptr_t b);

// RDP bypass removed (KI-2026-011: all attempted approaches failed — IAT patching,
// inline hooking). Use an alternative remote solution (Parsec, Moonlight, Steam
// Remote Play) or physical console.

typedef IDirect3D9 *(WINAPI *Direct3DCreate9Fn)(UINT);
typedef HRESULT(STDMETHODCALLTYPE *CreateDeviceFn)(
    IDirect3D9 *, UINT, unsigned int, HWND, DWORD,
    D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef void(__stdcall *MmOnD3D9DeviceCreatedFn)(IDirect3DDevice9 *);

enum { IDirect3D9_CreateDevice = 16 };

#ifndef LOAD_WITH_ALTERED_SEARCH_PATH
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x00000008
#endif

// #region agent log
static void ProxyDebugLog(const char *message, uintptr_t a, uintptr_t b);
// #endregion

static HMODULE LoadLibraryFromPath(const wchar_t *path) {
	using LoadLibraryExWFn = HMODULE(WINAPI *)(LPCWSTR, HANDLE, DWORD);
	HMODULE module = nullptr;
	__try {
		const auto loadEx = reinterpret_cast<LoadLibraryExWFn>(
		    GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryExW"));
		if (loadEx) {
			module = loadEx(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		} else {
			module = LoadLibraryW(path);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ProxyDebugLog("load_library_crash", GetExceptionCode(), 0);
		return nullptr;
	}
	return module;
}

static HMODULE realD3D9 = nullptr;
static Direct3DCreate9Fn realDirect3DCreate9 = nullptr;
static CreateDeviceFn realCreateDevice = nullptr;
static HMODULE modDll = nullptr;
static IDirect3DDevice9 *g_lastDevice = nullptr;
static CRITICAL_SECTION g_deviceLock;
static bool g_deviceLockReady = false;
static D3DPRESENT_PARAMETERS g_cachedPresentParams = {};
static bool g_hasCachedPresentParams = false;

// #region agent log
static void ProxyDebugLog(const char *message, uintptr_t a = 0, uintptr_t b = 0) {
	AgentDebugLog("d3d9proxy", "d3d9.cpp", message, "P", a, b, 0, 0);
}
// #endregion

static void EnsureDeviceLock() {
	if (!g_deviceLockReady) {
		InitializeCriticalSection(&g_deviceLock);
		g_deviceLockReady = true;
	}
}

static void StoreLastDevice(IDirect3DDevice9 *device) {
	if (!device) {
		return;
	}

	EnsureDeviceLock();
	EnterCriticalSection(&g_deviceLock);
	if (g_lastDevice) {
		reinterpret_cast<IUnknown *>(g_lastDevice)->Release();
		g_lastDevice = nullptr;
	}
	g_lastDevice = device;
	reinterpret_cast<IUnknown *>(g_lastDevice)->AddRef();
	LeaveCriticalSection(&g_deviceLock);
}

static MmOnD3D9DeviceCreatedFn ResolveDeviceNotify(HMODULE mod) {
	if (!mod) {
		return nullptr;
	}

	auto notify = reinterpret_cast<MmOnD3D9DeviceCreatedFn>(
	    GetProcAddress(mod, "MmOnD3D9DeviceCreated"));
	if (!notify) {
		notify = reinterpret_cast<MmOnD3D9DeviceCreatedFn>(
		    GetProcAddress(mod, "_MmOnD3D9DeviceCreated@4"));
	}
	return notify;
}

static bool NotifyModule(HMODULE mod, IDirect3DDevice9 *device) {
	const auto notify = ResolveDeviceNotify(mod);
	if (!notify) {
		return false;
	}

	ProxyDebugLog("notify_call", reinterpret_cast<uintptr_t>(device),
	              reinterpret_cast<uintptr_t>(mod));
	__try {
		notify(device);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ProxyDebugLog("notify_crash", GetExceptionCode(),
		              reinterpret_cast<uintptr_t>(mod));
		return false;
	}
	return true;
}

static void TryNotifyCachedDevice(HMODULE managerMod) {
	if (!managerMod) {
		return;
	}

	EnsureDeviceLock();
	EnterCriticalSection(&g_deviceLock);
	const auto device = g_lastDevice;
	if (device) {
		reinterpret_cast<IUnknown *>(device)->AddRef();
	}
	LeaveCriticalSection(&g_deviceLock);

	if (!device) {
		return;
	}

	if (NotifyModule(managerMod, device)) {
		ProxyDebugLog("notify_cached_device",
		              reinterpret_cast<uintptr_t>(device));
	}
	reinterpret_cast<IUnknown *>(device)->Release();
}

static HMODULE GetRealD3D9() {
	if (!realD3D9) {
		wchar_t path[MAX_PATH] = {};
		GetSystemDirectoryW(path, MAX_PATH);
		wcscat(path, L"\\d3d9.dll");
		realD3D9 = LoadLibraryW(path);

		if (realD3D9) {
			realDirect3DCreate9 = reinterpret_cast<Direct3DCreate9Fn>(
			    GetProcAddress(realD3D9, "Direct3DCreate9"));
		}
	}
	return realD3D9;
}

static bool BuildProxyModulePath(const wchar_t *relativePath, wchar_t *path,
                                 DWORD pathChars) {
	const HMODULE bases[] = {GetModuleHandleW(L"d3d9.dll"),
	                         GetModuleHandleW(nullptr)};
	for (const auto base : bases) {
		if (!base) {
			continue;
		}
		if (!GetModuleFileNameW(base, path, pathChars)) {
			continue;
		}
		auto slash = wcsrchr(path, L'\\');
		if (!slash) {
			continue;
		}
		*slash = L'\0';
		wcscat(path, relativePath);
		path[pathChars - 1] = L'\0';

		wchar_t full[MAX_PATH] = {};
		if (GetFullPathNameW(path, MAX_PATH, full, nullptr)) {
			wcscpy(path, full);
		}
		return true;
	}
	return false;
}

static bool BuildManagerPath(wchar_t *path, DWORD pathChars) {
	return BuildProxyModulePath(MMOD_MANAGER_PROXY_LOAD_PATH, path, pathChars);
}

static HMODULE LoadModuleManagerMod();

static volatile LONG g_managerPreloadScheduled = 0;

static DWORD WINAPI PreloadManagerWorker(LPVOID) {
	ProxyDebugLog("preload_manager_worker");
	LoadModuleManagerMod();
	ProxyDebugLog("preload_manager_done");
	return 0;
}

static void ScheduleManagerPreload() {
	if (InterlockedCompareExchange(&g_managerPreloadScheduled, 1, 0) != 0) {
		return;
	}

	const auto thread =
	    CreateThread(nullptr, 0, PreloadManagerWorker, nullptr, 0, nullptr);
	if (thread) {
		CloseHandle(thread);
		ProxyDebugLog("preload_manager_scheduled");
		return;
	}

	InterlockedExchange(&g_managerPreloadScheduled, 0);
	LoadModuleManagerMod();
}

static HMODULE LoadModuleManagerMod() {
	static HMODULE managerDll = nullptr;
	if (managerDll) {
		TryNotifyCachedDevice(managerDll);
		return managerDll;
	}

	wchar_t path[MAX_PATH] = {};
	if (!BuildManagerPath(path, MAX_PATH)) {
		ProxyDebugLog("load_manager_path_fail");
		return nullptr;
	}

	managerDll = LoadLibraryFromPath(path);
	ProxyDebugLog("load_manager", reinterpret_cast<uintptr_t>(managerDll),
	              managerDll ? 0u : GetLastError());
	if (managerDll) {
		TryNotifyCachedDevice(managerDll);
	}
	return managerDll;
}

static HMODULE LoadMmultiplayerMod() {
	if (modDll) {
		return modDll;
	}

	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(GetModuleHandleW(nullptr), path, MAX_PATH);
	auto slash = wcsrchr(path, L'\\');
	if (!slash) {
		return nullptr;
	}
	*slash = L'\0';
	wcscat(path, MMOD_PROXY_LOAD_PATH);
	path[MAX_PATH - 1] = L'\0';
	modDll = LoadLibraryFromPath(path);
	return modDll;
}

static void NotifyDeviceCreatedImpl(IDirect3DDevice9 *device) {
	if (!device) {
		return;
	}

	const auto managerMod = LoadModuleManagerMod();
	if (managerMod && NotifyModule(managerMod, device)) {
		return;
	}

	static const wchar_t *kModNames[] = {L"module_manager.dll",
	                                     MMOD_DLL_FILENAME};
	for (const auto modName : kModNames) {
		const auto mod = GetModuleHandleW(modName);
		if (!mod) {
			continue;
		}

		if (NotifyModule(mod, device)) {
			return;
		}
	}

	ProxyDebugLog("notify_missing_export", reinterpret_cast<uintptr_t>(device),
	              reinterpret_cast<uintptr_t>(managerMod));
}

struct DeviceNotifyParam {
	IDirect3DDevice9 *device = nullptr;
};

static DWORD WINAPI DeviceNotifyWorker(LPVOID param) {
	const auto ctx = reinterpret_cast<DeviceNotifyParam *>(param);
	const auto device = ctx->device;
	delete ctx;

	ProxyDebugLog("notify_worker_start", reinterpret_cast<uintptr_t>(device));
	NotifyDeviceCreatedImpl(device);
	reinterpret_cast<IUnknown *>(device)->Release();
	ProxyDebugLog("notify_worker_done");
	return 0;
}

static void NotifyDeviceCreated(IDirect3DDevice9 *device) {
	if (!device) {
		return;
	}

	ProxyDebugLog("create_device_ok", reinterpret_cast<uintptr_t>(device));
	reinterpret_cast<IUnknown *>(device)->AddRef();
	const auto ctx = new DeviceNotifyParam{device};
	const auto thread =
	    CreateThread(nullptr, 0, DeviceNotifyWorker, ctx, 0, nullptr);
	if (!thread) {
		ProxyDebugLog("notify_thread_fail", GetLastError());
		delete ctx;
		NotifyDeviceCreatedImpl(device);
		reinterpret_cast<IUnknown *>(device)->Release();
		return;
	}
	CloseHandle(thread);
}

static void PatchCreateDevicePresentation(HWND focusWindow,
                                          D3DPRESENT_PARAMETERS *params) {
	if (!params || !WindowLayout_IsEnabled()) {
		return;
	}

	RECT monitorRect = {};
	if (!WindowLayout_GetMonitorRect(
	        focusWindow ? focusWindow : GetDesktopWindow(), monitorRect)) {
		params->Windowed = TRUE;
		params->FullScreen_RefreshRateInHz = 0;
		return;
	}

	RECT targetRect = {};
	int width = 0;
	int height = 0;
	const HWND hwnd = focusWindow ? focusWindow : GetDesktopWindow();
	if (!WindowLayout_ResolveRenderResolution(hwnd, WindowLayout_GetScale(),
	                                          width, height)) {
		if (!WindowLayout_ComputeWindowSize(hwnd, WindowLayout_GetScale(), width,
		                                    height)) {
			WindowLayout_ComputeTargetRect(monitorRect, WindowLayout_GetScale(),
			                               targetRect);
			width = targetRect.right - targetRect.left;
			height = targetRect.bottom - targetRect.top;
		}
	}

	params->Windowed = TRUE;
	params->BackBufferWidth = static_cast<UINT>(width);
	params->BackBufferHeight = static_cast<UINT>(height);
	params->FullScreen_RefreshRateInHz = 0;

	params->hDeviceWindow = focusWindow;
	params->BackBufferFormat = D3DFMT_UNKNOWN;
	params->SwapEffect = D3DSWAPEFFECT_DISCARD;
	params->Flags = 0;

	ProxyDebugLog("create_device_windowed", static_cast<uintptr_t>(width),
	              static_cast<uintptr_t>(height));
}

static HRESULT STDMETHODCALLTYPE ProxyCreateDevice(
    IDirect3D9 *self, UINT adapter, unsigned int deviceType, HWND focusWindow,
    DWORD behaviorFlags, D3DPRESENT_PARAMETERS *presentationParameters,
    IDirect3DDevice9 **returnedDeviceInterface) {

	ProxyDebugLog("create_device_enter", reinterpret_cast<uintptr_t>(focusWindow));

	if (!realCreateDevice) {
		return static_cast<HRESULT>(0x88760869L);
	}

	if (presentationParameters) {
		g_cachedPresentParams = *presentationParameters;
		g_hasCachedPresentParams = true;
		PatchCreateDevicePresentation(focusWindow, presentationParameters);
	}

	const auto hr = realCreateDevice(self, adapter, deviceType, focusWindow,
	                                 behaviorFlags, presentationParameters,
	                                 returnedDeviceInterface);

	ProxyDebugLog("create_device_return", static_cast<uintptr_t>(hr),
	              reinterpret_cast<uintptr_t>(returnedDeviceInterface ?
	                                              *returnedDeviceInterface :
	                                              nullptr));

	if (SUCCEEDED(hr) && focusWindow && WindowLayout_IsEnabled()) {
		WindowLayout_ApplyToWindow(focusWindow);
	}

	if (SUCCEEDED(hr) && returnedDeviceInterface &&
	    *returnedDeviceInterface) {
		StoreLastDevice(*returnedDeviceInterface);
		NotifyDeviceCreated(*returnedDeviceInterface);
	}

	return hr;
}

static void HookCreateDevice(IDirect3D9 *d3d) {
	if (!d3d) {
		return;
	}

	const auto vtable = *reinterpret_cast<void ***>(d3d);
	if (!vtable || !vtable[IDirect3D9_CreateDevice]) {
		return;
	}

	// If this specific vtable entry is already our proxy, skip re-hooking.
	// Multiple IDirect3D9 instances share the same vtable (per COM class),
	// so this guard handles both the first hook and subsequent no-ops
	// without blocking hooks on different D3D9 classes.
	if (reinterpret_cast<void *>(ProxyCreateDevice) ==
	    vtable[IDirect3D9_CreateDevice]) {
		return;
	}

	// Capture the real CreateDevice once (all IDirect3D9 vtable entries
	// ultimately point to the same d3d9.dll implementation).
	if (!realCreateDevice) {
		realCreateDevice = reinterpret_cast<CreateDeviceFn>(
		    vtable[IDirect3D9_CreateDevice]);
	}

	DWORD protect = 0;
	if (!VirtualProtect(&vtable[IDirect3D9_CreateDevice], sizeof(void *),
	                    PAGE_READWRITE, &protect)) {
		return;
	}

	vtable[IDirect3D9_CreateDevice] =
	    reinterpret_cast<void *>(ProxyCreateDevice);
	VirtualProtect(&vtable[IDirect3D9_CreateDevice], sizeof(void *), protect,
	               &protect);
}

extern "C" __declspec(dllexport) BOOL __stdcall
MmProxyGetCachedPresentParameters(D3DPRESENT_PARAMETERS *params) {
	if (!params || !g_hasCachedPresentParams) {
		return FALSE;
	}

	*params = g_cachedPresentParams;
	return TRUE;
}

extern "C" __declspec(dllexport) void __stdcall MmProxyRetryDeviceNotify() {
	const auto loaded = GetModuleHandleW(L"module_manager.dll");
	if (loaded) {
		TryNotifyCachedDevice(loaded);
		return;
	}

	TryNotifyCachedDevice(LoadModuleManagerMod());
}

extern "C" __declspec(dllexport) IDirect3D9 *WINAPI
Direct3DCreate9(UINT sdkVersion) {
	if (!GetRealD3D9() || !realDirect3DCreate9) {
		return nullptr;
	}

	ProxyDebugLog("direct3d_create9");
	const auto d3d = realDirect3DCreate9(sdkVersion);
	if (d3d) {
		HookCreateDevice(d3d);
		ProxyDebugLog("create_device_hooked",
		              reinterpret_cast<uintptr_t>(realCreateDevice));
		// Borderless boot can sit on splash/movies for a long time before
		// CreateDevice. Preload module_manager asynchronously so InitWorker can
		// signal module_manager_ready without blocking the game's D3D9 init.
		ScheduleManagerPreload();
	}

	return d3d;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(module);
		EnsureDeviceLock();
	} else if (reason == DLL_PROCESS_DETACH && g_deviceLockReady) {
		EnterCriticalSection(&g_deviceLock);
		if (g_lastDevice) {
			reinterpret_cast<IUnknown *>(g_lastDevice)->Release();
			g_lastDevice = nullptr;
		}
		LeaveCriticalSection(&g_deviceLock);
		DeleteCriticalSection(&g_deviceLock);
		g_deviceLockReady = false;
	}
	return TRUE;
}
