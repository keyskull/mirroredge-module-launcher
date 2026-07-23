#pragma once

#include <Windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "mod_host_api.h"

namespace ModRegistryInternal {

enum class LoadPhase : uint8_t {
	Idle,
	Queued,
	Validating,
	PrepDeps,
	LoadLibrary,
	ResolveExport,
	PluginInit,
};

struct ModuleEntry {
	std::wstring id;
	std::wstring dllPath;
	HMODULE module = nullptr;
	bool loaded = false;
	bool busy = false;
	std::string status;
	LoadPhase loadPhase = LoadPhase::Idle;
	MMOD_PluginInitializeFn pendingInit = nullptr;
	DWORD loadStartedTick = 0;
	bool hasPluginInfo = false;
	unsigned pluginMajor = 0;
	unsigned pluginMinor = 0;
	unsigned pluginPatch = 0;
	std::string pluginDisplayName;
	std::string requiresId;
	unsigned requiresMinVersion = 0;
};

extern std::mutex g_mutex;
extern std::vector<ModuleEntry> g_modules;
extern std::wstring g_pendingUnloadId;
extern std::unordered_map<HMODULE, std::wstring> g_moduleHandles;

ModuleEntry *FindEntryById(const std::wstring &moduleId);
void SetLoadStatus(ModuleEntry &entry, const char *status);
const char *PhaseName(LoadPhase phase);
void LogPhaseTransition(ModuleEntry &entry, LoadPhase from, LoadPhase to);
bool IsInProgressStatus(const std::string &status);
void FailLoad(ModuleEntry &entry, const std::string &status);
bool PickModuleDll(const std::wstring &folder, const std::wstring &moduleId,
                   std::wstring &dllPath);
bool TryReadPluginInfo(HMODULE module, ModuleEntry &entry);
void ProbePluginInfoFromDll(ModuleEntry &entry);
bool GetLoadedPluginVersion(const std::string &moduleId, unsigned &packedVersion);
std::string ValidatePluginDependenciesLocked(const ModuleEntry &entry);
void DiscoverModulesLocked();
void EnsureModuleEntryLocked(const std::wstring &moduleId);
void AdvanceLoadPhase(ModuleEntry &entry);
bool UnloadModuleLocked(ModuleEntry &entry);
void ProcessPendingOperationsImpl();
void SignalPendingOperations();
DWORD WaitForPendingOperationsImpl(DWORD timeoutMs);
void QueueLoadModule(const std::wstring &moduleId, const char *source);
void QueueUnloadModule(const std::wstring &moduleId);

void FormatModuleList(std::string &out);
void FormatStatusList(std::string &out);
void FormatStatusJson(std::string &out);

void AppendJsonEscaped(std::string &out, const char *value);
std::string WideToUtf8(const std::wstring &text);

} // namespace ModRegistryInternal
