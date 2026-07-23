#include "stdafx.h"

#include "config.h"
#include "config_integrity_bypass.h"
#include "deploy_settings.h"
#include "game_path.h"
#include "process_util.h"
#include "timing_constants.h"
#include "ui/status_dialog.h"

#include <atomic>
#include <cstring>
#include <vector>

namespace {

constexpr DWORD kProcessAccess =
    PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
    PROCESS_QUERY_INFORMATION;

constexpr int kPatchWarnAfterFailures = 40;
// Patching during early CRT/init can hang MirrorsEdge.exe (watch thread polls every 50ms).
constexpr ULONGLONG kMinProcessAgeBeforePatchMs = 650;

std::atomic<bool> g_stop{false};
HANDLE g_watchThread = nullptr;
std::atomic<DWORD> g_patchedPid{0};
std::atomic<DWORD> g_warnedPid{0};

bool MaskCompare(const char *data, const char *pattern, const char *mask) {
	for (; *mask; ++mask, ++data, ++pattern) {
		if (*mask == 'x' && *data != *pattern) {
			return false;
		}
	}
	return true;
}

DWORD FindPatternInBuffer(const std::vector<char> &buffer, DWORD moduleBase,
                          const char *pattern, const char *mask) {
	const auto maskLen = strlen(mask);
	if (buffer.size() <= maskLen) {
		return 0;
	}

	const DWORD length =
	    static_cast<DWORD>(buffer.size()) - static_cast<DWORD>(maskLen);
	for (DWORD i = 0; i < length; ++i) {
		if (MaskCompare(&buffer[i], pattern, mask)) {
			return moduleBase + i;
		}
	}
	return 0;
}

bool GetMainModuleInfo(DWORD processId, DWORD &base, DWORD &size) {
	const auto snapshot =
	    CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return false;
	}

	MODULEENTRY32 entry = {sizeof(entry)};
	bool found = false;
	if (Module32First(snapshot, &entry)) {
		base = reinterpret_cast<DWORD>(entry.modBaseAddr);
		size = entry.modBaseSize;
		found = true;
	}

	CloseHandle(snapshot);
	return found;
}

bool WritePatch(HANDLE process, DWORD address, const void *bytes, size_t size) {
	DWORD oldProtect = 0;
	if (!VirtualProtectEx(process, reinterpret_cast<void *>(address), size,
	                      PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}

	SIZE_T written = 0;
	const auto ok =
	    WriteProcessMemory(process, reinterpret_cast<void *>(address), bytes, size,
	                       &written) != FALSE &&
	    written == size;

	DWORD ignored = 0;
	VirtualProtectEx(process, reinterpret_cast<void *>(address), size, oldProtect,
	                 &ignored);
	return ok;
}

bool GetProcessAgeMs(const DWORD processId, ULONGLONG &ageMs) {
	const auto process =
	    OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (!process) {
		return false;
	}

	FILETIME createTime = {};
	FILETIME exitTime = {};
	FILETIME kernelTime = {};
	FILETIME userTime = {};
	if (!GetProcessTimes(process, &createTime, &exitTime, &kernelTime,
	                     &userTime)) {
		CloseHandle(process);
		return false;
	}
	CloseHandle(process);

	FILETIME nowFt = {};
	GetSystemTimeAsFileTime(&nowFt);

	ULARGE_INTEGER now = {};
	now.LowPart = nowFt.dwLowDateTime;
	now.HighPart = nowFt.dwHighDateTime;

	ULARGE_INTEGER created = {};
	created.LowPart = createTime.dwLowDateTime;
	created.HighPart = createTime.dwHighDateTime;

	if (now.QuadPart <= created.QuadPart) {
		ageMs = 0;
		return true;
	}

	ageMs = (now.QuadPart - created.QuadPart) / 10000;
	return true;
}

bool IsReadyForConfigPatch(const DWORD processId) {
	ULONGLONG ageMs = 0;
	if (!GetProcessAgeMs(processId, ageMs)) {
		return false;
	}
	return ageMs >= kMinProcessAgeBeforePatchMs;
}

void WarnPatchFailedOnce(DWORD processId) {
	DWORD expected = g_warnedPid.load();
	if (expected == processId) {
		return;
	}
	if (!g_warnedPid.compare_exchange_strong(expected, processId)) {
		return;
	}

	StatusDialog::AppendLog(
	    L"\u8b66\u544a\uff1a\u5185\u7f6e\u914d\u7f6e\u5b8c\u6574\u6027\u7ed5\u8fc7\u672a\u80fd\u5e94\u7528\u3002"
	    L"\u82e5\u51fa\u73b0 Default*.ini corrupt \u5bf9\u8bdd\u6846\uff0c\u8bf7\u9a8c\u8bc1\u6e38\u620f\u6587\u4ef6"
	    L"\u6216\u786e\u8ba4 MirrorsEdge.exe \u7248\u672c\u4e0e\u542f\u52a8\u5668\u5339\u914d\u3002");
}

bool ApplyConfigIntegrityBypass(HANDLE process, const bool ignoreAgeGate) {
	const DWORD processId = GetProcessId(process);
	if (!ignoreAgeGate && !IsReadyForConfigPatch(processId)) {
		return false;
	}

	DWORD moduleBase = 0;
	DWORD moduleSize = 0;
	if (!GetMainModuleInfo(processId, moduleBase, moduleSize)) {
		return false;
	}

	std::vector<char> buffer(moduleSize);
	SIZE_T read = 0;
	if (!ReadProcessMemory(process, reinterpret_cast<void *>(moduleBase),
	                       buffer.data(), moduleSize, &read) ||
	    read == 0) {
		return false;
	}
	buffer.resize(static_cast<size_t>(read));

	const auto c1 = FindPatternInBuffer(
	    buffer, moduleBase,
	    "\xE8\x00\x00\x00\x00\x83\xC4\x04\x39\x00\x00\x00\x00\x00\x74\x05",
	    "x????xxxx?????xx");
	const auto c2 = FindPatternInBuffer(
	    buffer, moduleBase,
	    "\xE8\x00\x00\x00\x00\x83\xC4\x04\x39\x00\x00\x00\x00\x00\x74\x1E",
	    "x????xxxx?????xx");
	const auto c3 = FindPatternInBuffer(
	    buffer, moduleBase, "\x68\xFE\x7F\x00\x00\x8D\x44\x24\x06", "xxxxxxxxx");
	if (!c1 || !c2 || !c3) {
		return false;
	}

	static const unsigned char kNop5[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
	static const unsigned char kPatch3[7] = {0x81, 0xC4, 0x04, 0x80,
	                                       0x00, 0x00, 0xC3};

	return WritePatch(process, c1, kNop5, sizeof(kNop5)) &&
	       WritePatch(process, c2, kNop5, sizeof(kNop5)) &&
	       WritePatch(process, c3, kPatch3, sizeof(kPatch3));
}

DWORD WINAPI WatchThreadProc(LPVOID) {
	const auto &config = LauncherConfig::Get();
	DWORD trackedPid = 0;
	int failStreak = 0;

	while (!g_stop.load()) {
		const auto entry = FindProcessByName(config.gameProcessName.c_str());
		if (!entry.th32ProcessID) {
			g_patchedPid.store(0);
			g_warnedPid.store(0);
			trackedPid = 0;
			failStreak = 0;
			Sleep(LauncherTiming::kConfigBypassMonitorPollMs);
			continue;
		}

		if (g_patchedPid.load() == entry.th32ProcessID) {
			trackedPid = entry.th32ProcessID;
			failStreak = 0;
			Sleep(LauncherTiming::kConfigBypassMonitorPollMs);
			continue;
		}

		const auto process =
		    OpenProcess(kProcessAccess, FALSE, entry.th32ProcessID);
		if (process) {
			if (ApplyConfigIntegrityBypass(process, false)) {
				g_patchedPid.store(entry.th32ProcessID);
				trackedPid = entry.th32ProcessID;
				failStreak = 0;
			} else {
				if (entry.th32ProcessID == trackedPid) {
					++failStreak;
				} else {
					trackedPid = entry.th32ProcessID;
					failStreak = 1;
				}
				if (failStreak >= kPatchWarnAfterFailures) {
					WarnPatchFailedOnce(entry.th32ProcessID);
				}
			}
			CloseHandle(process);
		}

		Sleep(LauncherTiming::kConfigBypassMonitorPollMs);
	}
	return 0;
}

} // namespace

namespace ConfigIntegrityBypass {

bool IsBypassDisabled() {
	char value[8] = {};
	return GetEnvironmentVariableA("MMOD_DISABLE_CONFIG_BYPASS", value,
	                               static_cast<DWORD>(sizeof(value))) > 0 &&
	       value[0] == '1' && value[1] == '\0';
}

bool IsBypassForced() {
	char value[8] = {};
	return GetEnvironmentVariableA("MMOD_FORCE_CONFIG_BYPASS", value,
	                               static_cast<DWORD>(sizeof(value))) > 0 &&
	       value[0] == '1' && value[1] == '\0';
}

bool DefaultIniHeaderIntact(const std::wstring &path) {
	const HANDLE file =
	    CreateFileW(path.c_str(), GENERIC_READ,
	                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
	                FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	char buffer[96] = {};
	DWORD bytesRead = 0;
	const auto ok =
	    ReadFile(file, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &bytesRead,
	             nullptr) != FALSE;
	CloseHandle(file);
	if (!ok || bytesRead == 0) {
		return false;
	}

	buffer[bytesRead] = '\0';
	return strstr(buffer, "Don't modify") != nullptr;
}

bool NeedsConfigIntegrityBypass() {
	if (IsBypassDisabled()) {
		return false;
	}
	if (IsBypassForced()) {
		return true;
	}

	bool skipConfigIntegrityCheck = true;
	DeploySettings::MigrateLegacySettingsIfNeeded();
	DeploySettings::LoadSkipConfigIntegrityCheck(skipConfigIntegrityCheck, {});
	if (!skipConfigIntegrityCheck) {
		return false;
	}

	return true;
}

void BeginWatching() {
	if (!NeedsConfigIntegrityBypass()) {
		return;
	}
	if (g_watchThread) {
		return;
	}

	g_stop.store(false);
	g_watchThread = CreateThread(nullptr, 0, WatchThreadProc, nullptr, 0, nullptr);
}

void StopWatching() {
	g_stop.store(true);
	if (!g_watchThread) {
		return;
	}

	WaitForSingleObject(g_watchThread, 2000);
	CloseHandle(g_watchThread);
	g_watchThread = nullptr;
}

bool IsProcessPatched(const DWORD processId) {
	return processId != 0 && g_patchedPid.load() == processId;
}

bool TryApplyToProcess(const DWORD processId) {
	if (!processId) {
		return false;
	}
	if (IsProcessPatched(processId)) {
		return true;
	}

	const auto process = OpenProcess(kProcessAccess, FALSE, processId);
	if (!process) {
		return false;
	}

	const auto patched = ApplyConfigIntegrityBypass(process, false);
	CloseHandle(process);
	if (patched) {
		g_patchedPid.store(processId);
	}
	return patched;
}

bool TryApplyBeforeFirstRun(const DWORD processId) {
	if (!NeedsConfigIntegrityBypass()) {
		return false;
	}
	if (!processId) {
		return false;
	}
	if (IsProcessPatched(processId)) {
		return true;
	}

	const auto process = OpenProcess(kProcessAccess, FALSE, processId);
	if (!process) {
		return false;
	}

	const auto patched = ApplyConfigIntegrityBypass(process, true);
	CloseHandle(process);
	if (patched) {
		g_patchedPid.store(processId);
		StatusDialog::AppendLog(
		    L"\u914d\u7f6e\u5b8c\u6574\u6027\u7ed5\u8fc7\u5df2\u5728\u542f\u52a8\u524d\u5e94\u7528\u3002");
	}
	return patched;
}

void NotifyPatchFailed(const DWORD processId) {
	WarnPatchFailedOnce(processId);
}

} // namespace ConfigIntegrityBypass
