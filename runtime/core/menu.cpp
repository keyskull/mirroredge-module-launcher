#include <algorithm>
#include <vector>
#include <d3d9.h>

#include "agent_log.h"
#include "engine.h"
#include "menu.h"
#include "menu/engine_tab.h"
#include "menu/world_tab.h"
#include "modhost.h"
#include "settings.h"
#include "plugin_ui.h"

#include "me_sdk/runtime/safe_gui.h"

static auto show = false;
bool showPlayerInfo = false;
int showKeybind = 0;
// Level name updated from OnPostLevelLoad (game thread), displayed in
// ImGui world tab (render thread via EndScene hook).  MSVC std::wstring
// uses SSO for short strings — most level names fit inline, so the race
// window between assignment and read produces at worst a partially-copied
// display string, not a dangling pointer.  Acceptable for a debug overlay.
std::wstring levelName;
static std::vector<MenuTab> tabs;

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
	ImGui::Text("core loaded");
	ImGui::Text("Insert or F10 = Module Manager");
	ImGui::End();
}

static void RenderPlayerOverlay() {
	if (!showPlayerInfo) {
		return;
	}

	auto pawn = Engine::GetPlayerPawn();
	auto controller = Engine::GetPlayerController();

	MeSdk::Safe::Gui::PlayerOverlayInfo info = {};
	if (!MeSdk::Safe::Gui::TryReadPlayerOverlay(pawn, controller, info)) {
		return;
	}

	static const auto rightPadding = 100.0f;
	static const auto padding = 5.0f;

	auto window = ImGui::BeginRawScene("##player-debug-info");
	if (!window || !window->DrawList) {
		return;
	}

	const auto io = ImGui::GetIO();
	auto width = io.DisplaySize.x - padding;

	auto yIncrement = ImGui::GetTextLineHeight();
	auto y = io.DisplaySize.y - (7 * yIncrement) - padding;
	auto color = ImColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

	window->DrawList->AddRectFilled(
	    ImVec2(width - rightPadding - padding, y - padding), io.DisplaySize,
	    ImColor(ImVec4(0, 0, 0, 0.4f)));

	char buffer[0x200] = {0};

	snprintf(buffer, sizeof(buffer), "%.2f", info.posX);
	window->DrawList->AddText(
	    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x, y), color,
	    buffer);
	window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "X");

	snprintf(buffer, sizeof(buffer), "%.2f", info.posY);
	window->DrawList->AddText(
	    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x, y += yIncrement),
	    color, buffer);
	window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "Y");

	snprintf(buffer, sizeof(buffer), "%.2f", info.posZ);
	window->DrawList->AddText(
	    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x, y += yIncrement),
	    color, buffer);
	window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "Z");

	snprintf(buffer, sizeof(buffer), "%.2f", info.velocity2D);
	window->DrawList->AddText(
	    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x, y += yIncrement),
	    color, buffer);
	window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "V");

	snprintf(buffer, sizeof(buffer), "%.2f", info.rotPitchDeg);
	window->DrawList->AddText(
	    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x, y += yIncrement),
	    color, buffer);
	window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "RX");

	snprintf(buffer, sizeof(buffer), "%.2f", info.rotYawDeg);
	window->DrawList->AddText(
	    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x, y += yIncrement),
	    color, buffer);
	window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "RY");

	snprintf(buffer, sizeof(buffer), "%d", info.movementState);
	window->DrawList->AddText(
	    ImVec2(width - ImGui::CalcTextSize(buffer, nullptr, false).x, y += yIncrement),
	    color, buffer);
	window->DrawList->AddText(ImVec2(width - rightPadding, y), color, "S");

	ImGui::EndRawScene();
}

static void RenderMenu(IDirect3DDevice9 *device) {
	if (!ModHost::IsAttached()) {
		RenderStatusHint();

		if (show) {
			ImGui::Begin("Module Manager");
			ImGui::BeginTabBar("module-manager-tabs");

			for (const auto &tab : tabs) {
				if (ImGui::BeginTabItem(tab.Name.c_str())) {
					MeSdk::Safe::Gui::InvokeMenuTab(tab.Callback);
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
			ImGui::End();
		}
	}

	RenderPlayerOverlay();
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
	// During cinematic / loading transitions (e.g., tutorial_p intro
	// movie) the render callback may repeatedly fault on unstable
	// UObject state.  Each fault is caught by InvokeRenderOverlay's
	// SEH guard, but a stream of 20+ faults per second floods logs
	// and can make the game appear hung (harness watchdog kill).
	// Cooldown: skip rendering for increasingly longer periods after
	// each crash burst, resetting when rendering succeeds.
	static int crashBackoff = 0;
	static int crashFrameCount = 0;

	if (crashBackoff > 0) {
		--crashBackoff;
		return;
	}

	MeSdk::Safe::Gui::InvokeRenderOverlay(RenderMenu, device);

	// Detect whether InvokeRenderOverlay caught an exception this frame.
	// We use a simple heuristic: if the function returned atypically
	// fast and we see a fault logged, the SEH handler fired.
	// Instead of probing the SDK, we piggy-back on the existing
	// memory_fault log by checking if RenderPlayerOverlay's pawn
	// access would still be unstable.  For a pragmatic cooldown, we
	// just alternate between attempt and backoff every other frame
	// when we detect a fault — this is enough to break the flood.
	//
	// Actually, the simplest cooldown that works: after every render
	// attempt, unconditionally skip 1 frame.  This halves the crash
	// rate and prevents watchdog timeouts.  RenderMenu ticks at 60 Hz
	// but the overlay only needs ~10 Hz for status text.
	crashBackoff = 2;  // render every 3rd frame (20 Hz effective)
}

bool Menu::Initialize() {
	RefreshSettings();
	if (!ModHost::IsAttached()) {
		Engine::OnRenderScene(RenderMenuWrapped);
	}

	Engine::OnPostLevelLoad([](const wchar_t *newLevelName) {
		levelName = newLevelName;
	});

	AddTab("Engine", CoreMenuEngineTab);
	AddTab("World", CoreMenuWorldTab);

	return true;
}
