#pragma once

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <vector>

#include <d3d9.h>

#include "presentation.h"

struct ImDrawData;

namespace PresentationInternal {

enum VTableIndex { kPresent = 17, kEndScene = 42 };
enum {
	kLazyHookDelayMs = 12000,
	kLazyHookDelayBorderlessMs = 500,
	kProxyHookSettleMs = 12000,
	kHookRetryIntervalMs = 1000,
	kHookRetryIntervalProxyMs = 100,
	kImGuiInitFramesRequired = 4,
	kStableFramesRequired = 20,
	kMaxPendingImGuiMessages = 32
};

struct WindowState {
	HWND hwnd = nullptr;
	bool blockInput = false;
	BOOL(WINAPI *PeekMessage)(LPMSG, HWND, UINT, UINT, UINT) = nullptr;
	BOOL(WINAPI *GetMessage)(LPMSG, HWND, UINT, UINT) = nullptr;
};

struct RenderState {
	HRESULT(WINAPI *EndSceneOriginal)(IDirect3DDevice9 *) = nullptr;
	HRESULT(WINAPI *PresentOriginal)(IDirect3DDevice9 *, const RECT *, const RECT *,
	                                HWND, const RGNDATA *) = nullptr;
};

extern WindowState g_window;
extern RenderState g_render;

extern std::atomic<bool> g_bootstrapInstalled;
extern std::atomic<bool> g_hooksInstalled;
extern std::atomic<bool> g_imguiReady;
extern std::atomic<int> g_focusCooldown;
extern std::atomic<int> g_preModFocusCooldown;
extern std::atomic<int> g_stableFrames;
extern std::atomic<DWORD> g_injectTick;
extern std::atomic<bool> g_gameForegroundSinceInject;

extern std::mutex g_presentationHookMutex;
extern std::mutex g_taskMutex;
extern std::vector<Presentation::MainThreadTask> g_tasks;
extern std::mutex g_imguiMutex;

extern IDirect3DDevice9 *g_cachedDevice;
extern std::atomic<int> g_endSceneCalls;
extern std::atomic<int> g_presentCalls;
extern std::atomic<bool> g_lazyGateLogged;
extern std::atomic<bool> g_deviceMissLogged;
extern std::atomic<bool> g_proxyDeviceReceived;
extern std::atomic<DWORD> g_proxyDeviceTick;
extern std::atomic<int> g_postFocusWatch;
extern std::atomic<bool> g_pendingFocusSync;
extern std::atomic<bool> g_pendingMenuHide;
extern std::atomic<bool> g_pendingImGuiReset;
extern std::atomic<bool> g_pendingImGuiDeviceInvalidate;
extern std::atomic<DWORD> g_lastCharMessageTick;
extern std::mutex g_imguiEventMutex;
extern std::vector<MSG> g_pendingImGuiMessages;

extern HRESULT lastCoop;
extern DWORD g_lastHookAttemptTick;

HWND FindGameWindow();
HWND GetGameWindow();
HWND GetHookWindowHint(IDirect3DDevice9 *device);
bool IsOurProcessForeground();
void UpdateForegroundHeuristic();
bool IsDeviceCooperativeReady(IDirect3DDevice9 *device);

void NotifyFocusTransition(IDirect3DDevice9 *device);
void UpdateStability(IDirect3DDevice9 *device);
bool CanRenderOverlay(IDirect3DDevice9 *device);

void SyncImGuiDisplaySize();
void EnsureImGui(IDirect3DDevice9 *device);
void ApplyImGuiInputReset();
void ClearPendingImGuiMessages();
void PollImGuiMouseButtons();
void PollImGuiKeyboardInput();
void InvalidateImGuiDeviceObjects();
bool RenderImGuiDrawDataSafe(ImDrawData *drawData);
void ProcessDeferredFocusLoss();
void TryInvalidateImGuiBeforeDeviceReset(IDirect3DDevice9 *device);

void InvalidateImGuiDeviceObjectsNow(const char *reason, const char *hypothesisId);
void ReleaseGameCapture();
void ProcessLostFrameSideEffects(IDirect3DDevice9 *device);
void ProcessImGuiRenderThreadEvents(IDirect3DDevice9 *device);
void PollUnfocusInputRelease();
void SyncInputBlockWithForeground(IDirect3DDevice9 *device);

void PumpTasks();
bool TryInstallHooks();

void HandleFocus(LPMSG msg);
void HandleInput(LPMSG msg);
void PumpPreHookBootstrap();

HRESULT WINAPI ModuleManager_EndScene(IDirect3DDevice9 *device);
HRESULT WINAPI ModuleManager_Present(IDirect3DDevice9 *device, const RECT *sourceRect,
                                     const RECT *destRect, HWND destWindowOverride,
                                     const RGNDATA *dirtyRegion);

BOOL WINAPI PeekMessageHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                            UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI GetMessageHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                           UINT wMsgFilterMax);

} // namespace PresentationInternal

void HostMenu_SetBlockInput(bool block);
