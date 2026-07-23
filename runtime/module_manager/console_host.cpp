#include "mod_console.h"

#include "console_loader.h"
#include "manager_console_bridge.h"
#include "menu.h"
#include "mod_log.h"
#include "mod_registry.h"
#include "plugin_ui_bridge.h"
#include "presentation.h"

#include <Windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

typedef void(__cdecl *ConsoleAttachFn)(const ManagerConsoleBridge *, const PluginUiApi *);
typedef void(__cdecl *ConsoleVoidFn)();
typedef bool(__cdecl *ConsoleBoolFn)();
typedef void(__cdecl *ConsoleRenderFn)(IDirect3DDevice9 *);
typedef void(__cdecl *ConsoleRenderRecentFn)(size_t);
typedef void(__cdecl *ConsoleExecuteFn)(const char *);

ConsoleAttachFn g_attach = nullptr;
ConsoleVoidFn g_initialize = nullptr;
ConsoleVoidFn g_show = nullptr;
ConsoleVoidFn g_hide = nullptr;
ConsoleBoolFn g_isOpen = nullptr;
ConsoleVoidFn g_toggle = nullptr;
ConsoleVoidFn g_pollToggle = nullptr;
ConsoleRenderFn g_render = nullptr;
ConsoleRenderRecentFn g_renderRecent = nullptr;
ConsoleExecuteFn g_execute = nullptr;

bool g_attached = false;

void BridgeWriteLog(const char *message) { ModLog::Write(message); }

void BridgeClearLog() { ModLog::Clear(); }

void BridgeForEachLogLine(void (*fn)(const char *line, void *ctx), void *ctx) {
	if (!fn) {
		return;
	}
	std::vector<std::string> lines;
	ModLog::GetLines(lines);
	for (const auto &line : lines) {
		fn(line.c_str(), ctx);
	}
}

void BridgeFormatStatus(char *out, size_t outSize) {
	if (!out || outSize == 0) {
		return;
	}
	std::string status;
	ModRegistry::FormatStatusList(status);
	strncpy(out, status.c_str(), outSize - 1);
	out[outSize - 1] = '\0';
}

void BridgeFormatModules(char *out, size_t outSize) {
	if (!out || outSize == 0) {
		return;
	}
	std::string listing;
	ModRegistry::FormatModuleList(listing);
	strncpy(out, listing.c_str(), outSize - 1);
	out[outSize - 1] = '\0';
}

bool BridgeRequestLoad(const wchar_t *moduleId, const char *source) {
	return ModRegistry::RequestLoad(moduleId, source);
}

bool BridgeRequestUnload(const wchar_t *moduleId) {
	return ModRegistry::RequestUnload(moduleId);
}

void BridgeShowMenu() { HostMenu::Show(); }

bool BridgeIsMenuOpen() { return HostMenu::IsOpen(); }

bool BridgeAreHooksInstalled() { return Presentation::AreHooksInstalled(); }

bool BridgeIsOverlayReady() { return Presentation::IsOverlayReady(); }

void BridgeSetBlockInput(bool block) { HostMenu_SetBlockInput(block); }

bool ResolveExports(HMODULE consoleModule) {
	g_attach = reinterpret_cast<ConsoleAttachFn>(
	    GetProcAddress(consoleModule, "MMOD_ConsoleAttach"));
	g_initialize = reinterpret_cast<ConsoleVoidFn>(
	    GetProcAddress(consoleModule, "MMOD_ConsoleInitialize"));
	g_show = reinterpret_cast<ConsoleVoidFn>(GetProcAddress(consoleModule, "MMOD_ConsoleShow"));
	g_hide = reinterpret_cast<ConsoleVoidFn>(GetProcAddress(consoleModule, "MMOD_ConsoleHide"));
	g_isOpen =
	    reinterpret_cast<ConsoleBoolFn>(GetProcAddress(consoleModule, "MMOD_ConsoleIsOpen"));
	g_toggle =
	    reinterpret_cast<ConsoleVoidFn>(GetProcAddress(consoleModule, "MMOD_ConsoleToggle"));
	g_pollToggle = reinterpret_cast<ConsoleVoidFn>(
	    GetProcAddress(consoleModule, "MMOD_ConsolePollToggle"));
	g_render =
	    reinterpret_cast<ConsoleRenderFn>(GetProcAddress(consoleModule, "MMOD_ConsoleRender"));
	g_renderRecent = reinterpret_cast<ConsoleRenderRecentFn>(
	    GetProcAddress(consoleModule, "MMOD_ConsoleRenderRecent"));
	g_execute = reinterpret_cast<ConsoleExecuteFn>(
	    GetProcAddress(consoleModule, "MMOD_ConsoleExecuteCommand"));
	return g_attach && g_initialize && g_show && g_hide && g_isOpen && g_toggle &&
	       g_pollToggle && g_render && g_renderRecent && g_execute;
}

bool EnsureAttached() {
	if (g_attached) {
		return true;
	}

	const HMODULE hostModule = GetModuleHandleW(L"module_manager.dll");
	if (!hostModule) {
		return false;
	}

	if (!ConsoleLoader::EnsureLoaded(hostModule)) {
		ModLog::Write("console: failed to load mm-console.dll");
		return false;
	}

	const HMODULE consoleModule = ConsoleLoader::GetModule();
	if (!consoleModule || !ResolveExports(consoleModule)) {
		ModLog::Write("console: mm-console exports missing");
		return false;
	}

	static const ManagerConsoleBridge kBridge = {
	    BridgeWriteLog,
	    BridgeClearLog,
	    BridgeForEachLogLine,
	    BridgeFormatStatus,
	    BridgeFormatModules,
	    BridgeRequestLoad,
	    BridgeRequestUnload,
	    BridgeShowMenu,
	    BridgeIsMenuOpen,
	    BridgeAreHooksInstalled,
	    BridgeIsOverlayReady,
	    BridgeSetBlockInput,
	};

	g_attach(&kBridge, PluginUiBridge_GetApi());
	g_attached = true;
	return true;
}

} // namespace

namespace ModConsole {

void Initialize() {
	if (EnsureAttached() && g_initialize) {
		g_initialize();
	}
}

void Show() {
	if (EnsureAttached() && g_show) {
		g_show();
	}
}

void Hide() {
	if (g_hide) {
		g_hide();
	}
}

bool IsOpen() {
	if (!EnsureAttached() || !g_isOpen) {
		return false;
	}
	return g_isOpen();
}

void Toggle() {
	if (EnsureAttached() && g_toggle) {
		g_toggle();
	}
}

void PollToggle() {
	if (g_pollToggle) {
		g_pollToggle();
	}
}

void ExecuteCommand(const char *line) {
	if (g_execute) {
		g_execute(line);
	}
}

void RenderRecent(size_t maxLines) {
	if (EnsureAttached() && g_renderRecent) {
		g_renderRecent(maxLines);
	}
}

void Render(IDirect3DDevice9 *device) {
	if (g_render) {
		g_render(device);
	}
}

} // namespace ModConsole
