#pragma once

#include <Windows.h>

#include <cstdio>
#include <cstring>

inline const char *WindowLayoutSettingsPathA() {
	static char path[MAX_PATH] = {};
	if (!path[0]) {
		char temp[MAX_PATH] = {};
		GetTempPathA(static_cast<DWORD>(sizeof(temp)), temp);
		snprintf(path, sizeof(path), "%smodule_manager.settings.ini", temp);
	}
	return path;
}

inline bool WindowLayout_IsEnabled() {
	return GetPrivateProfileIntA("Window", "Enabled", 1,
	                             WindowLayoutSettingsPathA()) != 0;
}

inline float WindowLayout_GetScale() {
	char buffer[32] = {};
	GetPrivateProfileStringA("Window", "Scale", "0.5", buffer,
	                         static_cast<DWORD>(sizeof(buffer)),
	                         WindowLayoutSettingsPathA());
	float scale = static_cast<float>(atof(buffer));
	if (scale < 0.25f) {
		scale = 0.25f;
	}
	if (scale > 1.0f) {
		scale = 1.0f;
	}
	return scale;
}

inline bool WindowLayout_GetRenderResolution(int &resX, int &resY) {
	resX = GetPrivateProfileIntA("Window", "ResX", 0,
	                             WindowLayoutSettingsPathA());
	resY = GetPrivateProfileIntA("Window", "ResY", 0,
	                             WindowLayoutSettingsPathA());
	return resX > 0 && resY > 0;
}

inline bool WindowLayout_GetPrimaryMonitorSize(int &width, int &height) {
	POINT origin = {0, 0};
	const HMONITOR monitor =
	    MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
	if (!monitor) {
		width = GetSystemMetrics(SM_CXSCREEN);
		height = GetSystemMetrics(SM_CYSCREEN);
		return width > 0 && height > 0;
	}

	MONITORINFO monitorInfo = {sizeof(monitorInfo)};
	if (!GetMonitorInfoW(monitor, &monitorInfo)) {
		return false;
	}

	width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
	height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
	return width > 0 && height > 0;
}

inline bool WindowLayout_ComputeMatchWindowResolution(float scale, int &resX,
                                                      int &resY) {
	int monitorW = 0;
	int monitorH = 0;
	if (!WindowLayout_GetPrimaryMonitorSize(monitorW, monitorH)) {
		monitorW = 1920;
		monitorH = 1080;
	}

	if (scale < 0.25f) {
		scale = 0.25f;
	}
	if (scale > 1.0f) {
		scale = 1.0f;
	}

	resX = static_cast<int>(static_cast<float>(monitorW) * scale);
	resY = static_cast<int>(static_cast<float>(monitorH) * scale);
	if (resX < 1) {
		resX = 1;
	}
	if (resY < 1) {
		resY = 1;
	}
	return true;
}

inline bool WindowLayout_GetMonitorRect(HWND hwnd, RECT &monitorRect) {
	const HMONITOR monitor =
	    MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	if (!monitor) {
		return false;
	}

	MONITORINFO monitorInfo = {sizeof(monitorInfo)};
	if (!GetMonitorInfoW(monitor, &monitorInfo)) {
		return false;
	}

	monitorRect = monitorInfo.rcMonitor;
	return true;
}

inline void WindowLayout_ComputeTargetRect(const RECT &monitorRect, float scale,
                                           RECT &targetRect) {
	const int monitorW = monitorRect.right - monitorRect.left;
	const int monitorH = monitorRect.bottom - monitorRect.top;
	int targetW = static_cast<int>(static_cast<float>(monitorW) * scale);
	int targetH = static_cast<int>(static_cast<float>(monitorH) * scale);
	if (targetW < 1) {
		targetW = 1;
	}
	if (targetH < 1) {
		targetH = 1;
	}
	const int x = monitorRect.left + (monitorW - targetW) / 2;
	const int y = monitorRect.top + (monitorH - targetH) / 2;
	targetRect.left = x;
	targetRect.top = y;
	targetRect.right = x + targetW;
	targetRect.bottom = y + targetH;
}

inline bool WindowLayout_ComputeWindowSize(HWND hwnd, float scale, int &width,
                                           int &height) {
	RECT monitorRect = {};
	if (!WindowLayout_GetMonitorRect(hwnd, monitorRect)) {
		return false;
	}

	RECT targetRect = {};
	WindowLayout_ComputeTargetRect(monitorRect, scale, targetRect);
	width = targetRect.right - targetRect.left;
	height = targetRect.bottom - targetRect.top;
	return width > 0 && height > 0;
}

inline bool WindowLayout_ResolveRenderResolution(HWND hwnd, float scale,
                                                 int &resX, int &resY) {
	if (WindowLayout_GetRenderResolution(resX, resY)) {
		return true;
	}

	if (hwnd && WindowLayout_ComputeWindowSize(hwnd, scale, resX, resY)) {
		return true;
	}

	return WindowLayout_ComputeMatchWindowResolution(scale, resX, resY);
}

inline bool WindowLayout_StripWindowChrome(HWND hwnd) {
	if (!hwnd) {
		return false;
	}

	auto style = GetWindowLongW(hwnd, GWL_STYLE);
	style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
	           WS_SYSMENU | WS_BORDER);
	style |= WS_POPUP;
	SetWindowLongW(hwnd, GWL_STYLE, style);

	auto exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
	exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE |
	             WS_EX_WINDOWEDGE);
	SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle);
	return true;
}

inline bool WindowLayout_ApplyToWindow(HWND hwnd) {
	if (!hwnd || !WindowLayout_IsEnabled()) {
		return false;
	}

	RECT monitorRect = {};
	if (!WindowLayout_GetMonitorRect(hwnd, monitorRect)) {
		return false;
	}

	const float scale = WindowLayout_GetScale();
	int width = 0;
	int height = 0;
	RECT targetRect = {};
	if (WindowLayout_ComputeWindowSize(hwnd, scale, width, height)) {
		const int x =
		    monitorRect.left + (monitorRect.right - monitorRect.left - width) / 2;
		const int y =
		    monitorRect.top + (monitorRect.bottom - monitorRect.top - height) / 2;
		targetRect.left = x;
		targetRect.top = y;
		targetRect.right = x + width;
		targetRect.bottom = y + height;
	} else {
		WindowLayout_ComputeTargetRect(monitorRect, scale, targetRect);
		width = targetRect.right - targetRect.left;
		height = targetRect.bottom - targetRect.top;
	}

	WindowLayout_StripWindowChrome(hwnd);
	return SetWindowPos(hwnd, HWND_TOP, targetRect.left, targetRect.top, width,
	                    height, SWP_FRAMECHANGED | SWP_SHOWWINDOW) != FALSE;
}
