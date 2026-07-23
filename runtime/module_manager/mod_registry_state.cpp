#include "mod_registry_internal.h"

namespace ModRegistryInternal {

std::mutex g_mutex;
std::vector<ModuleEntry> g_modules;
std::wstring g_pendingUnloadId;
std::unordered_map<HMODULE, std::wstring> g_moduleHandles;

ModuleEntry *FindEntryById(const std::wstring &moduleId) {
	for (auto &entry : g_modules) {
		if (_wcsicmp(entry.id.c_str(), moduleId.c_str()) == 0) {
			return &entry;
		}
	}
	return nullptr;
}

} // namespace ModRegistryInternal
