#pragma once

#include <Windows.h>

#include "memory_fault_log_c.h"

namespace PluginSehGuard {

using VoidThunk = void (*)(void *);

inline bool InvokeVoid(const char *context, const char *location, VoidThunk thunk,
                       void *data, DWORD *exceptionCode = nullptr) {
	if (exceptionCode) {
		*exceptionCode = 0;
	}
	if (!thunk) {
		return true;
	}

	__try {
		thunk(data);
		return true;
	} __except (MemoryFaultLog_Filter(GetExceptionInformation(), context,
	                                   location)) {
		if (exceptionCode) {
			*exceptionCode = GetExceptionCode();
		}
		return false;
	}
}

} // namespace PluginSehGuard
