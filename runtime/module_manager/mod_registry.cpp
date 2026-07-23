#include "mod_registry.h"
#include "mod_registry_internal.h"

#include "debug_trace.h"
#include "mod_console.h"
#include "mod_log.h"
#include "mod_security.h"

#include <Windows.h>

#include <string>
#include <vector>

using ModRegistryInternal::FindEntryById;
using ModRegistryInternal::g_mutex;
using ModRegistryInternal::g_modules;
using ModRegistryInternal::IsInProgressStatus;
using ModRegistryInternal::LoadPhase;
using ModRegistryInternal::ModuleEntry;
using ModRegistryInternal::PhaseName;
using ModRegistryInternal::ProcessPendingOperationsImpl;
using ModRegistryInternal::QueueLoadModule;
using ModRegistryInternal::QueueUnloadModule;
using ModRegistryInternal::ValidatePluginDependenciesLocked;
using ModRegistryInternal::WaitForPendingOperationsImpl;

namespace ModRegistry {

void ProcessPendingOperations() { ProcessPendingOperationsImpl(); }

DWORD WaitForPendingOperations(DWORD timeoutMs) {
	return WaitForPendingOperationsImpl(timeoutMs);
}

void DiscoverModules() {
	std::lock_guard<std::mutex> lock(g_mutex);
	ModRegistryInternal::DiscoverModulesLocked();
}

bool IsLoaded(const wchar_t *moduleId) {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (const auto *entry = FindEntryById(moduleId)) {
		return entry->loaded;
	}
	return false;
}

bool TryGetModuleId(HMODULE module, std::wstring &moduleId) {
	std::lock_guard<std::mutex> lock(g_mutex);
	const auto it = ModRegistryInternal::g_moduleHandles.find(module);
	if (it == ModRegistryInternal::g_moduleHandles.end()) {
		return false;
	}
	moduleId = it->second;
	return true;
}

bool RequestLoad(const wchar_t *moduleId, const char *source,
                 std::string *rejectReasonOut) {
	if (!moduleId || !moduleId[0]) {
		return false;
	}

	if (ModSecurity::IsBuiltinModuleId(moduleId)) {
		if (rejectReasonOut) {
			*rejectReasonOut = "core is auto-loaded";
		}
		ModLog::Writef("mod_registry: inject rejected %ls via %s (core is auto-loaded)",
		               moduleId, source ? source : "unknown");
		return false;
	}

	bool canQueue = false;
	std::string rejectReason;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (auto *entry = FindEntryById(moduleId)) {
			if (entry->busy) {
				rejectReason = "busy";
			} else if (entry->loaded) {
				rejectReason = "already loaded";
			} else if (entry->loadPhase != LoadPhase::Idle) {
				rejectReason = std::string("phase=") + PhaseName(entry->loadPhase);
			} else {
				const auto depError = ValidatePluginDependenciesLocked(*entry);
				if (!depError.empty()) {
					rejectReason = depError;
				} else {
					canQueue = true;
				}
			}
		} else {
			rejectReason = "not found";
		}
	}

	if (!canQueue) {
		ModLog::Writef("mod_registry: inject rejected %ls via %s (%s)",
		               moduleId, source ? source : "unknown",
		               rejectReason.c_str());
		if (rejectReasonOut) {
			*rejectReasonOut = rejectReason;
		}
		return false;
	}

	QueueLoadModule(moduleId, source);
	return true;
}

bool RequestUnload(const wchar_t *moduleId) {
	if (!moduleId || !moduleId[0]) {
		return false;
	}

	if (ModSecurity::IsBuiltinModuleId(moduleId)) {
		ModLog::Write("mod_registry: unload rejected (core is auto-loaded)");
		return false;
	}

	bool canQueue = false;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (const auto *entry = FindEntryById(moduleId)) {
			canQueue = entry->loaded && !entry->busy;
		}
	}

	if (!canQueue) {
		return false;
	}

	QueueUnloadModule(moduleId);
	return true;
}

void FormatModuleList(std::string &out) {
	ModRegistryInternal::FormatModuleList(out);
}

void FormatStatusList(std::string &out) {
	ModRegistryInternal::FormatStatusList(out);
}

void FormatStatusJson(std::string &out) {
	ModRegistryInternal::FormatStatusJson(out);
}

void CopyModuleSnapshot(std::vector<ModuleUiEntry> &out) {
	out.clear();
	std::lock_guard<std::mutex> lock(g_mutex);
	out.reserve(g_modules.size());
	for (const auto &entry : g_modules) {
		if (ModSecurity::IsBuiltinModuleId(entry.id)) {
			continue;
		}
		ModuleUiEntry row = {};
		row.id = entry.id;
		row.dllPath = entry.dllPath;
		row.status = entry.status;
		row.loaded = entry.loaded;
		row.busy = entry.busy;
		row.hasPluginInfo = entry.hasPluginInfo;
		row.pluginMajor = entry.pluginMajor;
		row.pluginMinor = entry.pluginMinor;
		row.pluginPatch = entry.pluginPatch;
		row.requiresId = entry.requiresId;
		row.actionsEnabled = !entry.busy && entry.loadPhase == LoadPhase::Idle;
		out.push_back(std::move(row));
	}
}

void QueueLoadFromUi(const std::wstring &moduleId) {
	QueueLoadModule(moduleId, "ui");
}

void QueueUnloadFromUi(const std::wstring &moduleId) {
	QueueUnloadModule(moduleId);
}

bool IsModuleLoadInProgress(const ModuleUiEntry &entry) {
	return entry.busy || IsInProgressStatus(entry.status);
}

} // namespace ModRegistry
