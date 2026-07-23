#pragma once

#include <windows.h>

namespace InputRestore {

constexpr UINT kRestoreInputMessage = WM_APP + 5;
constexpr UINT_PTR kRestoreInputTimerId = 0x4D4D;

void BeginWatchingGame(DWORD processId);
void RestoreDesktopNow(HWND launcherWindow);
void RequestRestoreOnLauncherThread();
void ScheduleRestoreBurst(HWND launcherWindow);
bool HandleRestoreTimer(HWND launcherWindow);

} // namespace InputRestore
