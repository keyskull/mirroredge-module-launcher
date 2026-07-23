#include "stdafx.h"

#include "input_restore.h"

#include "timing_constants.h"
#include "win_input.h"

#include "ui/status_dialog.h"

#include <atomic>

std::atomic<DWORD> g_watchedPid{0};
std::atomic<int> g_restoreBurstRemaining{0};

DWORD WINAPI WatchGameExitThread(LPVOID param) {
	const DWORD pid = static_cast<DWORD>(reinterpret_cast<uintptr_t>(param));
	if (!pid) {
		return 0;
	}

	HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (process) {
		WaitForSingleObject(process, INFINITE);
		CloseHandle(process);
	} else {
		for (int i = 0; i < LauncherTiming::kWatchProcessMaxRetries; ++i) {
			Sleep(LauncherTiming::kWatchProcessRetryMs);
			process = OpenProcess(SYNCHRONIZE, FALSE, pid);
			if (process) {
				WaitForSingleObject(process, INFINITE);
				CloseHandle(process);
				break;
			}
		}
	}

	if (g_watchedPid.exchange(0) != pid) {
		return 0;
	}

	Sleep(LauncherTiming::kInputRestoreFirstDelayMs);
	InputRestore::RequestRestoreOnLauncherThread();
	Sleep(LauncherTiming::kInputRestoreSecondDelayMs);
	InputRestore::RequestRestoreOnLauncherThread();
	return 0;
}

namespace InputRestore {

void BeginWatchingGame(const DWORD processId) {
	if (!processId) {
		return;
	}

	g_watchedPid = processId;
	const auto thread =
	    CreateThread(nullptr, 0, WatchGameExitThread,
	                 reinterpret_cast<LPVOID>(static_cast<uintptr_t>(processId)), 0,
	                 nullptr);
	if (thread) {
		CloseHandle(thread);
	}
}

void RestoreDesktopNow(const HWND launcherWindow) {
	WinInput_RestoreDesktopInput(launcherWindow);
}

void RequestRestoreOnLauncherThread() {
	if (const HWND window = StatusDialog::GetMainWindow()) {
		PostMessageW(window, kRestoreInputMessage, 0, 0);
	} else {
		RestoreDesktopNow(nullptr);
	}
}

void ScheduleRestoreBurst(const HWND launcherWindow) {
	g_restoreBurstRemaining = LauncherTiming::kInputRestoreBurstCount;
	RestoreDesktopNow(launcherWindow);
	if (launcherWindow) {
		SetTimer(launcherWindow, kRestoreInputTimerId, LauncherTiming::kInputRestoreBurstIntervalMs, nullptr);
	}
}

bool HandleRestoreTimer(const HWND launcherWindow) {
	RestoreDesktopNow(launcherWindow);
	return g_restoreBurstRemaining.fetch_sub(1) > 0;
}

} // namespace InputRestore
