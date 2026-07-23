#include "mod_registry.h"

#include "debug_trace.h"
#include "menu.h"
#include "mod_console.h"
#include "mod_registry_internal.h"
#include "presentation.h"

#include "imgui/imgui.h"
#include "ui_harness.h"

#include <Windows.h>

#include <string>
#include <vector>

namespace ModRegistry {

void RenderModulesTab() {
	// #region agent log
	static DWORD lastUiLog = 0;
	const DWORD now = GetTickCount();
	if (now - lastUiLog > 1000) {
		lastUiLog = now;
		std::vector<ModuleUiEntry> snapshot;
		CopyModuleSnapshot(snapshot);
		DebugTrace::SessionEvent("mod_registry_ui.cpp:RenderModulesTab", "ui_tick", "H5",
		                         static_cast<uintptr_t>(snapshot.size()),
		                         HostMenu::IsOpen() ? 1u : 0u, 0);
	}
	// #endregion

	if (ImGui::Button("Refresh list")) {
		DiscoverModules();
	}

	ImGui::SameLine();
	ImGui::TextDisabled("Third-party mods from modules\\<id>\\ only (core auto-loads).");

	std::vector<ModuleUiEntry> snapshot;
	CopyModuleSnapshot(snapshot);

	if (snapshot.empty()) {
		ImGui::TextWrapped("No injectable modules found under modules\\.");
		return;
	}

	ImGui::Separator();

	bool anyLoading = false;
	for (const auto &entry : snapshot) {
		if (IsModuleLoadInProgress(entry)) {
			anyLoading = true;
			break;
		}
	}

	const float listHeight = ImGui::GetContentRegionAvail().y;
	if (listHeight > 80.0f) {
		ImGui::BeginChild("module_list", ImVec2(0.0f, listHeight - 4.0f), false);
	}

	for (const auto &entry : snapshot) {
		ImGui::PushID(entry.id.c_str());

		ImGui::Text("%s", ModRegistryInternal::WideToUtf8(entry.id).c_str());
		ImGui::SameLine();
		if (entry.hasPluginInfo) {
			ImGui::TextDisabled("v%u.%u.%u", entry.pluginMajor, entry.pluginMinor,
			                      entry.pluginPatch);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", ModRegistryInternal::WideToUtf8(entry.dllPath).c_str());
		if (entry.hasPluginInfo && !entry.requiresId.empty()) {
			ImGui::TextDisabled("Requires: %s", entry.requiresId.c_str());
		}

		const bool inProgress = IsModuleLoadInProgress(entry);
		const bool failed =
		    !entry.loaded && !inProgress && entry.status != "Not loaded";
		if (failed) {
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Status: %s",
			                   entry.status.c_str());
		} else if (inProgress) {
			ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "Status: %s",
			                   entry.status.c_str());
			ImGui::SameLine();
			const float t = static_cast<float>(ImGui::GetTime());
			const char spinner[] = "|/-\\";
			ImGui::Text("%c", spinner[static_cast<int>(t * 8.0f) % 4]);
		} else if (entry.loaded) {
			ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "Status: %s",
			                   entry.status.c_str());
		} else {
			ImGui::Text("Status: %s", entry.status.c_str());
		}

		const bool canAct = entry.actionsEnabled;
		if (!entry.loaded) {
			if (!canAct) {
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
			}
			const bool clicked = ImGui::Button("Inject");
			{
				char moduleIdUtf8[128] = {};
				WideCharToMultiByte(CP_UTF8, 0, entry.id.c_str(), -1, moduleIdUtf8,
				                    static_cast<int>(sizeof(moduleIdUtf8)), nullptr,
				                    nullptr);
				const std::string injectTarget =
				    std::string("manager/inject/") + moduleIdUtf8;
				const ImVec2 rectMin = ImGui::GetItemRectMin();
				const ImVec2 rectMax = ImGui::GetItemRectMax();
				if (rectMax.x > rectMin.x && rectMax.y > rectMin.y) {
					HarnessUi::RecordRect(injectTarget.c_str(), rectMin.x, rectMin.y,
					                      rectMax.x, rectMax.y);
				}
			}
			const bool hovered = ImGui::IsItemHovered();
			const bool deactivated = ImGui::IsItemDeactivated();
			// #region agent log
			if (ImGui::IsMouseReleased(0)) {
				const auto &io = ImGui::GetIO();
				DebugTrace::SessionEvent("mod_registry_ui.cpp:RenderModulesTab",
				                         "inject_mouse_up", "H7", hovered ? 1u : 0u,
				                         static_cast<uintptr_t>(io.MousePos.x),
				                         static_cast<int>(io.MousePos.y));
			}
			// #endregion
			if (clicked) {
				// #region agent log
				DebugTrace::SessionEvent("mod_registry_ui.cpp:RenderModulesTab",
				                         "inject_click", "H1", canAct ? 1u : 0u,
				                         entry.busy ? 1u : 0u,
				                         static_cast<int>(entry.status.size()));
				// #endregion
			}
			const bool releaseClick =
			    hovered && (deactivated || ImGui::IsMouseReleased(0));
			if (releaseClick && !clicked) {
				// #region agent log
				DebugTrace::SessionEvent("mod_registry_ui.cpp:RenderModulesTab",
				                         "inject_fallback", "H7", canAct ? 1u : 0u, 0,
				                         0);
				// #endregion
			}
			if ((clicked || releaseClick) && canAct) {
				QueueLoadFromUi(entry.id);
			}
			if (!canAct) {
				ImGui::PopStyleVar();
			}
		} else {
			if (!canAct) {
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
			}
			if (ImGui::Button("Unload") && canAct) {
				QueueUnloadFromUi(entry.id);
			}
			if (!canAct) {
				ImGui::PopStyleVar();
			}
		}

		ImGui::Separator();
		ImGui::PopID();
	}

	if (listHeight > 80.0f) {
		ImGui::EndChild();
	}

	if (anyLoading) {
		ImGui::Spacing();
		ImGui::TextDisabled("Load progress");
		ImGui::BeginChild("module_load_log", ImVec2(0.0f, 120.0f), true);
		ModConsole::RenderRecent(12);
		ImGui::EndChild();
	}
}

} // namespace ModRegistry
