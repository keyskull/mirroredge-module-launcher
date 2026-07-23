#include "menu_ui.h"

#include "mod_registry.h"
#include "module_contract.h"
#include "presentation.h"
#include "version.h"
#include "runtime_version.h"
#include "menu_state.h"

#include "imgui/imgui.h"
#include "ui_harness.h"

#include <Windows.h>

#include <mutex>
#include <string>
#include <vector>

namespace {

const char *QueryRuntimeVersion(HMODULE module) {
	if (!module) {
		return nullptr;
	}
	const auto getVersion = reinterpret_cast<MMOD_GetRuntimeVersionFn>(
	    GetProcAddress(module, MMOD_RUNTIME_VERSION_EXPORT));
	if (!getVersion) {
		return nullptr;
	}
	const MmodRuntimeVersion *info = getVersion();
	return info && info->string ? info->string : nullptr;
}

void RenderStatusHint() {
	if (HostMenuState::show) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.45f);
	if (ImGui::Begin("##mm-status", nullptr,
	                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
	                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
	                     ImGuiWindowFlags_NoFocusOnAppearing)) {
		ImGui::Text("Module Manager v%s", MMOD_MANAGER_VERSION_STRING);
		char productVersion[32] = {};
		if (GetEnvironmentVariableA(MMOD_PRODUCT_VERSION_ENV, productVersion,
		                            static_cast<DWORD>(sizeof(productVersion))) > 0 &&
		    productVersion[0]) {
			ImGui::Text("Launcher v%s", productVersion);
		}
		if (const char *engineVersion =
		        QueryRuntimeVersion(GetModuleHandleW(L"engine.dll"))) {
			ImGui::Text("Engine v%s", engineVersion);
		}
		if (const char *coreVersion = QueryRuntimeVersion(GetModuleHandleW(L"core.dll"))) {
			ImGui::Text("Core v%s", coreVersion);
		}
		ImGui::Text("Insert/F10 = menu | ` = console");
		ImGui::End();
	}
}

void RenderModulesTab() { ModRegistry::RenderModulesTab(); }

} // namespace

namespace HostMenuUi {

void RenderOverlay(IDirect3DDevice9 *) {
	RenderStatusHint();

	if (!HostMenuState::show) {
		return;
	}

	HarnessUi::BeginFrame();

	std::vector<HostMenuState::Tab> tabs;
	int pendingSelect = -1;
	{
		std::lock_guard<std::mutex> lock(HostMenuState::tabsMutex);
		tabs = HostMenuState::tabs;
		pendingSelect = HostMenuState::pendingTabSelect;
		HostMenuState::ClampActiveTabLocked();
	}

	if (tabs.empty()) {
		return;
	}

	if (!ImGui::Begin("Module Manager")) {
		return;
	}

	if (ImGui::BeginTabBar("module-manager-tabs")) {
		for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
			const auto &tab = tabs[static_cast<size_t>(i)];
			ImGuiTabItemFlags flags = 0;
			if (i == pendingSelect) {
				flags |= ImGuiTabItemFlags_SetSelected;
			}
			ImGui::PushID(i);
			const bool tabOpen = ImGui::BeginTabItem(tab.name.c_str(), nullptr, flags);
			{
				const std::string tabTarget = std::string("manager/tab:") + tab.name;
				HarnessUi::Record(tabTarget.c_str(), Presentation::GetGameWindow());
			}
			if (tabOpen) {
				{
					std::lock_guard<std::mutex> lock(HostMenuState::tabsMutex);
					HostMenuState::activeTab = i;
					if (i == HostMenuState::pendingTabSelect) {
						HostMenuState::pendingTabSelect = -1;
					}
				}
				HostMenuState::InvokeTabCallbackSafely(tab.callback);
				ImGui::EndTabItem();
			}
			ImGui::PopID();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}

HostMenuTabCallback ModulesTabCallback() { return RenderModulesTab; }

} // namespace HostMenuUi
