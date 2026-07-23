#pragma once

#include "module_contract.h"

#include <string>
#include <vector>

struct LauncherConfig {
	std::wstring gameProcessName = L"MirrorsEdge.exe";
	std::wstring gameExecutable = L"MirrorsEdge.exe";
	std::wstring gameBinariesSubdir = L"Binaries";
	std::wstring windowTitleHint = L"Mirror";

	std::wstring managerReadyEventName = MMOD_MANAGER_READY_EVENT_NAME;
	std::wstring moduleLogPipeName = MMOD_LOG_PIPE_NAME;

	std::wstring graphicsProxyDllName = L"d3d9.dll";
	std::wstring graphicsProxyBackup = L"d3d9.dll.mmproxy.bak";

	std::vector<std::wstring> managerDllNames = {MMOD_MANAGER_DLL_FILENAME};
	std::vector<std::wstring> managerSearchSubdirs = {
	    MMOD_MANAGER_DEPLOY_SUBDIR,
	    L"modules\\module_manager",
	    L"dist\\modules\\module_manager",
	    L"dist",
	    L"."};

	DWORD injectWaitTimeoutMs = 180000;
	DWORD injectReportIntervalMs = 5000;
	DWORD readyEventWaitMs = 30000;
	DWORD readyFallbackSleepMs = 10000;

	static const LauncherConfig &Get();
};
