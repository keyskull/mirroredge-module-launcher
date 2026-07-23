#include "world_tab.h"

#include "agent_log.h"
#include "engine.h"
#include "ui_harness_plugin.h"

#include "me_sdk/runtime/safe_gui.h"

#include <string>

extern std::wstring levelName;

void CoreMenuWorldTab() {
	// #region agent log
	MpDebugLog("menu/world_tab.cpp:CoreMenuWorldTab", "tab_enter", "H-W");
	// #endregion

	ImGui::TextUnformatted("World");
	HarnessUi::Record("mm/multiplayer/world-tab", Engine::GetWindow());
	HarnessUi::RecordRect("mm/multiplayer/world-tab-anchor", 8.f, 8.f, 9.f, 9.f);

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
		MpDebugLog("menu/world_tab.cpp:CoreMenuWorldTab", "main_menu_skip", "H-W", 0, 0,
		           controller ? 1 : 0);
		// #endregion
		ImGui::TextWrapped("World tools are available in-game.");
		HarnessUi::Record("mm/multiplayer/world-tab", Engine::GetWindow());
		return;
	}

	if (!world || !MeSdk::Safe::IsPlausibleUObject(world)) {
		// #region agent log
		MpDebugLog("menu/world_tab.cpp:CoreMenuWorldTab", "no_world", "H-W");
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
