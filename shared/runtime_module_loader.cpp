#include "runtime_module_loader.h"

namespace RuntimeModuleLoader {

std::wstring ResolveSiblingModulePath(HMODULE hostModule, const wchar_t *subfolder,
                                      const wchar_t *dllFileName) {
	wchar_t selfPath[MAX_PATH] = {};
	if (!hostModule || !GetModuleFileNameW(hostModule, selfPath, MAX_PATH)) {
		return {};
	}

	std::wstring path(selfPath);
	const auto slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos) {
		return {};
	}

	const auto folderSlash = path.rfind(L'\\', slash - 1);
	if (folderSlash == std::wstring::npos) {
		return {};
	}

	const std::wstring modulesRoot = path.substr(0, folderSlash);
	return modulesRoot + L"\\" + subfolder + L"\\" + dllFileName;
}

HMODULE EnsureSiblingModule(HMODULE hostModule, const wchar_t *subfolder,
                            const wchar_t *dllFileName, HMODULE *cached) {
	if (cached && *cached) {
		return *cached;
	}

	const std::wstring modulePath =
	    ResolveSiblingModulePath(hostModule, subfolder, dllFileName);
	if (modulePath.empty()) {
		return nullptr;
	}

	const HMODULE loaded = LoadLibraryW(modulePath.c_str());
	if (cached && loaded) {
		*cached = loaded;
	}
	return loaded;
}

} // namespace RuntimeModuleLoader
