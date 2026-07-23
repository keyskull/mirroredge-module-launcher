#pragma once

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <vector>

#include <d3d9.h>

#include "engine.h"

namespace EnginePresentationInternal {

struct RenderSceneState {
	std::vector<RenderSceneCallback> Callbacks;
	std::mutex Mutex;
	HRESULT(WINAPI *Original)(IDirect3DDevice9 *) = nullptr;
};

struct PresentSceneState {
	HRESULT(WINAPI *Original)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND,
	                          const RGNDATA *) = nullptr;
};

struct WindowState {
	bool BlockInput = false;
	byte KeysDown[0x100] = {0};
	std::vector<InputCallback> InputCallbacks;
	std::vector<InputCallback> SuperInputCallbacks;

	HWND Window = nullptr;
	WNDPROC WndProc = nullptr;
	BOOL(WINAPI *PeekMessage)(LPMSG, HWND, UINT, UINT, UINT) = nullptr;
	BOOL(WINAPI *GetMessage)(LPMSG, HWND, UINT, UINT) = nullptr;
};

extern RenderSceneState renderScene;
extern PresentSceneState presentScene;
extern WindowState window;

extern std::atomic<bool> presentationHooksInstalled;
extern std::mutex presentationHookMutex;
extern IDirect3DDevice9 *capturedDevice;
extern IDirect3DDevice9 *bootstrapDevice;
extern IDirect3DDevice9 *g_presentationHookDevice;

extern Engine::MainThreadTask deferredInitTask;
extern std::atomic<bool> deferredInitRan;
extern std::atomic<bool> deferredInitQueued;
extern std::atomic<int> presentStableFrames;
extern std::atomic<int> focusCooldownFrames;
extern std::atomic<int> preModFocusCooldown;
extern std::atomic<bool> gameForegroundSinceInject;
extern HRESULT lastCooperativeLevel;
extern DWORD injectTick;

extern IDirect3D9 *(WINAPI *Direct3DCreate9Original)(UINT);
extern std::atomic<bool> d3d9ExportHooked;
extern std::atomic<bool> rendererManagedByProxy;

extern std::mutex mainThreadTaskMutex;
extern std::vector<void (*)()> mainThreadTasks;

HWND GetGameWindowHint();
bool TryLazyPresentationHook();
void NotifyFocusTransition();
void UpdateDeviceStability(IDirect3DDevice9 *device);
bool IsDeviceReadyForOverlay(IDirect3DDevice9 *device);
bool InstallPresentationHooksInternal(IDirect3DDevice9 *device);
IDirect3DDevice9 *FindExistingD3D9Device();
bool HookDevicePresentation(IDirect3DDevice9 *device);
void OnD3D9DeviceCreated(IDirect3DDevice9 *device);
void HookIDirect3D9CreateDevice(IDirect3D9 *d3d);
IDirect3D9 *WINAPI Direct3DCreate9Hook(UINT sdkVersion);
bool IsModD3D9ProxyModule(HMODULE module);

HRESULT WINAPI EndSceneHook(IDirect3DDevice9 *device);
HRESULT WINAPI PresentHook(IDirect3DDevice9 *device, const RECT *sourceRect,
                           const RECT *destRect, HWND destWindowOverride,
                           const RGNDATA *dirtyRegion);

void HandleMessage(HWND hWnd, UINT &msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void PumpMainThreadTasks();
void HandleFocusMessage(LPMSG lpMsg);
void PumpMessageBootstrap();
void HandleModInputMessage(LPMSG lpMsg);
BOOL WINAPI PeekMessageHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                            UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI GetMessageHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                           UINT wMsgFilterMax);
bool InstallPeekMessageBootstrapImpl();

} // namespace EnginePresentationInternal
