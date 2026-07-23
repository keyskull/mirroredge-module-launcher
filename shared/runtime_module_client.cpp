#include "runtime_module_client.h"

#include "runtime_module_loader.h"

namespace RuntimeModuleClient {

HMODULE FindLoaded(const wchar_t *dllFileName) {
	return dllFileName ? GetModuleHandleW(dllFileName) : nullptr;
}

HMODULE FindHostModule() {
	const wchar_t *candidates[] = {L"core.dll", L"module_manager.dll"};
	for (const wchar_t *name : candidates) {
		const HMODULE host = GetModuleHandleW(name);
		if (host) {
			return host;
		}
	}
	return nullptr;
}

HMODULE EnsureLoaded(const Binding &binding, HMODULE hostModule, HMODULE *cache) {
	if (!binding.subfolder || !binding.dllFileName) {
		return nullptr;
	}

	if (const HMODULE loaded = FindLoaded(binding.dllFileName)) {
		if (cache) {
			*cache = loaded;
		}
		return loaded;
	}

	if (!hostModule) {
		hostModule = FindHostModule();
	}
	if (!hostModule) {
		return nullptr;
	}

	return RuntimeModuleLoader::EnsureSiblingModule(hostModule, binding.subfolder,
	                                                binding.dllFileName, cache);
}

void *ResolveExport(const Binding &binding, HMODULE hostModule, const char *exportName,
                    HMODULE *cache) {
	const HMODULE module = EnsureLoaded(binding, hostModule, cache);
	if (!module || !exportName) {
		return nullptr;
	}
	return reinterpret_cast<void *>(GetProcAddress(module, exportName));
}

} // namespace RuntimeModuleClient
