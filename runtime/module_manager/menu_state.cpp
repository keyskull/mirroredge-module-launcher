#include "menu_state.h"

#include "me_sdk/runtime/safe_gui_invoke.h"

namespace HostMenuState {

bool show = false;
int activeTab = 0;
int pendingTabSelect = -1;
std::vector<Tab> tabs;
std::mutex tabsMutex;

void ClampActiveTabLocked() {
	if (tabs.empty()) {
		activeTab = 0;
		pendingTabSelect = -1;
		return;
	}
	if (activeTab < 0 || activeTab >= static_cast<int>(tabs.size())) {
		activeTab = 0;
	}
	if (pendingTabSelect >= static_cast<int>(tabs.size())) {
		pendingTabSelect = -1;
	}
}

void InvokeTabCallbackSafely(HostMenuTabCallback callback) {
	MeSdk::Safe::Gui::InvokeMenuTab(callback);
}

} // namespace HostMenuState
