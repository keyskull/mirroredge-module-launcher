#include "stdafx.h"

#include "config.h"
#include "injection_flow.h"
#include "process_util.h"
#include "input_restore.h"
#include "timing_constants.h"
#include "ui/status_dialog.h"

static bool SleepInterruptible(const DWORD totalMs) {
	const DWORD chunkMs = LauncherTiming::kSleepInterruptChunkMs;
	DWORD elapsed = 0;
	while (elapsed < totalMs) {
		if (StatusDialog::IsExitRequested()) {
			return false;
		}

		const DWORD step = min(chunkMs, totalMs - elapsed);
		Sleep(step);
		elapsed += step;
	}

	return true;
}

static bool WaitForReadyEvent(const std::wstring &eventName,
                              const wchar_t *label) {
	const auto &config = LauncherConfig::Get();

	if (eventName.empty()) {
		if (!SleepInterruptible(config.readyFallbackSleepMs)) {
			return false;
		}
		return true;
	}

	const auto readyEvent = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
	if (!readyEvent) {
		StatusDialog::AppendLogf(
		    L"\u672a\u627e\u5230 %s \u5c31\u7eea\u4e8b\u4ef6 %s\uff0c\u7b49\u5f85 %u \u79d2...",
		    label, eventName.c_str(), config.readyFallbackSleepMs / 1000);
		if (!SleepInterruptible(config.readyFallbackSleepMs)) {
			return false;
		}
		return true;
	}

	DWORD remaining = config.readyEventWaitMs;
	while (remaining > 0) {
		if (StatusDialog::IsExitRequested()) {
			CloseHandle(readyEvent);
			return false;
		}

		const DWORD step = min(LauncherTiming::kReadyEventPollStepMs, remaining);
		const auto waitResult = WaitForSingleObject(readyEvent, step);
		if (waitResult == WAIT_OBJECT_0) {
			CloseHandle(readyEvent);
			StatusDialog::AppendLogf(L"%s \u521d\u59cb\u5316\u5b8c\u6210\u3002", label);
			return true;
		}

		remaining -= step;
	}

	CloseHandle(readyEvent);

	StatusDialog::AppendLogf(
	    L"%s \u521d\u59cb\u5316\u8d85\u65f6\uff0c\u4f46\u4ee3\u7406\u52a0\u8f7d\u53ef\u80fd\u5df2\u6210\u529f\u3002",
	    label);
	return true;
}

static bool IsManagerReadySignal(DWORD processId,
                                 const LauncherConfig &config) {
	if (HasAnyModuleLoadedByPid(processId, config.managerDllNames)) {
		return true;
	}

	if (config.managerReadyEventName.empty()) {
		return false;
	}

	const auto readyEvent = OpenEventW(SYNCHRONIZE, FALSE,
	                                   config.managerReadyEventName.c_str());
	if (!readyEvent) {
		return false;
	}

	const auto signaled =
	    WaitForSingleObject(readyEvent, 0) == WAIT_OBJECT_0;
	CloseHandle(readyEvent);
	return signaled;
}

static bool WaitForManagerViaProxy() {
	const auto &config = LauncherConfig::Get();
	const auto startTick = GetTickCount();
	const auto deadline = startTick + config.injectWaitTimeoutMs;
	auto lastReportTick = startTick;

	StatusDialog::SetStep(L"\u7b49\u5f85\u6e38\u620f\u8fdb\u7a0b");
	StatusDialog::AppendLog(
	    L"\u7b49\u5f85\u6e38\u620f\u542f\u52a8\uff0c\u7531 d3d9 \u4ee3\u7406\u52a0\u8f7d module_manager.dll\u3002\n"
	    L"mmultiplayer \u8bf7\u5728\u6e38\u620f\u5185 Module Manager \u2192 Modules \u6807\u7b7e\u6ce8\u5165\u3002");

	auto processInfo = FindProcessByName(config.gameProcessName.c_str());
	while (!processInfo.th32ProcessID && GetTickCount() < deadline) {
		if (StatusDialog::IsExitRequested()) {
			StatusDialog::AppendLog(L"\u542f\u52a8\u5668\u5df2\u5173\u95ed\u3002");
			return false;
		}

		Sleep(LauncherTiming::kGameProcessPollMs);
		processInfo = FindProcessByName(config.gameProcessName.c_str());
	}

	if (!processInfo.th32ProcessID) {
		StatusDialog::SetStep(L"\u672a\u627e\u5230\u6e38\u620f\u8fdb\u7a0b");
		StatusDialog::AppendLog(
		    L"Mirror's Edge \u672a\u8fd0\u884c\u3002\u8bf7\u5148\u542f\u52a8\u6e38\u620f\u3002");
		return false;
	}

	StatusDialog::AppendLogf(L"\u627e\u5230\u8fdb\u7a0b PID %u\u3002",
	                       processInfo.th32ProcessID);

	InputRestore::BeginWatchingGame(processInfo.th32ProcessID);

	while (GetTickCount() < deadline) {
		if (StatusDialog::IsExitRequested()) {
			StatusDialog::AppendLog(L"\u542f\u52a8\u5668\u5df2\u5173\u95ed\u3002");
			return false;
		}

		processInfo = FindProcessByName(config.gameProcessName.c_str());
		if (!processInfo.th32ProcessID) {
			StatusDialog::AppendLog(L"\u6e38\u620f\u8fdb\u7a0b\u5df2\u9000\u51fa\u3002");
			return false;
		}

		if (IsManagerReadySignal(processInfo.th32ProcessID, config)) {
			StatusDialog::AppendLog(
			    L"module_manager \u5df2\u7531 d3d9 \u4ee3\u7406\u52a0\u8f7d\u5c31\u7eea\u3002");
			return true;
		}

		DWORD uptimeMs = 0;
		GetProcessUptimeMs(processInfo.th32ProcessID, &uptimeMs);

		const auto now = GetTickCount();
		if (now - lastReportTick >= config.injectReportIntervalMs) {
			StatusDialog::SetStep(L"\u7b49\u5f85 Module Manager \u5c31\u7eea");
			StatusDialog::AppendLogf(
			    L"[%u \u79d2] \u6e38\u620f\u5df2\u8fd0\u884c %u \u79d2\uff0c\u7b49\u5f85 d3d9 \u4ee3\u7406\u52a0\u8f7d module_manager...",
			    (now - startTick) / 1000, uptimeMs / 1000);
			lastReportTick = now;
		}

		Sleep(LauncherTiming::kManagerReadyPollMs);
	}

	StatusDialog::SetStep(L"\u7b49\u5f85\u8d85\u65f6");
	StatusDialog::AppendLog(
	    L"\u7b49\u5f85 module_manager \u8d85\u65f6\u3002\u8bf7\u786e\u8ba4\uff1a\n"
	    L"1. \u542f\u52a8\u5668\u5df2\u90e8\u7f72 Binaries\\d3d9.dll \u4ee3\u7406\n"
	    L"2. \u901a\u8fc7\u542f\u52a8\u5668\u542f\u52a8\u6e38\u620f\uff08\u4e0d\u8981\u5728\u90e8\u7f72\u524d\u624b\u52a8\u542f\u52a8\uff09\n"
	    L"3. \u6e38\u620f\u8fdb\u5165\u4e3b\u83dc\u5355\u540e\u7b49\u5f85\u56fe\u5f62\u521d\u59cb\u5316\u5b8c\u6210");
	return false;
}

bool RunLauncherFlow() {
	if (!WaitForManagerViaProxy()) {
		return false;
	}

	if (!WaitForReadyEvent(LauncherConfig::Get().managerReadyEventName,
	                       L"Module Manager")) {
		return false;
	}

	StatusDialog::SetStep(L"\u5b8c\u6210");
	StatusDialog::AppendLog(
	    L"module_manager \u5df2\u5c31\u7eea\u3002\n"
	    L"\u8bf7\u5728\u6e38\u620f\u5185\u6309 Insert/F10 \u6253\u5f00 Module Manager\uff0c\u5728 Modules \u6807\u7b7e\u6ce8\u5165 mmultiplayer\u3002\n"
	    L"\uff08\u5efa\u8bae\u4e3b\u83dc\u5355\u51fa\u73b0\u540e\u518d\u6ce8\u5165\uff09\u3002");
	return true;
}
