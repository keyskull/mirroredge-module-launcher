#pragma once

#include <string>
#include <vector>

PROCESSENTRY32 FindProcessByName(const wchar_t *name);
bool IsProcessRunning(const wchar_t *name);
bool HasLoadedModuleByPid(DWORD processId, const wchar_t *module);
bool HasAnyModuleLoadedByPid(DWORD processId,
                             const std::vector<std::wstring> &modules);
bool GetProcessUptimeMs(DWORD processId, DWORD *uptimeMs);
int ResumeAllProcessThreads(DWORD processId);
