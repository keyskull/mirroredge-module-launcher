#include "stdafx.h"
#include "launcher_i18n.h"

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
		    LauncherI18n::T(LauncherI18n::Str::ReadyEventMissingFmt),
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
			StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::InitCompleteFmt), label);
			return true;
		}

		remaining -= step;
	}

	CloseHandle(readyEvent);

	StatusDialog::AppendLogf(
	    LauncherI18n::T(LauncherI18n::Str::InitTimeoutFmt),
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

	StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::WaitingGameProcess));
	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::WaitingProxyLoad));

	auto processInfo = FindProcessByName(config.gameProcessName.c_str());
	while (!processInfo.th32ProcessID && GetTickCount() < deadline) {
		if (StatusDialog::IsExitRequested()) {
			StatusDialog::AppendLog(LauncherI18n::T(LauncherI18n::Str::LauncherClosed));
			return false;
		}

		Sleep(LauncherTiming::kGameProcessPollMs);
		processInfo = FindProcessByName(config.gameProcessName.c_str());
	}

	if (!processInfo.th32ProcessID) {
		StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::GameProcessNotFound));
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::GameNotRunning));
		return false;
	}

	StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::FoundPidFmt),
	                       processInfo.th32ProcessID);

	InputRestore::BeginWatchingGame(processInfo.th32ProcessID);

	while (GetTickCount() < deadline) {
		if (StatusDialog::IsExitRequested()) {
			StatusDialog::AppendLog(LauncherI18n::T(LauncherI18n::Str::LauncherClosed));
			return false;
		}

		processInfo = FindProcessByName(config.gameProcessName.c_str());
		if (!processInfo.th32ProcessID) {
			StatusDialog::AppendLog(LauncherI18n::T(LauncherI18n::Str::GameProcessExited));
			return false;
		}

		if (IsManagerReadySignal(processInfo.th32ProcessID, config)) {
			StatusDialog::AppendLog(
			    LauncherI18n::T(LauncherI18n::Str::ManagerLoadedByProxy));
			return true;
		}

		DWORD uptimeMs = 0;
		GetProcessUptimeMs(processInfo.th32ProcessID, &uptimeMs);

		const auto now = GetTickCount();
		if (now - lastReportTick >= config.injectReportIntervalMs) {
			StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::WaitingManagerReady));
			StatusDialog::AppendLogf(
			    LauncherI18n::T(LauncherI18n::Str::WaitingManagerReportFmt),
			    (now - startTick) / 1000, uptimeMs / 1000);
			lastReportTick = now;
		}

		Sleep(LauncherTiming::kManagerReadyPollMs);
	}

	StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::WaitTimeout));
	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::ManagerWaitTimeoutHelp));
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

	StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::Done));
	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::ManagerReadyInjectHint));
	return true;
}
