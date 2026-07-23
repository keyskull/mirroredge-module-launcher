#include "modhost.h"

#include "agent_log.h"
#include "plugin_ui.h"
#include "me_sdk/runtime/safe_gui_invoke.h"

#include <vector>

namespace {

const ModHostApi *g_host = nullptr;
HMODULE g_selfModule = nullptr;

std::vector<MenuTabCallback> g_hostedTabCallbacks;
std::vector<MMOD_RenderSceneCallback> g_hostedRenderCallbacks;

#define MMOD_DECL_HOSTED_TAB(N)                                                \
    static void HostedTab##N() {                                               \
        ModHost::EnsureImGuiContext();                                         \
        if ((N) < g_hostedTabCallbacks.size() && g_hostedTabCallbacks[(N)]) {  \
            MeSdk::Safe::Gui::InvokeMenuTab(g_hostedTabCallbacks[(N)]);        \
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
MMOD_DECL_HOSTED_TAB(8)
MMOD_DECL_HOSTED_TAB(9)
MMOD_DECL_HOSTED_TAB(10)
MMOD_DECL_HOSTED_TAB(11)
MMOD_DECL_HOSTED_TAB(12)
MMOD_DECL_HOSTED_TAB(13)
MMOD_DECL_HOSTED_TAB(14)
MMOD_DECL_HOSTED_TAB(15)

static MMOD_MenuTabCallback kHostedTabTrampolines[] = {
    HostedTab0,  HostedTab1,  HostedTab2,  HostedTab3,  HostedTab4,
    HostedTab5,  HostedTab6,  HostedTab7,  HostedTab8,  HostedTab9,
    HostedTab10, HostedTab11, HostedTab12, HostedTab13, HostedTab14,
    HostedTab15,
};

#define MMOD_DECL_HOSTED_RENDER(N)                                             \
    static void HostedRender##N(IDirect3DDevice9 *device) {                    \
        ModHost::EnsureImGuiContext();                                         \
        if ((N) < g_hostedRenderCallbacks.size() &&                            \
            g_hostedRenderCallbacks[(N)]) {                                    \
            MeSdk::Safe::Gui::InvokeRenderOverlay(                             \
                g_hostedRenderCallbacks[(N)], device);                         \
        }                                                                      \
    }

MMOD_DECL_HOSTED_RENDER(0)
MMOD_DECL_HOSTED_RENDER(1)
MMOD_DECL_HOSTED_RENDER(2)
MMOD_DECL_HOSTED_RENDER(3)
MMOD_DECL_HOSTED_RENDER(4)
MMOD_DECL_HOSTED_RENDER(5)
MMOD_DECL_HOSTED_RENDER(6)
MMOD_DECL_HOSTED_RENDER(7)

static MMOD_RenderSceneCallback kHostedRenderTrampolines[] = {
    HostedRender0, HostedRender1, HostedRender2, HostedRender3,
    HostedRender4, HostedRender5, HostedRender6, HostedRender7,
};

} // namespace

namespace ModHost {

void Attach(const ModHostApi *host) {
	g_host = host;
	if (host && host->ui) {
		PluginUi::Bind(host->ui);
	}
}

void SetSelfModule(HMODULE module) { g_selfModule = module; }

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

MMOD_MenuTabCallback WrapTabCallback(MenuTabCallback callback) {
    if (!callback) {
        return nullptr;
    }

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

    const auto index = g_hostedRenderCallbacks.size();
    if (index >= sizeof(kHostedRenderTrampolines) /
                     sizeof(kHostedRenderTrampolines[0])) {
        return callback;
    }

    g_hostedRenderCallbacks.push_back(callback);
    return kHostedRenderTrampolines[index];
}

} // namespace ModHost
