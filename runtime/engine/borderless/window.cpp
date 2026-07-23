#include "borderless.h"

#include "debug_trace.h"
#include "detail.h"
#include "mod_host_api.h"
#include "viewport.h"
#include "window_layout_settings.h"
#include "me_sdk/runtime/init.h"

#include <Windows.h>
#include <ShlObj.h>
#include <Shlwapi.h>

#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace {

const ModHostApi *g_host = nullptr;

void InvalidateOverlay() {
	if (g_host && g_host->InvalidateOverlayGraphics) {
		g_host->InvalidateOverlayGraphics();
	}
}

void CreateOverlay() {
	if (g_host && g_host->CreateOverlayGraphics) {
		g_host->CreateOverlayGraphics();
	}
}

void SyncOverlayDisplaySize(int width, int height) {
	if (g_host && g_host->SetOverlayDisplaySize) {
		g_host->SetOverlayDisplaySize(width, height);
	}
}

using namespace EngineBorderless::Detail;

enum { kDeviceReset = 16 };
constexpr int kPositionTolerancePx = 4;

bool g_hooksInstalled = false;
DWORD g_lastAttemptTick = 0;
int g_bootResX = 0;
int g_bootResY = 0;
int g_syncedResX = 0;
int g_syncedResY = 0;
bool g_bootResCaptured = false;
HWND g_gameHwnd = nullptr;

HRESULT(STDMETHODCALLTYPE *g_resetOriginal)(IDirect3DDevice9 *,
                                            D3DPRESENT_PARAMETERS *) = nullptr;

void LoadSettings() {
	g_windowScale = WindowLayout_GetScale();
	g_enabled = WindowLayout_IsEnabled();
}

bool GetTdEngineIniPath(wchar_t *path, size_t pathChars) {
	if (!path || pathChars == 0) {
		return false;
	}

	wchar_t documents[MAX_PATH] = {};
	if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr,
	                            SHGFP_TYPE_CURRENT, documents))) {
		return false;
	}

	static const wchar_t *kCandidates[] = {
	    L"\\EA Games\\Mirror's Edge\\TdGame\\Config\\TdEngine.ini",
	    L"\\EA Games\\Mirrors Edge\\TdGame\\Config\\TdEngine.ini",
	};

	for (const auto *suffix : kCandidates) {
		swprintf(path, pathChars, L"%s%s", documents, suffix);
		if (PathFileExistsW(path)) {
			return true;
		}
	}

	swprintf(path, pathChars, L"%s%s", documents, kCandidates[0]);
	return true;
}

void CaptureBootEngineResolution() {
	if (g_bootResCaptured) {
		return;
	}

	wchar_t iniPath[MAX_PATH] = {};
	if (!GetTdEngineIniPath(iniPath, _countof(iniPath))) {
		return;
	}

	g_bootResX = GetPrivateProfileIntW(L"SystemSettings", L"ResX", 0, iniPath);
	g_bootResY = GetPrivateProfileIntW(L"SystemSettings", L"ResY", 0, iniPath);
	if (g_bootResX < 1) {
		g_bootResX = 1920;
	}
	if (g_bootResY < 1) {
		g_bootResY = 1080;
	}
	g_bootResCaptured = true;
}

void SyncTdEngineResolution(int resX, int resY) {
	if (resX < 1 || resY < 1 || (resX == g_syncedResX && resY == g_syncedResY)) {
		return;
	}

	wchar_t iniPath[MAX_PATH] = {};
	if (!GetTdEngineIniPath(iniPath, _countof(iniPath))) {
		return;
	}

	wchar_t resXText[16] = {};
	wchar_t resYText[16] = {};
	swprintf(resXText, 16, L"%d", resX);
	swprintf(resYText, 16, L"%d", resY);
	WritePrivateProfileStringW(L"SystemSettings", L"ResX", resXText, iniPath);
	WritePrivateProfileStringW(L"SystemSettings", L"ResY", resYText, iniPath);
	WritePrivateProfileStringW(L"SystemSettings", L"Fullscreen", L"False",
	                           iniPath);
	g_syncedResX = resX;
	g_syncedResY = resY;
}

void SyncWindowSettingsResolution(int resX, int resY) {
	if (resX < 1 || resY < 1) {
		return;
	}

	const auto path = WindowLayoutSettingsPathA();
	char resXText[16] = {};
	char resYText[16] = {};
	snprintf(resXText, sizeof(resXText), "%d", resX);
	snprintf(resYText, sizeof(resYText), "%d", resY);
	WritePrivateProfileStringA("Window", "ResX", resXText, path);
	WritePrivateProfileStringA("Window", "ResY", resYText, path);
}

bool BuildClientScreenClip(HWND hwnd, RECT &screenClip) {
	RECT client = {};
	if (!hwnd || !GetClientRect(hwnd, &client)) {
		return false;
	}

	POINT topLeft = {client.left, client.top};
	POINT bottomRight = {client.right, client.bottom};
	ClientToScreen(hwnd, &topLeft);
	ClientToScreen(hwnd, &bottomRight);
	screenClip = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
	return screenClip.right > screenClip.left && screenClip.bottom > screenClip.top;
}

bool GetWindowClientSize(HWND hwnd, int &width, int &height) {
	RECT client = {};
	if (!hwnd || !GetClientRect(hwnd, &client)) {
		return false;
	}

	width = client.right - client.left;
	height = client.bottom - client.top;
	return width > 0 && height > 0;
}

bool HasWindowBorder(HWND hwnd) {
	const auto style = GetWindowLongW(hwnd, GWL_STYLE);
	return (style & (WS_CAPTION | WS_THICKFRAME | WS_BORDER)) != 0;
}

bool RectsNear(const RECT &left, const RECT &right) {
	return abs(left.left - right.left) <= kPositionTolerancePx &&
	       abs(left.top - right.top) <= kPositionTolerancePx &&
	       abs(left.right - right.right) <= kPositionTolerancePx &&
	       abs(left.bottom - right.bottom) <= kPositionTolerancePx;
}

bool IsAtTargetLayout(HWND hwnd, float scale) {
	RECT monitorRect = {};
	RECT windowRect = {};
	RECT targetRect = {};
	if (!WindowLayout_GetMonitorRect(hwnd, monitorRect) ||
	    !GetWindowRect(hwnd, &windowRect)) {
		return false;
	}

	int targetW = 0;
	int targetH = 0;
	if (WindowLayout_ComputeWindowSize(hwnd, scale, targetW, targetH)) {
		const int x =
		    monitorRect.left + (monitorRect.right - monitorRect.left - targetW) / 2;
		const int y =
		    monitorRect.top + (monitorRect.bottom - monitorRect.top - targetH) / 2;
		targetRect.left = x;
		targetRect.top = y;
		targetRect.right = x + targetW;
		targetRect.bottom = y + targetH;
	} else {
		WindowLayout_ComputeTargetRect(monitorRect, scale, targetRect);
	}

	return !HasWindowBorder(hwnd) && RectsNear(windowRect, targetRect);
}

bool StripWindowChrome(HWND hwnd) {
	return WindowLayout_StripWindowChrome(hwnd);
}

bool ApplyWindowFrame(HWND hwnd, float scale) {
	if (!hwnd || !WindowLayout_IsEnabled()) {
		return false;
	}

	RECT monitorRect = {};
	if (!WindowLayout_GetMonitorRect(hwnd, monitorRect)) {
		return false;
	}

	int width = 0;
	int height = 0;
	if (!WindowLayout_ComputeWindowSize(hwnd, scale, width, height)) {
		RECT targetRect = {};
		WindowLayout_ComputeTargetRect(monitorRect, scale, targetRect);
		width = targetRect.right - targetRect.left;
		height = targetRect.bottom - targetRect.top;
	}

	WindowLayout_StripWindowChrome(hwnd);
	const int x =
	    monitorRect.left + (monitorRect.right - monitorRect.left - width) / 2;
	const int y =
	    monitorRect.top + (monitorRect.bottom - monitorRect.top - height) / 2;
	return SetWindowPos(hwnd, HWND_TOP, x, y, width, height,
	                    SWP_FRAMECHANGED | SWP_SHOWWINDOW) != FALSE;
}

HWND ResolveGameWindow(IDirect3DDevice9 *device) {
	if (!device) {
		return nullptr;
	}

	D3DDEVICE_CREATION_PARAMETERS params = {};
	if (SUCCEEDED(device->GetCreationParameters(&params)) &&
	    params.hFocusWindow && IsWindow(params.hFocusWindow)) {
		return params.hFocusWindow;
	}

	return nullptr;
}

bool TryGetBackBufferSize(IDirect3DDevice9 *device, int &width, int &height) {
	if (!device) {
		return false;
	}

	IDirect3DSurface9 *backBuffer = nullptr;
	if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) {
		return false;
	}

	D3DSURFACE_DESC desc = {};
	const HRESULT hr = backBuffer->GetDesc(&desc);
	backBuffer->Release();
	if (FAILED(hr)) {
		return false;
	}

	width = static_cast<int>(desc.Width);
	height = static_cast<int>(desc.Height);
	return width > 0 && height > 0;
}

void SyncImGuiDisplaySize(int width, int height) {
	SyncOverlayDisplaySize(width, height);
}

bool BackBufferMatchesTarget(IDirect3DDevice9 *device, int width, int height) {
	int backBufferWidth = 0;
	int backBufferHeight = 0;
	if (!TryGetBackBufferSize(device, backBufferWidth, backBufferHeight)) {
		return false;
	}

	return abs(backBufferWidth - width) <= 1 &&
	       abs(backBufferHeight - height) <= 1;
}

bool HookVTableEntry(void **vtable, unsigned index, void *hook, void **original) {
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

void **GetDeviceVtable(IDirect3DDevice9 *device) {
	if (!device) {
		return nullptr;
	}
	return *reinterpret_cast<void ***>(device);
}

bool TryGetCachedPresentParameters(D3DPRESENT_PARAMETERS &params) {
	const auto d3d9 = GetModuleHandleW(L"d3d9.dll");
	if (!d3d9) {
		return false;
	}

	using GetCachedFn = BOOL(__stdcall *)(D3DPRESENT_PARAMETERS *);
	const auto getCached = reinterpret_cast<GetCachedFn>(
	    GetProcAddress(d3d9, "MmProxyGetCachedPresentParameters"));
	if (!getCached) {
		return false;
	}

	return getCached(&params) != FALSE;
}

void FillDefaultPresentParameters(D3DPRESENT_PARAMETERS &params) {
	memset(&params, 0, sizeof(params));
	params.BackBufferCount = 1;
	params.MultiSampleType = D3DMULTISAMPLE_NONE;
	params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	params.EnableAutoDepthStencil = TRUE;
	params.AutoDepthStencilFormat = D3DFMT_D24S8;
	params.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
}

void FillPresentParametersForLayout(D3DPRESENT_PARAMETERS &params, HWND hwnd,
                                    int width, int height) {
	if (!TryGetCachedPresentParameters(params)) {
		FillDefaultPresentParameters(params);
	}

	params.Windowed = TRUE;
	params.hDeviceWindow = hwnd;
	params.BackBufferWidth = static_cast<UINT>(width);
	params.BackBufferHeight = static_cast<UINT>(height);
	params.BackBufferFormat = D3DFMT_UNKNOWN;
	params.FullScreen_RefreshRateInHz = 0;
	params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	params.Flags = 0;
}

HRESULT ResetDeviceWindowed(IDirect3DDevice9 *device, HWND hwnd, int width,
                            int height) {
	if (!device || !hwnd || width < 1 || height < 1) {
		return E_INVALIDARG;
	}

	const HRESULT level = device->TestCooperativeLevel();
	if (level == D3DERR_DEVICELOST) {
		return level;
	}

	D3DPRESENT_PARAMETERS params = {};
	FillPresentParametersForLayout(params, hwnd, width, height);

	InvalidateOverlay();

	HRESULT hr = device->Reset(&params);
	if (hr == D3DERR_INVALIDCALL) {
		params.MultiSampleType = D3DMULTISAMPLE_NONE;
		params.MultiSampleQuality = 0;
		hr = device->Reset(&params);
	}

	if (SUCCEEDED(hr)) {
		CreateOverlay();
	}
	return hr;
}

} // namespace

namespace EngineBorderless {

void Initialize() {
	LoadSettings();
	CaptureBootEngineResolution();
}

void AppendStatusJson(std::string &out) {
	int viewportWidth = 0;
	int viewportHeight = 0;
	if (MeSdk::AreGlobalsReady()) {
		EngineBorderlessSync::QueryEngineViewportSize(viewportWidth, viewportHeight);
	}

	char buffer[192] = {};
	snprintf(buffer, sizeof(buffer),
	         ",\"windowLayoutEnabled\":%s,\"clientWidth\":%d,\"clientHeight\":%d,"
	         "\"backBufferWidth\":%d,\"backBufferHeight\":%d,"
	         "\"viewportWidth\":%d,\"viewportHeight\":%d,"
	         "\"mouseLookScale\":%.4f",
	         g_enabled ? "true" : "false", g_lastAppliedWidth, g_lastAppliedHeight,
	         g_lastBackBufferWidth, g_lastBackBufferHeight, viewportWidth,
	         viewportHeight, EngineBorderlessSync::GetLastMouseLookScale());
	out += buffer;
}

bool PatchPresentParameters(D3DPRESENT_PARAMETERS *params, HWND hwnd) {
	if (!g_enabled || !params || !hwnd) {
		return false;
	}

	int clientW = 0;
	int clientH = 0;
	if (!GetWindowClientSize(hwnd, clientW, clientH)) {
		if (!WindowLayout_ComputeWindowSize(hwnd, g_windowScale, clientW,
		                                    clientH)) {
			RECT monitorRect = {};
			RECT targetRect = {};
			if (!WindowLayout_GetMonitorRect(hwnd, monitorRect)) {
				return false;
			}
			WindowLayout_ComputeTargetRect(monitorRect, g_windowScale, targetRect);
			clientW = targetRect.right - targetRect.left;
			clientH = targetRect.bottom - targetRect.top;
		}
	}

	int renderW = 0;
	int renderH = 0;
	if (!WindowLayout_ResolveRenderResolution(hwnd, g_windowScale, renderW,
	                                          renderH)) {
		renderW = clientW;
		renderH = clientH;
	}

	D3DPRESENT_PARAMETERS patched = *params;
	FillPresentParametersForLayout(patched, hwnd, renderW, renderH);
	*params = patched;
	return true;
}

bool PatchResetParameters(D3DPRESENT_PARAMETERS *params, HWND hwnd) {
	return PatchPresentParameters(params, hwnd);
}

HRESULT STDMETHODCALLTYPE ResetHook(IDirect3DDevice9 *device,
                                      D3DPRESENT_PARAMETERS *params) {
	if (g_enabled && device && params) {
		const auto hwnd = ResolveGameWindow(device);
		PatchResetParameters(params, hwnd);
	}

	InvalidateOverlay();

	const HRESULT hr =
	    g_resetOriginal ? g_resetOriginal(device, params) : D3DERR_INVALIDCALL;

	if (SUCCEEDED(hr)) {
		CreateOverlay();
	}

	return hr;
}

void InstallDeviceHooks(IDirect3DDevice9 *device) {
	if (g_hooksInstalled || !device || !g_enabled) {
		return;
	}

	const auto vtable = GetDeviceVtable(device);
	if (!vtable || !vtable[kDeviceReset] || g_resetOriginal) {
		return;
	}

	if (!HookVTableEntry(vtable, kDeviceReset,
	                     reinterpret_cast<void *>(ResetHook),
	                     reinterpret_cast<void **>(&g_resetOriginal))) {
		return;
	}

	g_hooksInstalled = true;
	EngineDebugTrace::Log("engine: window layout Reset hook installed");
}

void Tick(IDirect3DDevice9 *device) {
	if (!g_enabled || !device) {
		return;
	}

	if (FAILED(device->TestCooperativeLevel())) {
		return;
	}

	InstallDeviceHooks(device);

	const HWND hwnd = ResolveGameWindow(device);
	if (!hwnd) {
		return;
	}

	g_gameHwnd = hwnd;
	if (!g_bootResCaptured) {
		CaptureBootEngineResolution();
	}

	RECT monitorRect = {};
	if (!WindowLayout_GetMonitorRect(hwnd, monitorRect)) {
		return;
	}

	int windowW = 0;
	int windowH = 0;
	if (!WindowLayout_ComputeWindowSize(hwnd, g_windowScale, windowW, windowH)) {
		RECT targetRect = {};
		WindowLayout_ComputeTargetRect(monitorRect, g_windowScale, targetRect);
		windowW = targetRect.right - targetRect.left;
		windowH = targetRect.bottom - targetRect.top;
	}

	int clientW = 0;
	int clientH = 0;
	if (!GetWindowClientSize(hwnd, clientW, clientH)) {
		clientW = windowW;
		clientH = windowH;
	}

	const DWORD now = GetTickCount();
	const bool layoutReady = IsAtTargetLayout(hwnd, g_windowScale);
	int renderW = 0;
	int renderH = 0;
	if (!WindowLayout_ResolveRenderResolution(hwnd, g_windowScale, renderW,
	                                          renderH)) {
		renderW = clientW;
		renderH = clientH;
	}

	const bool bufferReady =
	    BackBufferMatchesTarget(device, renderW, renderH);
	const bool needsHeavyWork = g_forceApply || !layoutReady || !bufferReady;

	if (needsHeavyWork) {
		// Throttle window resize + D3D Reset during boot; previously only
		// throttled when layoutReady, so every Present frame could Reset.
		const bool throttled =
		    !g_forceApply && now - g_lastAttemptTick < 500;
		if (!throttled) {
			g_lastAttemptTick = now;

			if (!layoutReady) {
				if (!ApplyWindowFrame(hwnd, g_windowScale)) {
					return;
				}
				g_d3dSynced = false;
			}

			if (!bufferReady || g_forceApply) {
				const HRESULT resetHr =
				    ResetDeviceWindowed(device, hwnd, renderW, renderH);
				if (FAILED(resetHr)) {
					if (resetHr != D3DERR_DEVICELOST && !g_resetFailLogged) {
						g_resetFailLogged = true;
						EngineDebugTrace::Logf(
						    "engine: window Reset failed hr=0x%08X "
						    "(render %dx%d, client %dx%d)",
						    static_cast<unsigned>(resetHr), renderW, renderH,
						    clientW, clientH);
					}
				} else {
					SyncImGuiDisplaySize(clientW, clientH);
					EngineBorderlessSync::TrySyncViewportResolution(renderW,
					                                                  renderH);
					g_resetFailLogged = false;
				}
			}

			g_d3dSynced =
			    bufferReady || BackBufferMatchesTarget(device, renderW, renderH);
			g_forceApply = false;

			g_lastAppliedWidth = clientW;
			g_lastAppliedHeight = clientH;
			TryGetBackBufferSize(device, g_lastBackBufferWidth,
			                     g_lastBackBufferHeight);

			if (!g_logged && layoutReady && g_d3dSynced) {
				g_logged = true;
				EngineDebugTrace::Logf(
				    "engine: window %.0f%% (%dx%d), render %dx%d, "
				    "back buffer %dx%d, boot engine %dx%d",
				    g_windowScale * 100.0f, g_lastAppliedWidth,
				    g_lastAppliedHeight, renderW, renderH,
				    g_lastBackBufferWidth, g_lastBackBufferHeight, g_bootResX,
				    g_bootResY);
			}
		}
	}

	if (layoutReady && bufferReady) {
		SyncTdEngineResolution(renderW, renderH);
		SyncWindowSettingsResolution(renderW, renderH);
		EngineBorderlessSync::TrySyncViewportResolution(renderW, renderH);
		EngineBorderlessSync::TryCompensateMouseLook(clientW, clientH, renderW,
		                                             renderH);
	}
}

void PumpInputSync() {
	if (!g_enabled || g_lastAppliedWidth < 1 || g_lastAppliedHeight < 1) {
		return;
	}

	static DWORD lastPumpTick = 0;
	const DWORD now = GetTickCount();
	if (lastPumpTick != 0 && now - lastPumpTick < 8) {
		return;
	}
	lastPumpTick = now;

	EngineBorderlessSync::TryCompensateMouseLook(g_lastAppliedWidth,
	                                             g_lastAppliedHeight,
	                                             g_lastBackBufferWidth,
	                                             g_lastBackBufferHeight);
}

void InstallHost(const ModHostApi *host) {
	if (host) {
		g_host = host;
	}
	Initialize();
	if (host && host->OnPresentationTick) {
		host->OnPresentationTick(Tick);
	}
	if (host && host->OnPresentationInputSync) {
		host->OnPresentationInputSync(PumpInputSync);
	}
}

bool QueryUiState(UiState &out) {
	using namespace Detail;
	out.enabled = g_enabled;
	out.scale = g_windowScale;
	out.clientWidth = g_lastAppliedWidth;
	out.clientHeight = g_lastAppliedHeight;
	out.backBufferWidth = g_lastBackBufferWidth;
	out.backBufferHeight = g_lastBackBufferHeight;
	return true;
}

void SetEnabled(bool enabled) {
	Detail::g_enabled = enabled;
	Detail::SaveSettings();
	Detail::MarkApply();
}

void SetScale(float scale) {
	Detail::g_windowScale = scale;
	Detail::SaveSettings();
	Detail::MarkApply();
}

void MarkApply() { Detail::MarkApply(); }

} // namespace EngineBorderless
