#include "feature_plugin_host.h"

#include "plugin_ui.h"
#include "me_sdk/runtime/safe_gui_invoke.h"

#include <mutex>
#include <vector>

namespace {

const ModHostApi *g_host = nullptr;
HMODULE g_selfModule = nullptr;

std::mutex g_hostedCallbackMutex;
std::vector<FeatureMenuTabCallback> g_hostedTabCallbacks;
std::vector<MMOD_RenderSceneCallback> g_hostedRenderCallbacks;

#define MMOD_DECL_HOSTED_TAB(N)                                                \
	static void HostedTab##N() {                                               \
		FeaturePluginHost::EnsureImGuiContext();                               \
		FeatureMenuTabCallback callback = nullptr;                             \
		{                                                                      \
			std::lock_guard<std::mutex> lock(g_hostedCallbackMutex);         \
			if ((N) < g_hostedTabCallbacks.size()) {                           \
				callback = g_hostedTabCallbacks[(N)];                          \
			}                                                                  \
		}                                                                      \
		if (callback) {                                                      \
			MeSdk::Safe::Gui::InvokeMenuTab(callback);                        \
		}                                                                      \
	}

MMOD_DECL_HOSTED_TAB(0)
MMOD_DECL_HOSTED_TAB(1)
MMOD_DECL_HOSTED_TAB(2)
MMOD_DECL_HOSTED_TAB(3)
MMOD_DECL_HOSTED_TAB(4)
MMOD_DECL_HOSTED_TAB(5)
MMOD_DECL_HOSTED_TAB(6)
MMOD_DECL_HOSTED_TAB(7)

static MMOD_MenuTabCallback kHostedTabTrampolines[] = {
    HostedTab0, HostedTab1, HostedTab2, HostedTab3,
    HostedTab4, HostedTab5, HostedTab6, HostedTab7,
};

#define MMOD_DECL_HOSTED_RENDER(N)                                             \
	static void HostedRender##N(IDirect3DDevice9 *device) {                    \
		FeaturePluginHost::EnsureImGuiContext();                               \
		MMOD_RenderSceneCallback callback = nullptr;                           \
		{                                                                      \
			std::lock_guard<std::mutex> lock(g_hostedCallbackMutex);         \
			if ((N) < g_hostedRenderCallbacks.size()) {                        \
				callback = g_hostedRenderCallbacks[(N)];                       \
			}                                                                  \
		}                                                                      \
		if (callback) {                                                      \
			MeSdk::Safe::Gui::InvokeRenderOverlay(callback, device);          \
		}                                                                      \
	}

MMOD_DECL_HOSTED_RENDER(0)
MMOD_DECL_HOSTED_RENDER(1)
MMOD_DECL_HOSTED_RENDER(2)
MMOD_DECL_HOSTED_RENDER(3)

static MMOD_RenderSceneCallback kHostedRenderTrampolines[] = {
    HostedRender0, HostedRender1, HostedRender2, HostedRender3,
};

} // namespace

namespace FeaturePluginHost {

void Attach(const ModHostApi *host, HMODULE self) {
	g_host = host;
	g_selfModule = self;
	if (host && host->ui) {
		PluginUi::Bind(host->ui);
	}
}

void Detach() {
	g_host = nullptr;
	g_selfModule = nullptr;
	std::lock_guard<std::mutex> lock(g_hostedCallbackMutex);
	g_hostedTabCallbacks.clear();
	g_hostedRenderCallbacks.clear();
}

bool IsAttached() { return g_host != nullptr; }

const ModHostApi *Get() { return g_host; }

void ForwardLog(const char *message) {
	if (!message || !message[0] || !g_host || !g_host->LogMessage || !g_selfModule) {
		return;
	}
	g_host->LogMessage(g_selfModule, message);
}

void EnsureImGuiContext() {
	if (!g_host || !g_host->ui) {
		return;
	}
	PluginUi::Bind(g_host->ui);
}

MMOD_MenuTabCallback WrapTabCallback(FeatureMenuTabCallback callback) {
	if (!callback) {
		return nullptr;
	}

	std::lock_guard<std::mutex> lock(g_hostedCallbackMutex);
	const auto index = g_hostedTabCallbacks.size();
	if (index >= sizeof(kHostedTabTrampolines) / sizeof(kHostedTabTrampolines[0])) {
		return callback;
	}

	g_hostedTabCallbacks.push_back(callback);
	return kHostedTabTrampolines[index];
}

MMOD_RenderSceneCallback WrapRenderCallback(MMOD_RenderSceneCallback callback) {
	if (!callback) {
		return nullptr;
	}

	std::lock_guard<std::mutex> lock(g_hostedCallbackMutex);
	const auto index = g_hostedRenderCallbacks.size();
	if (index >= sizeof(kHostedRenderTrampolines) /
	                 sizeof(kHostedRenderTrampolines[0])) {
		return callback;
	}

	g_hostedRenderCallbacks.push_back(callback);
	return kHostedRenderTrampolines[index];
}

void AddTab(const char *name, FeatureMenuTabCallback callback) {
	if (!g_host || !g_host->AddTab) {
		return;
	}
	g_host->AddTab(name, WrapTabCallback(callback));
}

void RemoveTab(const char *name) {
	if (!g_host || !g_host->RemoveTab) {
		return;
	}
	g_host->RemoveTab(name);
}

void OnRenderScene(MMOD_RenderSceneCallback callback) {
	if (!g_host || !g_host->OnRenderScene) {
		return;
	}
	g_host->OnRenderScene(WrapRenderCallback(callback));
}

void ShowMenu() {
	if (g_host && g_host->ShowMenu) {
		g_host->ShowMenu();
	}
}

void HideMenu() {
	if (g_host && g_host->HideMenu) {
		g_host->HideMenu();
	}
}

bool IsMenuOpen() {
	return g_host && g_host->IsMenuOpen && g_host->IsMenuOpen();
}

} // namespace FeaturePluginHost
