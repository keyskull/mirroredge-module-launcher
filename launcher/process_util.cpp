#include "stdafx.h"

#include "process_util.h"

PROCESSENTRY32 FindProcessByName(const wchar_t *name) {
	const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return {0};
	}

	PROCESSENTRY32 entry = {sizeof(entry)};
	PROCESSENTRY32 result = {0};

	if (Process32First(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, name) == 0) {
				result = entry;
				break;
			}
		} while (Process32Next(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return result;
}

bool IsProcessRunning(const wchar_t *name) {
	return FindProcessByName(name).th32ProcessID != 0;
}

bool HasLoadedModuleByPid(DWORD processId, const wchar_t *module) {
	const auto snapshot =
	    CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);

	if (snapshot == INVALID_HANDLE_VALUE) {
		return false;
	}

	MODULEENTRY32 entry = {sizeof(entry)};
	if (Module32First(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szModule, module) == 0) {
				CloseHandle(snapshot);
				return true;
			}
		} while (Module32Next(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return false;
}

bool HasAnyModuleLoadedByPid(DWORD processId,
                             const std::vector<std::wstring> &modules) {
	for (const auto &module : modules) {
		if (HasLoadedModuleByPid(processId, module.c_str())) {
			return true;
		}
	}
	return false;
}

bool GetProcessUptimeMs(DWORD processId, DWORD *uptimeMs) {
	if (!uptimeMs) {
		return false;
	}

	const auto process =
	    OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (!process) {
		return false;
	}

	FILETIME created = {};
	FILETIME ignored = {};
	if (!GetProcessTimes(process, &created, &ignored, &ignored, &ignored)) {
		CloseHandle(process);
		return false;
	}
	CloseHandle(process);

	ULARGE_INTEGER createdTime = {};
	createdTime.LowPart = created.dwLowDateTime;
	createdTime.HighPart = created.dwHighDateTime;

	FILETIME nowFileTime = {};
	GetSystemTimeAsFileTime(&nowFileTime);
	ULARGE_INTEGER now = {};
	now.LowPart = nowFileTime.dwLowDateTime;
	now.HighPart = nowFileTime.dwHighDateTime;

	*uptimeMs = static_cast<DWORD>((now.QuadPart - createdTime.QuadPart) / 10000);
	return true;
}

int ResumeAllProcessThreads(DWORD processId) {
	if (!processId) {
		return 0;
	}

	const auto snapshot =
	    CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	int resumed = 0;
	THREADENTRY32 entry = {sizeof(entry)};
	if (Thread32First(snapshot, &entry)) {
		do {
			if (entry.th32OwnerProcessID != processId) {
				continue;
			}

			const auto thread =
			    OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
			if (!thread) {
				continue;
			}

			for (int attempt = 0; attempt < 32; ++attempt) {
				const auto previous = ResumeThread(thread);
				if (previous == static_cast<DWORD>(-1)) {
					break;
				}
				if (previous > 0) {
					++resumed;
				}
				if (previous <= 1) {
					break;
				}
			}

			CloseHandle(thread);
		} while (Thread32Next(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return resumed;
}
