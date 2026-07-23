#include <Windows.h>

#include "memory_fault_log_c.h"

typedef int(__cdecl *MmPluginInitFn)(const void *host, HMODULE self);
typedef void(__cdecl *MmPluginShutdownFn)(HMODULE self);
typedef HMODULE(WINAPI *MmLoadLibraryExWFn)(LPCWSTR, HANDLE, DWORD);

int MmSehCallPluginInit(MmPluginInitFn fn, const void *host, HMODULE module,
                        unsigned long *exceptionCode) {
	if (!fn || !host || !module) {
		if (exceptionCode) {
			*exceptionCode = 0;
		}
		return 0;
	}

	__try {
		return fn(host, module) ? 1 : 0;
	} __except (MemoryFaultLog_Filter(GetExceptionInformation(), "plugin_init",
	                                   "mod_load_safe_seh.c:MmSehCallPluginInit")) {
		if (exceptionCode) {
			*exceptionCode = GetExceptionCode();
		}
		return 0;
	}
}

int MmSehCallPluginShutdown(MmPluginShutdownFn fn, HMODULE module,
                           unsigned long *exceptionCode) {
	if (!fn || !module) {
		if (exceptionCode) {
			*exceptionCode = 0;
		}
		return 0;
	}

	__try {
		fn(module);
		return 1;
	} __except (MemoryFaultLog_Filter(
	    GetExceptionInformation(), "plugin_shutdown",
	    "mod_load_safe_seh.c:MmSehCallPluginShutdown")) {
		if (exceptionCode) {
			*exceptionCode = GetExceptionCode();
		}
		return 0;
	}
}

HMODULE MmSehLoadModuleLibrary(LPCWSTR dllPath, unsigned long *exceptionCode) {
	if (exceptionCode) {
		*exceptionCode = 0;
	}
	if (!dllPath || !dllPath[0]) {
		return NULL;
	}

	HMODULE module = NULL;
	__try {
		MmLoadLibraryExWFn loadEx = (MmLoadLibraryExWFn)GetProcAddress(
		    GetModuleHandleW(L"kernel32.dll"), "LoadLibraryExW");
		if (loadEx) {
			module = loadEx(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		} else {
			module = LoadLibraryW(dllPath);
		}
	} __except (MemoryFaultLog_Filter(GetExceptionInformation(), "load_library",
	                                   "mod_load_safe_seh.c:MmSehLoadModuleLibrary")) {
		if (exceptionCode) {
			*exceptionCode = GetExceptionCode();
		}
		return NULL;
	}

	return module;
}
