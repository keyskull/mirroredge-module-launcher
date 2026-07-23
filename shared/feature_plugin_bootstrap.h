#pragma once

#include <Windows.h>

#include "feature_plugin_host.h"
#include "mmultiplayer_api.h"
#include "module_contract.h"
#include "mod_host_api.h"
#include "mp_engine_shim.h"
#include "plugin_ui.h"

namespace FeaturePluginBootstrap {

inline const MmultiplayerApi *ResolveMmultiplayerApi() {
	HMODULE core = GetModuleHandleW(L"core.dll");
	if (!core) {
		core = GetModuleHandleW(L"mm-core.dll");
	}
	if (!core) {
		core = GetModuleHandleW(MMOD_LEGACY_DLL_FILENAME);
	}
	if (!core) {
		return nullptr;
	}

	const auto getApi = reinterpret_cast<MMOD_GetMmultiplayerApiFn>(
	    GetProcAddress(core, MMULTIPLAYER_API_EXPORT));
	if (!getApi) {
		return nullptr;
	}

	const MmultiplayerApi *api = getApi();
	if (!api || api->version < 1) {
		return nullptr;
	}

	return api;
}

inline bool IsValidHost(const ModHostApi *host) {
	return host && host->version >= MMOD_HOST_API_VERSION && host->ui;
}

inline void ForwardLogSafe(const char *message) {
	try {
		FeaturePluginHost::ForwardLog(message);
	} catch (...) {
		OutputDebugStringA("feature_plugin_bootstrap: C++ exception in ForwardLog\n");
	}
}

inline const MmultiplayerApi *AttachCore() {
	const MmultiplayerApi *api = ResolveMmultiplayerApi();
	if (!api) {
		return nullptr;
	}

	// Bind engine API only; gameplay hooks stay lazy (plugin tab / runtime hooks).
	// Requiring EnsureGameplayHooks() here can block plugin init on the main menu
	// when EndScene is not pumping yet (hosted split + borderless boot).
	MpEngineShim::Bind(api);
	return api;
}

inline void AttachHost(const ModHostApi *host, HMODULE self) {
	FeaturePluginHost::Attach(host, self);
	PluginUi::Bind(host->ui);
}

inline void DetachCore() {
	FeaturePluginHost::Detach();
	MpEngineShim::Bind(nullptr);
}

} // namespace FeaturePluginBootstrap
