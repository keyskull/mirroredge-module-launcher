#pragma once

#include "mod_host_api.h"

#include <Windows.h>

namespace ModLoadSafe {

bool CallPluginInitialize(MMOD_PluginInitializeFn init, const ModHostApi *host,
                          HMODULE module, DWORD *exceptionCode);
void CallPluginShutdown(MMOD_PluginShutdownFn shutdown, HMODULE module,
                        DWORD *exceptionCode);
HMODULE LoadModuleLibrary(const wchar_t *dllPath, DWORD *exceptionCode);

} // namespace ModLoadSafe
