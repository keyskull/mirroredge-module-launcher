#pragma once

#include <vector>

#include <Windows.h>

#include <string>

struct ModuleUiEntry {
	std::wstring id;
	std::wstring dllPath;
	std::string status;
	bool loaded = false;
	bool busy = false;
	bool actionsEnabled = false;
	bool hasPluginInfo = false;
	unsigned pluginMajor = 0;
	unsigned pluginMinor = 0;
	unsigned pluginPatch = 0;
	std::string requiresId;
};

namespace ModRegistry {

void DiscoverModules();
void ProcessPendingOperations();
DWORD WaitForPendingOperations(DWORD timeoutMs);
void RenderModulesTab();
void CopyModuleSnapshot(std::vector<ModuleUiEntry> &out);
void QueueLoadFromUi(const std::wstring &moduleId);
void QueueUnloadFromUi(const std::wstring &moduleId);
bool IsModuleLoadInProgress(const ModuleUiEntry &entry);
bool IsLoaded(const wchar_t *moduleId);
bool TryGetModuleId(HMODULE module, std::wstring &moduleId);
bool RequestLoad(const wchar_t *moduleId, const char *source = "api",
                 std::string *rejectReason = nullptr);
bool RequestUnload(const wchar_t *moduleId);
void FormatModuleList(std::string &out);
void FormatStatusList(std::string &out);
void FormatStatusJson(std::string &out);

void BootstrapAutoLoad();

void TryQueueSettingsAutoLoadMods();

} // namespace ModRegistry
