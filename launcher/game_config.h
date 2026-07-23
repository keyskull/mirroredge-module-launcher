#pragma once

#include "launcher_settings.h"

#include <string>

namespace GameConfig {

struct ConfigSyncStatus {
	bool tdEngineOk = false;
	bool tdContentOk = false;
	bool windowLayoutOk = false;
	bool displayMatched = false;
	bool proxyPresent = false;
	bool managerPresent = false;
	bool deployMatched = false;
	std::wstring detail;
};

bool GetTdEngineIniPath(std::wstring &path);

// Writes TdEngine.ini + module_manager.settings.ini before game launch.
bool ApplyDisplaySettings(const DisplaySettings &settings, std::wstring *logLine);

// Reads what is currently on disk (best-effort; borderless vs windowed is ambiguous).
bool ReadAppliedDisplaySettings(DisplaySettings &out, int *appliedResX,
                                int *appliedResY);

// Compares expected launcher settings against TdEngine.ini, window layout ini,
// and deployed d3d9 / module_manager files under the game root.
ConfigSyncStatus EvaluateConfigSync(const DisplaySettings &expected);

} // namespace GameConfig
