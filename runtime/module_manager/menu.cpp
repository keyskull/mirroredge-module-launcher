#include "menu.h"
#include "menu_ui.h"

#include "mod_console.h"
#include "presentation.h"
#include "mod_log.h"
#include "menu_state.h"

#include <Windows.h>

namespace {

bool IsToggleDown() {
	if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
		return true;
	}
	if (GetAsyncKeyState(VK_F10) & 0x8000) {
		return true;
	}
	return false;
}

} // namespace

namespace HostMenu {

void AddTab(const char *name, HostMenuTabCallback callback) {
	if (!name || !name[0]) {
		return;
	}
	std::lock_guard<std::mutex> lock(HostMenuState::tabsMutex);
	HostMenuState::tabs.push_back({name, callback});
	HostMenuState::ClampActiveTabLocked();
}

void InsertTab(int index, const char *name, HostMenuTabCallback callback) {
	if (!name || !name[0]) {
		return;
	}
	std::lock_guard<std::mutex> lock(HostMenuState::tabsMutex);
	if (index < 0 || index > static_cast<int>(HostMenuState::tabs.size())) {
		index = static_cast<int>(HostMenuState::tabs.size());
	}
	HostMenuState::tabs.insert(HostMenuState::tabs.begin() + index, {name, callback});
	if (HostMenuState::activeTab >= index) {
		++HostMenuState::activeTab;
	}
	if (HostMenuState::pendingTabSelect >= index) {
		++HostMenuState::pendingTabSelect;
	}
	HostMenuState::ClampActiveTabLocked();
}

void RemoveTab(const char *name) {
	if (!name || !name[0]) {
		return;
	}
	std::lock_guard<std::mutex> lock(HostMenuState::tabsMutex);
	for (size_t i = 0; i < HostMenuState::tabs.size(); ++i) {
		if (HostMenuState::tabs[i].name != name) {
			continue;
		}
		HostMenuState::tabs.erase(HostMenuState::tabs.begin() + i);
		if (HostMenuState::activeTab > i) {
			--HostMenuState::activeTab;
		} else if (HostMenuState::activeTab == i) {
			HostMenuState::activeTab = 0;
		}
		if (HostMenuState::pendingTabSelect == i) {
			HostMenuState::pendingTabSelect = -1;
		} else if (HostMenuState::pendingTabSelect > i) {
			--HostMenuState::pendingTabSelect;
		}
		HostMenuState::ClampActiveTabLocked();
		return;
	}
}

void Show() {
	if (!Presentation::AreHooksInstalled()) {
		ModLog::Write(
		    "module_manager: overlay not ready - focus game window, wait for "
		    "main menu, then retry Insert/F10");
		return;
	}
	HostMenuState::show = true;
	if (Presentation::IsOverlayReady()) {
		HostMenu_SetBlockInput(true);
	}
}

void Hide() {
	HostMenuState::show = false;
	if (!ModConsole::IsOpen()) {
		HostMenu_SetBlockInput(false);
	}
}

bool IsOpen() { return HostMenuState::show; }

void SetBlockInput(bool block) { HostMenu_SetBlockInput(block); }

void PollToggle() {
	static bool wasDown = false;
	const bool down = IsToggleDown();

	if (down && !wasDown) {
		if (HostMenuState::show) {
			Hide();
		} else {
			Show();
		}
	}

	wasDown = down;

	if (HostMenuState::show) {
		static bool escapeWasDown = false;
		const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
		if (escapeDown && !escapeWasDown) {
			Hide();
		}
		escapeWasDown = escapeDown;
	}
}

void Render(IDirect3DDevice9 *device) { HostMenuUi::RenderOverlay(device); }

void Initialize() { InsertTab(0, "Modules", HostMenuUi::ModulesTabCallback()); }

void GetTabNames(std::vector<std::string> &out) {
	out.clear();
	std::lock_guard<std::mutex> lock(HostMenuState::tabsMutex);
	out.reserve(HostMenuState::tabs.size());
	for (const auto &tab : HostMenuState::tabs) {
		out.push_back(tab.name);
	}
}

const char *GetActiveTabName() {
	static thread_local std::string activeName;
	std::lock_guard<std::mutex> lock(HostMenuState::tabsMutex);
	HostMenuState::ClampActiveTabLocked();
	if (HostMenuState::activeTab < 0 ||
	    HostMenuState::activeTab >= static_cast<int>(HostMenuState::tabs.size())) {
		activeName.clear();
		return "";
	}
	activeName = HostMenuState::tabs[static_cast<size_t>(HostMenuState::activeTab)].name;
	return activeName.c_str();
}

bool SelectTab(const char *name) {
	if (!name || !name[0]) {
		return false;
	}

	std::lock_guard<std::mutex> lock(HostMenuState::tabsMutex);
	for (size_t i = 0; i < HostMenuState::tabs.size(); ++i) {
		if (HostMenuState::tabs[i].name == name) {
			HostMenuState::activeTab = i;
			HostMenuState::pendingTabSelect = i;
			return true;
		}
	}

	return false;
}

} // namespace HostMenu
