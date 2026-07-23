#pragma once

#include <Windows.h>

namespace EngineLoader {

// Load modules/engine/engine.dll relative to this core module.
bool EnsureLoaded(HMODULE coreModule);
HMODULE GetModule();

} // namespace EngineLoader
