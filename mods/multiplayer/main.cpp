#include <Windows.h>

#include "client_plugin.h"
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

		if (!ClientPlugin::Initialize()) {
			FeaturePluginBootstrap::DetachCore();
			return false;
		}

		FeaturePluginBootstrap::ForwardLogSafe("multiplayer: plugin initialized");
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
		ClientPlugin::Shutdown();
	} catch (...) {
		OutputDebugStringA("multiplayer: C++ exception in Shutdown\n");
	}

	FeaturePluginBootstrap::DetachCore();
	g_selfModule = nullptr;
	FeaturePluginBootstrap::ForwardLogSafe("multiplayer: plugin shutdown");
}
