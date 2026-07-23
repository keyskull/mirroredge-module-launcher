#pragma once

#include "launcher_settings.h"

#include <string>

namespace GameConfig {

bool GetTdEngineIniPath(std::wstring &path);

// Writes TdEngine.ini + module_manager.settings.ini before game launch.
bool ApplyDisplaySettings(const DisplaySettings &settings, std::wstring *logLine);

} // namespace GameConfig
