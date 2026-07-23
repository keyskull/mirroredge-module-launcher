#pragma once

#include <Windows.h>

#include "mod_host_api.h"

#define MMOD_ENGINE_FORMAT_STATUS_JSON "MMOD_EngineFormatStatusJson"

#define MMOD_BORDERLESS_INSTALL_HOST "MMOD_BorderlessInstallHost"
#define MMOD_BORDERLESS_APPEND_STATUS "MMOD_BorderlessAppendStatus"
#define MMOD_BORDERLESS_QUERY_UI_STATE "MMOD_BorderlessQueryUiState"
#define MMOD_BORDERLESS_SET_ENABLED "MMOD_BorderlessSetEnabled"
#define MMOD_BORDERLESS_SET_SCALE "MMOD_BorderlessSetScale"
#define MMOD_BORDERLESS_MARK_APPLY "MMOD_BorderlessMarkApply"
#define MMOD_BORDERLESS_TRY_SYNC "MMOD_BorderlessTrySyncViewportResolution"
#define MMOD_BORDERLESS_TRY_MOUSE "MMOD_BorderlessTryCompensateMouseLook"
#define MMOD_BORDERLESS_QUERY_VIEWPORT "MMOD_BorderlessQueryEngineViewportSize"
#define MMOD_BORDERLESS_GET_MOUSE_SCALE "MMOD_BorderlessGetLastMouseLookScale"

typedef int(__cdecl *MMOD_EngineFormatStatusJsonFn)(char *out, int outChars);

typedef void(__cdecl *MMOD_BorderlessInstallHostFn)(const ModHostApi *host);
typedef void(__cdecl *MMOD_BorderlessAppendStatusFn)(char *buffer, size_t bufferSize,
                                                     size_t *written);
typedef bool(__stdcall *MMOD_BorderlessQueryUiStateFn)(bool *enabled, float *scale,
                                                       int *clientWidth, int *clientHeight,
                                                       int *backBufferWidth,
                                                       int *backBufferHeight);
typedef void(__stdcall *MMOD_BorderlessSetEnabledFn)(bool enabled);
typedef void(__stdcall *MMOD_BorderlessSetScaleFn)(float scale);
typedef void(__stdcall *MMOD_BorderlessMarkApplyFn)();
typedef bool(__stdcall *MMOD_BorderlessTrySyncFn)(int width, int height);
typedef bool(__stdcall *MMOD_BorderlessTryMouseFn)(int clientWidth, int clientHeight,
                                                   int renderWidth, int renderHeight);
typedef bool(__stdcall *MMOD_BorderlessQueryViewportFn)(int *width, int *height);
typedef float(__stdcall *MMOD_BorderlessGetMouseScaleFn)();
