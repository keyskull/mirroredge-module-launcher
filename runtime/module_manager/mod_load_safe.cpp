#include "mod_load_safe.h"

extern "C" {
int MmSehCallPluginInit(void *fn, const void *host, HMODULE module,
                        unsigned long *exceptionCode);
int MmSehCallPluginShutdown(void *fn, HMODULE module,
                            unsigned long *exceptionCode);
HMODULE MmSehLoadModuleLibrary(const wchar_t *dllPath,
                               unsigned long *exceptionCode);
}

namespace ModLoadSafe {

bool CallPluginInitialize(MMOD_PluginInitializeFn init, const ModHostApi *host,
                          HMODULE module, DWORD *exceptionCode) {
	if (!init || !host || !module) {
		if (exceptionCode) {
			*exceptionCode = 0;
		}
		return false;
	}

	unsigned long code = 0;
	const bool ok = MmSehCallPluginInit(reinterpret_cast<void *>(init), host,
	                                    module, &code) != 0;
	if (exceptionCode) {
		*exceptionCode = code;
	}
	return ok;
}

void CallPluginShutdown(MMOD_PluginShutdownFn shutdown, HMODULE module,
                        DWORD *exceptionCode) {
	if (!shutdown || !module) {
		if (exceptionCode) {
			*exceptionCode = 0;
		}
		return;
	}

	unsigned long code = 0;
	MmSehCallPluginShutdown(reinterpret_cast<void *>(shutdown), module, &code);
	if (exceptionCode) {
		*exceptionCode = code;
	}
}

HMODULE LoadModuleLibrary(const wchar_t *dllPath, DWORD *exceptionCode) {
	unsigned long code = 0;
	const HMODULE module = MmSehLoadModuleLibrary(dllPath, &code);
	if (exceptionCode) {
		*exceptionCode = code;
	}
	return module;
}

} // namespace ModLoadSafe
