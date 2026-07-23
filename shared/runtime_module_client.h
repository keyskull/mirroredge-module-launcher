#pragma once

#include <Windows.h>

namespace RuntimeModuleClient {

struct Binding {
	const wchar_t *subfolder;
	const wchar_t *dllFileName;
};

HMODULE FindLoaded(const wchar_t *dllFileName);
HMODULE FindHostModule();
HMODULE EnsureLoaded(const Binding &binding, HMODULE hostModule, HMODULE *cache = nullptr);
void *ResolveExport(const Binding &binding, HMODULE hostModule, const char *exportName,
                    HMODULE *cache = nullptr);

} // namespace RuntimeModuleClient
