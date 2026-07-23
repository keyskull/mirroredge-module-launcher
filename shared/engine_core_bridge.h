#pragma once

#include "mod_host_api.h"

#include <d3d9.h>

typedef void (*MmCoreVoidFn)();
typedef void (*MmCoreLogFn)(const char *message);
typedef void (*MmCoreMenuTabFn)();
typedef MMOD_MenuTabCallback (*MmCoreWrapMenuTabFn)(MmCoreMenuTabFn callback);
typedef MMOD_RenderSceneCallback (*MmCoreWrapRenderFn)(MMOD_RenderSceneCallback callback);

struct MmCoreBridge {
	const ModHostApi *host = nullptr;
	MmCoreWrapRenderFn wrapRenderScene = nullptr;
	MmCoreWrapMenuTabFn wrapMenuTab = nullptr;
	MmCoreVoidFn ipcPump = nullptr;
	MmCoreVoidFn menuPoll = nullptr;
	MmCoreLogFn logWrite = nullptr;
	bool (*getSettingBool)(const char *section, const char *key, bool defaultValue) =
	    nullptr;
	int (*getSettingInt)(const char *section, const char *key, int defaultValue) =
	    nullptr;
	void (*setSettingBool)(const char *section, const char *key, bool value) =
	    nullptr;
	void (*setSettingInt)(const char *section, const char *key, int value) = nullptr;
	bool (*getSettingString)(const char *section, const char *key, char *out,
	                         size_t outSize, const char *defaultValue) = nullptr;
	void (*setSettingString)(const char *section, const char *key,
	                         const char *value) = nullptr;
};

namespace EngineCoreBridge {

void Set(const MmCoreBridge *bridge);
const MmCoreBridge *Get();

void Log(const char *message);
void PumpIpc();
void PollMenu();

} // namespace EngineCoreBridge
