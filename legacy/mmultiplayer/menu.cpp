#include <algorithm>
#include <vector>
#include <locale>
#include <codecvt>
#include <d3d9.h>

#include "agent_log.h"
#include "debug.h"
#include "engine.h"
#include "menu.h"
#include "modhost.h"
#include "settings.h"
#include "plugin_ui.h"
#include "ui_harness_plugin.h"
#include "me_sdk/runtime/safe_gui.h"

static auto show = false, showPlayerInfo = false;
static std::vector<MenuTab> tabs;
static int showKeybind = 0;
static std::wstring levelName;

static void RenderStatusHint() {
	if (ModHost::IsAttached() || show) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.45f);
	ImGui::Begin("##mm-status", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
	                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
	                 ImGuiWindowFlags_NoFocusOnAppearing);
	ImGui::Text("mmultiplayer loaded");
	ImGui::Text("Insert or F10 = Module Manager");
	ImGui::End();
}

static void RenderMenu(IDirect3DDevice9 *device) {
	if (!ModHost::IsAttached()) {
		RenderStatusHint();

		if (show) {
			ImGui::Begin("Module Manager");
			ImGui::BeginTabBar("module-manager-tabs");

			for (auto tab : tabs) {
				if (ImGui::BeginTabItem(tab.Name.c_str())) {
					MeSdk::Safe::Gui::InvokeMenuTab(tab.Callback);
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
			ImGui::End();
		}
	}

	if (showPlayerInfo) {
		auto pawn = Engine::GetPlayerPawn();
		auto controller = Engine::GetPlayerController();

		MeSdk::Safe::Gui::PlayerOverlayInfo info = {};
		if (MeSdk::Safe::Gui::TryReadPlayerOverlay(pawn, controller, info)) {
			static const auto rightPadding = 100.0f;
			static const auto padding = 5.0f;

			auto window = ImGui::BeginRawScene("##player-debug-info");
			if (window && window->DrawList) {
			const auto io = ImGui::GetIO();
			auto width = io.DisplaySize.x - padding;

			auto yIncrement = ImGui::GetTextLineHeight();
			auto y = io.DisplaySize.y - (7 * yIncrement) - padding;
			auto color = ImColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

			window->DrawList->AddRectFilled(
			    ImVec2(width - rightPadding - padding, y - padding), io.DisplaySize,
			    ImColor(ImVec4(0, 0, 0, 0.4f)));

			char buffer[0x200] = {0};

			sprintf_s(buffer, "%.2f", info.posX);
			window->DrawList->AddText(
			    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x, y),
			    color, buffer);
			window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "X");

			sprintf_s(buffer, "%.2f", info.posY);
			window->DrawList->AddText(
			    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x,
			           y += yIncrement),
			    color, buffer);
			window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "Y");

			sprintf_s(buffer, "%.2f", info.posZ);
			window->DrawList->AddText(
			    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x,
			           y += yIncrement),
			    color, buffer);
			window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "Z");

			sprintf_s(buffer, "%.2f", info.velocity2D);
			window->DrawList->AddText(
			    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x,
			           y += yIncrement),
			    color, buffer);
			window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "V");

			sprintf_s(buffer, "%.2f", info.rotPitchDeg);
			window->DrawList->AddText(
			    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x,
			           y += yIncrement),
			    color, buffer);
			window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "RX");

			sprintf_s(buffer, "%.2f", info.rotYawDeg);
			window->DrawList->AddText(
			    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x,
			           y += yIncrement),
			    color, buffer);
			window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "RY");

			sprintf_s(buffer, "%d", info.movementState);
			window->DrawList->AddText(
			    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x,
			           y += yIncrement),
			    color, buffer);
			window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "S");

			ImGui::EndRawScene();
			}
		}
	}
}

static void EngineTab() {
	// #region agent log
	MpDebugLog("menu.cpp:EngineTab", "tab_enter", "H-E");
	// #endregion

	auto engine = Engine::GetEngine();
	if (!engine) {
		// #region agent log
		MpDebugLog("menu.cpp:EngineTab", "no_engine", "H-E");
		// #endregion
		ImGui::TextDisabled("Engine not available yet.");
		return;
	}

	MeSdk::Safe::Gui::EngineMenuState state = {};
	if (!MeSdk::Safe::Gui::TryReadEngineMenuState(engine, state)) {
		ImGui::TextDisabled("Engine state unavailable.");
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

static void WorldTab() {
	// #region agent log
	MpDebugLog("menu.cpp:WorldTab", "tab_enter", "H-W");
	// #endregion

	ImGui::TextUnformatted("World");
	HarnessUi::Record("mm/multiplayer/world-tab", Engine::GetWindow());

	auto controller = MeSdk::Safe::Gui::TryFindTdPlayerController(true);
	auto world = MeSdk::Safe::Gui::TryFindActiveWorldInfo(true);

	MeSdk::Safe::Gui::WorldMenuState state = {};
	if (!MeSdk::Safe::Gui::TryReadWorldMenuState(controller, world, state)) {
		ImGui::TextDisabled("World state unavailable.");
		HarnessUi::Record("mm/multiplayer/world-tab", Engine::GetWindow());
		return;
	}

	if (state.inMainMenu) {
		// #region agent log
		MpDebugLog("menu.cpp:WorldTab", "main_menu_skip", "H-W", 0, 0,
		           controller ? 1 : 0);
		// #endregion
		ImGui::TextWrapped("World tools are available in-game.");
		HarnessUi::Record("mm/multiplayer/world-tab", Engine::GetWindow());
		return;
	}

	if (!world || !MeSdk::Safe::IsPlausibleUObject(world)) {
		// #region agent log
		MpDebugLog("menu.cpp:WorldTab", "no_world", "H-W");
		// #endregion
		ImGui::TextDisabled("World not available yet.");
		HarnessUi::Record("mm/multiplayer/world-tab", Engine::GetWindow());
		return;
	}

	float timeDilation = state.timeDilation;
	float gravity = state.worldGravityZ;
	if (ImGui::InputFloat("Time Dilation##world-time-dilation", &timeDilation) ||
	    ImGui::InputFloat("Gravity##-world-gravity", &gravity)) {
		MeSdk::Safe::Gui::TryWriteWorldScalars(world, timeDilation, gravity);
	}
	HarnessUi::Record("mm/multiplayer/world-time-dilation", Engine::GetWindow());
	HarnessUi::Record("mm/multiplayer/world-gravity", Engine::GetWindow());

	if (ImGui::TreeNode("world##world-levels", "%ws (%zu)", levelName.c_str(),
	                    state.streamingLevels.size())) {
		for (auto &entry : state.streamingLevels) {
			if (!entry.level || !MeSdk::Safe::IsPlausibleUObject(entry.level)) {
				continue;
			}

			bool check = entry.shouldBeLoaded;
			if (ImGui::Checkbox(entry.packageName.c_str(), &check) &&
			    check != entry.shouldBeLoaded) {
				MeSdk::Safe::Gui::TrySetStreamingLevelLoaded(entry.level, check);
			}
		}

		ImGui::TreePop();
	}
	HarnessUi::Record("mm/multiplayer/world-levels", Engine::GetWindow());
}

void Menu::AddTab(const char *name, MenuTabCallback callback) {
	if (const auto *host = ModHost::Get()) {
		if (host->AddTab) {
			host->AddTab(name, ModHost::WrapTabCallback(callback));
			return;
		}
	}

	tabs.push_back({name, callback});
}

void Menu::InsertTab(int index, const char *name, MenuTabCallback callback) {
	if (const auto *host = ModHost::Get()) {
		if (host->InsertTab) {
			host->InsertTab(index, name, ModHost::WrapTabCallback(callback));
			return;
		}
	}

	if (index < 0 || index > static_cast<int>(tabs.size())) {
		index = static_cast<int>(tabs.size());
	}

	tabs.insert(tabs.begin() + index, {name, callback});
}

void Menu::RemoveTab(const char *name) {
	if (const auto *host = ModHost::Get()) {
		if (host->RemoveTab) {
			host->RemoveTab(name);
			return;
		}
	}

	tabs.erase(std::remove_if(tabs.begin(), tabs.end(),
	                          [name](const MenuTab &tab) { return tab.Name == name; }),
	           tabs.end());
}

void Menu::Hide() {
	if (const auto *host = ModHost::Get()) {
		if (host->HideMenu) {
			host->HideMenu();
			return;
		}
	}

	show = false;
	Engine::BlockInput(false);
}

void Menu::Show() {
	if (const auto *host = ModHost::Get()) {
		if (host->ShowMenu) {
			host->ShowMenu();
			return;
		}
	}

	if (!Engine::ArePresentationHooksInstalled()) {
		return;
	}

	show = true;
	Engine::BlockInput(true);
}

bool Menu::IsOpen() {
	if (const auto *host = ModHost::Get()) {
		if (host->IsMenuOpen) {
			return host->IsMenuOpen();
		}
	}

	return show;
}

bool Menu::WantsOverlay() {
	if (ModHost::IsAttached()) {
		return showPlayerInfo;
	}

	return show || showPlayerInfo;
}

static bool IsMenuToggleDown() {
	if (showKeybind > 0 && showKeybind < 256 &&
	    (GetAsyncKeyState(showKeybind) & 0x8000)) {
		return true;
	}

	if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
		return true;
	}

	if (GetAsyncKeyState(VK_F10) & 0x8000) {
		return true;
	}

	return false;
}

static void PollMenuToggle() {
	if (ModHost::IsAttached()) {
		return;
	}

	static bool menuKeyWasDown = false;
	const bool menuKeyDown = IsMenuToggleDown();

	if (menuKeyDown && !menuKeyWasDown) {
		if (show) {
			Menu::Hide();
		} else if (Engine::ArePresentationHooksInstalled()) {
			Menu::Show();
		}
	}

	menuKeyWasDown = menuKeyDown;

	if (show) {
		static bool escapeWasDown = false;
		const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;

		if (escapeDown && !escapeWasDown) {
			Menu::Hide();
		}

		escapeWasDown = escapeDown;
	}
}

void Menu::PollToggle() { PollMenuToggle(); }

void Menu::RefreshSettings() {
	showKeybind = Settings::GetSetting("menu", "showKeybind", VK_INSERT).get<int>();
	if (showKeybind <= 0 || showKeybind >= 256) {
		showKeybind = VK_INSERT;
	}
	showPlayerInfo = Settings::GetSetting("player", "showInfo", false).get<bool>();
}

static void RenderMenuWrapped(IDirect3DDevice9 *device) {
	MeSdk::Safe::Gui::InvokeRenderOverlay(RenderMenu, device);
}

bool Menu::Initialize() {
	RefreshSettings();
	if (!ModHost::IsAttached()) {
		Engine::OnRenderScene(RenderMenuWrapped);
	}

	Engine::OnPostLevelLoad([](const wchar_t *newLevelName) {
		levelName = newLevelName;
	});

	AddTab("Engine", EngineTab);
	AddTab("World", WorldTab);

	return true;
}
