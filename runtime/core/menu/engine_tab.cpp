#include "engine_tab.h"

#include "agent_log.h"
#include "debug.h"
#include "engine.h"
#include "menu.h"
#include "settings.h"
#include "ui_harness_plugin.h"

#include "me_sdk/runtime/safe_gui.h"

#include <codecvt>
#include <locale>
#include <string>

extern bool showPlayerInfo;
extern int showKeybind;

void CoreMenuEngineTab() {
	// #region agent log
	MpDebugLog("menu/engine_tab.cpp:CoreMenuEngineTab", "tab_enter", "H-E");
	// #endregion

	ImGui::TextUnformatted("Engine");
	HarnessUi::Record("mm/multiplayer/engine-tab", Engine::GetWindow());
	HarnessUi::RecordRect("mm/multiplayer/engine-tab-anchor", 8.f, 8.f, 9.f, 9.f);

	auto engine = MeSdk::Safe::Gui::TryFindTdGameEngine(true);
	if (!engine) {
		// #region agent log
		MpDebugLog("menu/engine_tab.cpp:CoreMenuEngineTab", "no_engine", "H-E");
		// #endregion
		ImGui::TextDisabled("Engine not available yet.");
		HarnessUi::Record("mm/multiplayer/engine-not-ready", Engine::GetWindow());
		return;
	}

	MeSdk::Safe::Gui::EngineMenuState state = {};
	if (!MeSdk::Safe::Gui::TryReadEngineMenuState(engine, state)) {
		ImGui::TextDisabled("Engine state unavailable.");
		HarnessUi::Record("mm/multiplayer/engine-state-unavailable", Engine::GetWindow());
		return;
	}

	static char command[0xFFF] = {0};

	auto commandInputCallback = []() {
		if (command[0]) {
			Engine::ExecuteCommand(
			    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(
			        command)
			        .c_str());

			command[0] = 0;
		}
	};

	if (ImGui::InputText("##command", command, sizeof(command),
	                     ImGuiInputTextFlags_EnterReturnsTrue)) {
		commandInputCallback();
	}

	ImGui::SameLine();
	if (ImGui::Button("Execute Command##engine-execute-command")) {
		commandInputCallback();
	}
	HarnessUi::Record("mm/multiplayer/engine-execute-command", Engine::GetWindow());

	bool check = state.smoothFrameRate;
	ImGui::Checkbox("Smooth Framerate##engine-smooth-framerate", &check);
	if (check != state.smoothFrameRate) {
		MeSdk::Safe::Gui::TryWriteEngineSmoothFrameRate(engine, check);
		state.smoothFrameRate = check;
	}
	HarnessUi::Record("mm/multiplayer/engine-smooth-framerate", Engine::GetWindow());
	if (check) {
		float minFps = state.minSmoothedFrameRate;
		float maxFps = state.maxSmoothedFrameRate;
		if (ImGui::InputFloat("Min Smoothed Framerate##engine-min-smoothed",
		                      &minFps)) {
			MeSdk::Safe::Gui::TryWriteEngineFrameRateLimits(engine, minFps, maxFps);
		}
		if (ImGui::InputFloat("Max Smoothed Framerate##engine-max-smoothed",
		                      &maxFps)) {
			MeSdk::Safe::Gui::TryWriteEngineFrameRateLimits(engine, minFps, maxFps);
		}
	}

	if (state.hasClient) {
		float gamma = state.displayGamma;
		if (ImGui::InputFloat("Gamma##engine-gamma", &gamma)) {
			MeSdk::Safe::Gui::TryWriteClientGamma(engine, gamma);
		}
	}

	if (ImGui::Hotkey("Menu Keybind##menu-show", &showKeybind)) {
		Settings::SetSetting("menu", "showKeybind", showKeybind);
	}

	ImGui::SameLine();

	if (ImGui::Button("Debug Console##engine-debug-console")) {
		Debug::CreateConsole();
	}

	ImGui::Separator();
	ImGui::Text("Debug");
	if (ImGui::Checkbox("Show Player Info##engine-show-player-info", &showPlayerInfo)) {
		Settings::SetSetting("player", "showInfo", showPlayerInfo);
	}
	HarnessUi::Record("mm/multiplayer/engine-show-player-info", Engine::GetWindow());
}
