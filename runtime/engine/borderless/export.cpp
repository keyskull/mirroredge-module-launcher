#include "borderless.h"
#include "engine_api.h"
#include "mod_host_api.h"
#include "viewport.h"

#include <algorithm>
#include <cstring>
#include <string>

extern "C" __declspec(dllexport) void __cdecl
MMOD_BorderlessInstallHost(const ModHostApi *host) {
	EngineBorderless::InstallHost(host);
}

extern "C" __declspec(dllexport) void __cdecl
MMOD_BorderlessAppendStatus(char *buffer, size_t bufferSize, size_t *written) {
	if (written) {
		*written = 0;
	}
	if (!buffer || bufferSize == 0) {
		return;
	}

	std::string chunk;
	EngineBorderless::AppendStatusJson(chunk);
	const size_t copyLen = (std::min)(chunk.size(), bufferSize - 1);
	memcpy(buffer, chunk.data(), copyLen);
	buffer[copyLen] = '\0';
	if (written) {
		*written = copyLen;
	}
}

extern "C" __declspec(dllexport) bool __stdcall
MMOD_BorderlessQueryUiState(bool *enabled, float *scale, int *clientWidth,
                            int *clientHeight, int *backBufferWidth,
                            int *backBufferHeight) {
	EngineBorderless::UiState state = {};
	if (!EngineBorderless::QueryUiState(state)) {
		return false;
	}
	if (enabled) {
		*enabled = state.enabled;
	}
	if (scale) {
		*scale = state.scale;
	}
	if (clientWidth) {
		*clientWidth = state.clientWidth;
	}
	if (clientHeight) {
		*clientHeight = state.clientHeight;
	}
	if (backBufferWidth) {
		*backBufferWidth = state.backBufferWidth;
	}
	if (backBufferHeight) {
		*backBufferHeight = state.backBufferHeight;
	}
	return true;
}

extern "C" __declspec(dllexport) void __stdcall MMOD_BorderlessSetEnabled(bool enabled) {
	EngineBorderless::SetEnabled(enabled);
}

extern "C" __declspec(dllexport) void __stdcall MMOD_BorderlessSetScale(float scale) {
	EngineBorderless::SetScale(scale);
}

extern "C" __declspec(dllexport) void __stdcall MMOD_BorderlessMarkApply() {
	EngineBorderless::MarkApply();
}

extern "C" __declspec(dllexport) bool __stdcall
MMOD_BorderlessTrySyncViewportResolution(int width, int height) {
	return EngineBorderlessSync::TrySyncViewportResolution(width, height);
}

extern "C" __declspec(dllexport) bool __stdcall
MMOD_BorderlessTryCompensateMouseLook(int clientWidth, int clientHeight,
                                      int renderWidth, int renderHeight) {
	return EngineBorderlessSync::TryCompensateMouseLook(clientWidth, clientHeight,
	                                                    renderWidth, renderHeight);
}

extern "C" __declspec(dllexport) bool __stdcall
MMOD_BorderlessQueryEngineViewportSize(int *width, int *height) {
	if (!width || !height) {
		return false;
	}
	return EngineBorderlessSync::QueryEngineViewportSize(*width, *height);
}

extern "C" __declspec(dllexport) float __stdcall MMOD_BorderlessGetLastMouseLookScale() {
	return EngineBorderlessSync::GetLastMouseLookScale();
}
