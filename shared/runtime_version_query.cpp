#include "runtime_version_query.h"

#include <cstdio>
#include <cstring>

namespace RuntimeVersionQuery {

const MmodRuntimeVersion *QueryLoadedModule(const wchar_t *moduleFileName) {
	if (!moduleFileName) {
		return nullptr;
	}

	const HMODULE module = GetModuleHandleW(moduleFileName);
	if (!module) {
		return nullptr;
	}

	const auto getVersion = reinterpret_cast<MMOD_GetRuntimeVersionFn>(
	    GetProcAddress(module, MMOD_RUNTIME_VERSION_EXPORT));
	if (!getVersion) {
		return nullptr;
	}

	return getVersion();
}

void FormatComponentVersionsLine(char *out, size_t outSize) {
	if (!out || outSize == 0) {
		return;
	}

	auto appendPart = [&](const char *label, const MmodRuntimeVersion *info) {
		char segment[96] = {};
		if (info && info->string) {
			snprintf(segment, sizeof(segment), "%s%s %s", out[0] ? " | " : "", label,
			         info->string);
		} else {
			snprintf(segment, sizeof(segment), "%s%s --", out[0] ? " | " : "", label);
		}
		const size_t remaining = outSize - strlen(out) - 1;
		if (remaining > 0) {
			strncat(out, segment, remaining);
			out[outSize - 1] = '\0';
		}
	};

	out[0] = '\0';
	appendPart("mm-console", QueryLoadedModule(L"mm-console.dll"));
	appendPart("module_manager", QueryLoadedModule(L"module_manager.dll"));
	appendPart("core", QueryLoadedModule(L"core.dll"));
	appendPart("engine", QueryLoadedModule(L"engine.dll"));
}

} // namespace RuntimeVersionQuery
