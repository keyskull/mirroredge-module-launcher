#pragma once

#include "menu.h"

#include <mutex>
#include <string>
#include <vector>

namespace HostMenuState {

struct Tab {
	std::string name;
	HostMenuTabCallback callback = nullptr;
};

extern bool show;
extern int activeTab;
extern int pendingTabSelect;
extern std::vector<Tab> tabs;
extern std::mutex tabsMutex;

void ClampActiveTabLocked();
void InvokeTabCallbackSafely(HostMenuTabCallback callback);

} // namespace HostMenuState
