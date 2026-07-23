#include "loader.h"

#include "mod_log.h"
#include "modhost.h"
#include "mod_host_api.h"
#include "settings.h"

#include "json.h"

#include <Windows.h>

#include <string>
#include <vector>

namespace {

std::wstring Utf8ToWide(const std::string &text) {
	if (text.empty()) {
		return {};
	}

	const int needed =
	    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (needed <= 0) {
		return {};
	}

	std::vector<wchar_t> buffer(static_cast<size_t>(needed), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buffer.data(), needed);
	if (!buffer.empty() && buffer.back() == L'\0') {
		buffer.pop_back();
	}
	return std::wstring(buffer.begin(), buffer.end());
}

bool IsSkippableModuleId(const std::wstring &moduleId) {
	return _wcsicmp(moduleId.c_str(), L"core") == 0 ||
	       _wcsicmp(moduleId.c_str(), L"mm-core") == 0 ||
	       _wcsicmp(moduleId.c_str(), L"mmultiplayer") == 0 ||
	       _wcsicmp(moduleId.c_str(), L"engine") == 0 ||
	       _wcsicmp(moduleId.c_str(), L"mm-engine") == 0 ||
	       _wcsicmp(moduleId.c_str(), L"mm-console") == 0 ||
	       _wcsicmp(moduleId.c_str(), L"module_manager") == 0;
}

} // namespace

namespace Loader {

void ApplyAutoLoadModules() {
	ModLog::Write("loader: autoModules are configured in settings.json (mods.autoLoad)");
}

} // namespace Loader
