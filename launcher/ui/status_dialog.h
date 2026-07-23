#pragma once

namespace StatusDialog {
using LaunchGameCallback = bool (*)();
using CloseGameCallback = bool (*)();

void SetLaunchGameHandler(LaunchGameCallback handler);
void SetCloseGameHandler(CloseGameCallback handler);
void Create();
void Destroy();
void SetStep(const wchar_t *text);
void AppendLog(const wchar_t *text);
void AppendLogf(const wchar_t *format, ...);
void AppendModLog(const wchar_t *text);
void MarkFinished(bool success);
void RequestExit();
bool IsExitRequested();
void RefreshGamePathDisplay();
void SaveDisplaySettingsFromUi();
void StartUpdateCheck(bool manual);
HWND GetMainWindow();
int RunUntilUserCloses();
} // namespace StatusDialog
