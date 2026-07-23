#pragma once

#include <Windows.h>

#include <d3d9.h>

#include "plugin_ui_api.h"

#define MMOD_HOST_API_VERSION 4U
#define MMOD_PLUGIN_INIT_EXPORT "MMOD_PluginInitialize"
#define MMOD_PLUGIN_SHUTDOWN_EXPORT "MMOD_PluginShutdown"

struct IDirect3DDevice9;

typedef void (*MMOD_MenuTabCallback)();
typedef void (*MMOD_RenderSceneCallback)(IDirect3DDevice9 *device);
typedef void (*MMOD_PresentationTickFn)(IDirect3DDevice9 *device);
typedef void (*MMOD_PresentationInputSyncFn)();
typedef void (*MMOD_MainThreadTask)();
typedef void (*MMOD_LogMessageFn)(HMODULE module, const char *message);

struct ModHostApi {
	unsigned version;
	void (*AddTab)(const char *name, MMOD_MenuTabCallback callback);
	void (*RemoveTab)(const char *name);
	void (*InsertTab)(int index, const char *name, MMOD_MenuTabCallback callback);
	void (*OnRenderScene)(MMOD_RenderSceneCallback callback);
	void (*OnPresentationTick)(MMOD_PresentationTickFn callback);
	void (*OnPresentationInputSync)(MMOD_PresentationInputSyncFn callback);
	void (*InvalidateOverlayGraphics)();
	void (*CreateOverlayGraphics)();
	void (*SetOverlayDisplaySize)(int width, int height);
	void (*BlockInput)(bool block);
	bool (*ArePresentationHooksInstalled)();
	void (*QueueMainThreadTask)(MMOD_MainThreadTask task);
	void (*ShowMenu)();
	void (*HideMenu)();
	bool (*IsMenuOpen)();
	MMOD_LogMessageFn LogMessage;
	const PluginUiApi *ui;
	bool (*RequestLoadModule)(const wchar_t *moduleId, const char *source);
	HMODULE hostModule;
};

typedef bool(__cdecl *MMOD_PluginInitializeFn)(const ModHostApi *host,
                                               HMODULE self);
typedef void(__cdecl *MMOD_PluginShutdownFn)(HMODULE self);
