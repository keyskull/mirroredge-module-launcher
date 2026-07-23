#include "console_shared.h"

#include "plugin_ui.h"
#include "runtime_version_query.h"
#include "version.h"

#include <d3d9.h>

#include <Windows.h>

#include <string>
#include <vector>

namespace {

constexpr int kWndNoDecoration = (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5);
constexpr int kWndNoMove = 1 << 2;
constexpr int kWndNoSavedSettings = 1 << 8;
constexpr int kWndNoNav = (1 << 18) | (1 << 19);
constexpr int kWndHorizontalScrollbar = 1 << 11;

constexpr float kConsoleHeightRatio = 0.45f;

bool IsToggleDown() { return (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0; }

ImVec4 LineColor(const std::string &line) {
	auto contains = [&](const char *needle) {
		return line.find(needle) != std::string::npos;
	};

	if (contains("failed") || contains("exception") || contains("crashed") ||
	    contains("rejected") || contains("error")) {
		return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
	}
	if (contains("warning") || contains("skipped")) {
		return ImVec4(1.0f, 0.85f, 0.35f, 1.0f);
	}
	return ImVec4(0.65f, 0.82f, 0.65f, 1.0f);
}

void RenderOutputLines(const std::vector<std::string> &lines, size_t maxLines) {
	if (lines.empty()) {
		PluginUi::TextDisabled("Console ready. Type 'help' for commands.");
		return;
	}

	const size_t start =
	    maxLines > 0 && lines.size() > maxLines ? lines.size() - maxLines : 0;
	for (size_t i = start; i < lines.size(); ++i) {
		const auto &line = lines[i];
		PluginUi::PushStyleColor(ImGuiCol_Text, LineColor(line));
		PluginUi::TextUnformatted(line.c_str());
		PluginUi::PopStyleColor();
	}
}

void RenderVersionBar() {
	char line[256] = {};
	RuntimeVersionQuery::FormatComponentVersionsLine(line, sizeof(line));
	if (line[0]) {
		PluginUi::TextDisabled("%s", line);
	} else {
		PluginUi::TextDisabled("mm-console v%s", MMOD_CONSOLE_VERSION_STRING);
	}
	PluginUi::Separator();
}

} // namespace

extern "C" __declspec(dllexport) void __cdecl MMOD_ConsoleAttach(
    const ManagerConsoleBridge *bridge, const PluginUiApi *ui) {
	ConsoleShared::SetBridge(bridge, ui);
}

extern "C" __declspec(dllexport) void __cdecl MMOD_ConsoleShow() {
	const auto &bridge = ConsoleShared::Bridge();
	if (!bridge.areHooksInstalled || !bridge.areHooksInstalled()) {
		ConsoleShared::Print("module_manager: overlay not ready - focus game window, wait for "
		                   "main menu, then retry `");
		return;
	}
	ConsoleShared::SetShown(true);
	ConsoleShared::FocusInput() = true;
	if (bridge.isOverlayReady && bridge.isOverlayReady() && bridge.setBlockInput) {
		bridge.setBlockInput(true);
	}
}

extern "C" __declspec(dllexport) void __cdecl MMOD_ConsoleHide() {
	const auto &bridge = ConsoleShared::Bridge();
	ConsoleShared::SetShown(false);
	ConsoleShared::FocusInput() = false;
	ConsoleShared::ResetHistoryNav();
	if (bridge.isMenuOpen && !bridge.isMenuOpen() && bridge.setBlockInput) {
		bridge.setBlockInput(false);
	}
}

extern "C" __declspec(dllexport) bool __cdecl MMOD_ConsoleIsOpen() {
	return ConsoleShared::IsShown();
}

extern "C" __declspec(dllexport) void __cdecl MMOD_ConsoleToggle() {
	if (ConsoleShared::IsShown()) {
		MMOD_ConsoleHide();
	} else {
		MMOD_ConsoleShow();
	}
}

extern "C" __declspec(dllexport) void __cdecl MMOD_ConsolePollToggle() {
	static bool wasDown = false;
	const bool down = IsToggleDown();

	if (down && !wasDown) {
		MMOD_ConsoleToggle();
	}
	wasDown = down;

	if (!ConsoleShared::IsShown()) {
		return;
	}

	static bool escapeWasDown = false;
	const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
	if (escapeDown && !escapeWasDown) {
		MMOD_ConsoleHide();
	}
	escapeWasDown = escapeDown;
}

extern "C" __declspec(dllexport) void __cdecl MMOD_ConsoleRenderRecent(size_t maxLines) {
	std::vector<std::string> lines;
	ConsoleShared::GetLogLines(lines);
	RenderOutputLines(lines, maxLines);
}

extern "C" __declspec(dllexport) void __cdecl MMOD_ConsoleRender(IDirect3DDevice9 *) {
	if (!ConsoleShared::IsShown() || !PluginUi::IsBound()) {
		return;
	}

	const ImGuiIO io = PluginUi::GetIO();
	const ImVec2 consoleSize(io.DisplaySize.x, io.DisplaySize.y * kConsoleHeightRatio);

	PluginUi::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
	PluginUi::SetNextWindowSize(consoleSize, ImGuiCond_Always);
	PluginUi::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.82f));
	PluginUi::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	PluginUi::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	PluginUi::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

	const int windowFlags = kWndNoDecoration | kWndNoMove | kWndNoSavedSettings | kWndNoNav;
	if (!PluginUi::Begin("##mm-console", nullptr, windowFlags)) {
		PluginUi::PopStyleVar(3);
		PluginUi::PopStyleColor();
		return;
	}

	const float inputHeight = PluginUi::GetFrameHeightWithSpacing();
	PluginUi::BeginChild("mm_console_output", ImVec2(0.0f, -inputHeight), false,
	                     kWndHorizontalScrollbar);

	RenderVersionBar();

	std::vector<std::string> lines;
	ConsoleShared::GetLogLines(lines);
	RenderOutputLines(lines, 0);

	if (ConsoleShared::AutoScroll() &&
	    PluginUi::GetScrollY() >=
	        PluginUi::GetScrollMaxY() - PluginUi::GetTextLineHeight()) {
		PluginUi::SetScrollHereY(1.0f);
	}

	PluginUi::EndChild();

	PluginUi::Separator();
	PluginUi::TextUnformatted(">");
	PluginUi::SameLine();
	PluginUi::PushItemWidth(-1.0f);
	if (ConsoleShared::FocusInput()) {
		PluginUi::SetKeyboardFocusHere();
		ConsoleShared::FocusInput() = false;
	}

	const bool submitted = PluginUi::InputTextCallback(
	    "##mm_console_input", ConsoleShared::InputBuffer(),
	    static_cast<int>(ConsoleShared::InputBufferSize()),
	    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
	        ImGuiInputTextFlags_CallbackCompletion,
	    ConsoleInputCallback, nullptr);

	PluginUi::PopItemWidth();

	if (submitted) {
		ConsoleShared::ExecuteCommand(ConsoleShared::InputBuffer());
		ConsoleShared::InputBuffer()[0] = '\0';
		ConsoleShared::FocusInput() = true;
		ConsoleShared::AutoScroll() = true;
	}

	PluginUi::End();
	PluginUi::PopStyleVar(3);
	PluginUi::PopStyleColor();
}