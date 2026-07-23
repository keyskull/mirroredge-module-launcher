#include <Windows.h>

#include "dolly.h"
#include "feature_plugin_bootstrap.h"
#include "mod_log.h"
#include "settings.h"

static HMODULE g_selfModule = nullptr;

extern "C" bool __cdecl MMOD_PluginInitialize(const ModHostApi *host, HMODULE self) {
	try {
		if (!FeaturePluginBootstrap::IsValidHost(host)) {
			return false;
		}

		if (!FeaturePluginBootstrap::AttachCore()) {
			return false;
		}

		g_selfModule = self;
		FeaturePluginBootstrap::AttachHost(host, self);
		Settings::Load();

		if (!DollyPlugin::Initialize()) {
			FeaturePluginBootstrap::DetachCore();
			return false;
		}

		FeaturePluginBootstrap::ForwardLogSafe("dolly: plugin initialized");
		return true;
	} catch (...) {
		FeaturePluginBootstrap::DetachCore();
		return false;
	}
}

extern "C" void __cdecl MMOD_PluginShutdown(HMODULE self) {
	if (self != g_selfModule && g_selfModule != nullptr) {
		return;
	}

	try {
		DollyPlugin::Shutdown();
	} catch (...) {
		OutputDebugStringA("dolly: C++ exception in Shutdown\n");
	}

	FeaturePluginBootstrap::DetachCore();
	g_selfModule = nullptr;
}
