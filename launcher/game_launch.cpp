#include "stdafx.h"

#include "config.h"
#include "game_config.h"
#include "game_launch.h"
#include "game_path.h"
#include "input_restore.h"
#include "launcher_settings.h"
#include "config_integrity_bypass.h"
#include "diagnostics_session.h"
#include "module_contract.h"
#include "paths.h"
#include "product_version.h"
#include "process_util.h"
#include "timing_constants.h"
#include "ui/status_dialog.h"

#include <shellapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

static std::wstring FormatWin32Error(DWORD error) {
	wchar_t *buffer = nullptr;
	const auto length = FormatMessageW(
	    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
	        FORMAT_MESSAGE_IGNORE_INSERTS,
	    nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	    reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
	if (length == 0 || !buffer) {
		return L"";
	}

	std::wstring message(buffer, length);
	LocalFree(buffer);
	while (!message.empty() &&
	       (message.back() == L'\r' || message.back() == L'\n')) {
		message.pop_back();
	}
	return message;
}

static bool BackupAndCopyFile(const std::wstring &sourcePath,
                              const std::wstring &destinationPath,
                              const std::wstring &backupPath) {
	if (PathFileExistsW(destinationPath.c_str())) {
		if (PathFileExistsW(backupPath.c_str())) {
			DeleteFileW(backupPath.c_str());
		}
		if (!MoveFileW(destinationPath.c_str(), backupPath.c_str())) {
			return false;
		}
	}

	return CopyFileW(sourcePath.c_str(), destinationPath.c_str(), FALSE) != FALSE;
}

static bool ArePathsEquivalent(const std::wstring &left,
                               const std::wstring &right) {
	return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

static bool GetFileSizeBytes(const std::wstring &path, ULONGLONG &size) {
	WIN32_FILE_ATTRIBUTE_DATA data = {};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
		return false;
	}

	ULARGE_INTEGER value = {};
	value.LowPart = data.nFileSizeLow;
	value.HighPart = data.nFileSizeHigh;
	size = value.QuadPart;
	return true;
}

static bool DeployManagerDependency(const std::wstring &gameBinariesDirectory) {
	std::wstring managerSource;
	if (!Paths::ResolveManagerDll(managerSource)) {
		StatusDialog::AppendLog(
		    L"\u8b66\u544a\uff1a\u672a\u627e\u5230 module_manager.dll\u3002"
		    L"\u8bf7\u5148\u8fd0\u884c build.ps1 \u90e8\u7f72\u3002");
		return false;
	}

	wchar_t gameRoot[MAX_PATH] = {};
	wcscpy(gameRoot, gameBinariesDirectory.c_str());
	if (!PathRemoveFileSpecW(gameRoot)) {
		return false;
	}

	const auto modulesDir = std::wstring(gameRoot) + L"\\modules";
	const auto managerDir = modulesDir + L"\\module_manager";
	CreateDirectoryW(modulesDir.c_str(), nullptr);
	CreateDirectoryW(managerDir.c_str(), nullptr);

	const auto destinationPath =
	    managerDir + L"\\" + MMOD_MANAGER_DLL_FILENAME;
	if (ArePathsEquivalent(managerSource, destinationPath)) {
		return true;
	}

	if (CopyFileW(managerSource.c_str(), destinationPath.c_str(), FALSE)) {
		StatusDialog::AppendLogf(
		    L"\u5df2\u90e8\u7f72 module_manager.dll: %s",
		    destinationPath.c_str());
		return true;
	}

	const DWORD error = GetLastError();
	if (error == ERROR_SHARING_VIOLATION &&
	    PathFileExistsW(destinationPath.c_str())) {
		ULONGLONG sourceSize = 0;
		ULONGLONG destinationSize = 0;
		if (GetFileSizeBytes(managerSource, sourceSize) &&
		    GetFileSizeBytes(destinationPath, destinationSize) &&
		    sourceSize > 0 && sourceSize == destinationSize) {
			return true;
		}

		StatusDialog::AppendLog(
		    L"\u8b66\u544a\uff1amodule_manager.dll \u88ab\u5360\u7528\u4e14\u7248\u672c\u53ef\u80fd\u4e0d\u540c\u3002"
		    L"\u8bf7\u5148\u5173\u95ed\u6e38\u620f\u540e\u91cd\u542f\u542f\u52a8\u5668\u4ee5\u66f4\u65b0\u3002");
		return true;
	}

	StatusDialog::AppendLogf(
	    L"\u8b66\u544a\uff1a\u65e0\u6cd5\u90e8\u7f72 module_manager.dll (\u9519\u8bef %u)\u3002",
	    error);
	return false;
}

static bool DeployGraphicsProxy(const std::wstring &gameDirectory) {
	const auto &config = LauncherConfig::Get();

	std::wstring proxySource;
	if (!Paths::ResolveGraphicsProxyDll(proxySource)) {
		StatusDialog::AppendLog(
		    L"\u8b66\u544a\uff1a\u672a\u627e\u5230 d3d9 \u4ee3\u7406 DLL\u3002"
		    L"\u8bf7\u5148\u8fd0\u884c build.ps1 \u6216\u5c06 dist\\d3d9.dll \u653e\u5728\u542f\u52a8\u5668\u76ee\u5f55\u3002");
		return false;
	}

	const auto destinationPath =
	    gameDirectory + L"\\" + config.graphicsProxyDllName;
	const auto backupPath = gameDirectory + L"\\" + config.graphicsProxyBackup;

	if (!BackupAndCopyFile(proxySource, destinationPath, backupPath)) {
		StatusDialog::AppendLogf(
		    L"\u8b66\u544a\uff1a\u65e0\u6cd5\u90e8\u7f72 d3d9 \u4ee3\u7406 (\u9519\u8bef %u)\u3002",
		    GetLastError());
		return false;
	}

	StatusDialog::AppendLogf(L"\u5df2\u90e8\u7f72 d3d9 \u4ee3\u7406: %s",
	                       destinationPath.c_str());
	return true;
}

static HANDLE DuplicateExplorerPrimaryToken() {
	const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return nullptr;
	}

	PROCESSENTRY32W entry = {sizeof(entry)};
	HANDLE explorerProcess = nullptr;

	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, L"explorer.exe") != 0) {
				continue;
			}

			explorerProcess =
			    OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, entry.th32ProcessID);
			if (explorerProcess) {
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);
	if (!explorerProcess) {
		return nullptr;
	}

	HANDLE sessionToken = nullptr;
	if (!OpenProcessToken(explorerProcess, TOKEN_DUPLICATE | TOKEN_QUERY,
	                      &sessionToken)) {
		CloseHandle(explorerProcess);
		return nullptr;
	}
	CloseHandle(explorerProcess);

	HANDLE primaryToken = nullptr;
	if (!DuplicateTokenEx(sessionToken, TOKEN_ALL_ACCESS, nullptr,
	                      SecurityImpersonation, TokenPrimary, &primaryToken)) {
		CloseHandle(sessionToken);
		return nullptr;
	}

	CloseHandle(sessionToken);
	return primaryToken;
}

static bool LaunchWithCreateProcess(const std::wstring &gameExe,
                                    const std::wstring &gameDirectory,
                                    const std::wstring &extraArgs,
                                    HANDLE userToken, DWORD *lastError) {
	STARTUPINFOW startupInfo = {sizeof(startupInfo)};
	PROCESS_INFORMATION processInfo = {};
	std::wstring commandLine = L"\"" + gameExe + L"\"";
	if (!extraArgs.empty()) {
		commandLine += L" " + extraArgs;
	}
	std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
	commandBuffer.push_back(L'\0');

	const bool needsBypass = ConfigIntegrityBypass::NeedsConfigIntegrityBypass();
	const DWORD createFlags = needsBypass ? CREATE_SUSPENDED : 0;
	const auto created =
	    userToken ? CreateProcessAsUserW(
	                    userToken, nullptr, commandBuffer.data(), nullptr, nullptr,
	                    FALSE, createFlags, nullptr, gameDirectory.c_str(),
	                    &startupInfo, &processInfo)
	              : CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr,
	                               FALSE, createFlags, nullptr, gameDirectory.c_str(),
	                               &startupInfo, &processInfo);

	if (!created) {
		if (lastError) {
			*lastError = GetLastError();
		}
		return false;
	}

	if (needsBypass) {
		struct PrimaryThreadResumeGuard {
			HANDLE thread = nullptr;
			bool resumed = false;
			explicit PrimaryThreadResumeGuard(HANDLE t) : thread(t) {}
			~PrimaryThreadResumeGuard() { Resume(); }
			void Resume() {
				if (!resumed && thread) {
					ResumeThread(thread);
					resumed = true;
				}
			}
		} resumeGuard(processInfo.hThread);

		bool patched = false;
		for (int attempt = 0; attempt < LauncherTiming::kConfigBypassMaxRetries && !patched; ++attempt) {
			patched =
			    ConfigIntegrityBypass::TryApplyBeforeFirstRun(processInfo.dwProcessId);
			if (!patched) {
				Sleep(LauncherTiming::kConfigBypassRetryMs);
			}
		}
		if (!patched) {
			StatusDialog::AppendLog(
			    L"\u8b66\u544a\uff1a\u542f\u52a8\u524d\u914d\u7f6e\u7ed5\u8fc7\u672a\u5e94\u7528\uff0c\u540e\u53f0\u76d1\u63a7\u5c06\u91cd\u8bd5\u3002");
		}
		resumeGuard.Resume();
		const auto resumed = ResumeAllProcessThreads(processInfo.dwProcessId);
		if (resumed > 0) {
			StatusDialog::AppendLogf(
			    L"\u5df2\u6062\u590d\u6e38\u620f\u8fdb\u7a0b %u \u4e2a\u6302\u8d77\u7ebf\u7a0b\u3002",
			    resumed);
		}
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	return true;
}

static bool LaunchWithShellExecute(const std::wstring &gameExe,
                                   const std::wstring &gameDirectory,
                                   const std::wstring &extraArgs,
                                   DWORD *lastError) {
	const wchar_t *params =
	    extraArgs.empty() ? nullptr : extraArgs.c_str();
	const auto result =
	    reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", gameExe.c_str(),
	                                            params, gameDirectory.c_str(),
	                                            SW_SHOWNORMAL));
	if (result > 32) {
		return true;
	}

	if (lastError) {
		*lastError = static_cast<DWORD>(result);
	}
	return false;
}

static bool StartGameProcess(const std::wstring &gameExe,
                             const std::wstring &gameDirectory,
                             const DisplaySettings &displaySettings) {
	DWORD error = 0;
	std::wstring extraArgs;
	if (displaySettings.skipStartupMovies) {
		extraArgs = L"-nomoviestartup -nomovies";
	}

	// Allow test harness to launch game directly into a map via env var.
	wchar_t mapEnv[256] = {};
	const auto mapEnvLen = GetEnvironmentVariableW(
	    L"MMOD_GAME_MAP", mapEnv, static_cast<DWORD>(std::size(mapEnv)));
	if (mapEnvLen > 0) {
		if (!extraArgs.empty()) {
			extraArgs += L" ";
		}
		extraArgs += mapEnv;
	}

#ifdef _DEBUG
	// Diagnostic: write expected command line to temp file
	{
		const auto cmdLine = L"\"" + gameExe + L"\" " + extraArgs;
		wchar_t diagPath[MAX_PATH] = {};
		if (GetEnvironmentVariableW(L"TEMP", diagPath,
		                            static_cast<DWORD>(std::size(diagPath))) > 0) {
			wcscat(diagPath, L"\\launcher_args_diag.txt");
			FILE *f = nullptr;
			_wfopen_s(&f, diagPath, L"w,ccs=UTF-8");
			if (f) {
				fwprintf(f, L"%s\n", cmdLine.c_str());
				fwprintf(f, L"MMOD_GAME_MAP=%s (len=%lu)\n",
				         mapEnvLen > 0 ? mapEnv : L"<not set>", mapEnvLen);
				fclose(f);
			}
		}
	}
#endif

	if (LaunchWithCreateProcess(gameExe, gameDirectory, extraArgs, nullptr,
	                            &error)) {
		return true;
	}

	StatusDialog::AppendLogf(
	    L"\u76f4\u63a5\u542f\u52a8\u5931\u8d25 (\u9519\u8bef %u: %s)\uff0c\u5c1d\u8bd5\u4ee5\u666e\u901a\u7528\u6237\u6743\u9650\u542f\u52a8...",
	    error, FormatWin32Error(error).c_str());

	if (const auto explorerToken = DuplicateExplorerPrimaryToken()) {
		const auto launched = LaunchWithCreateProcess(
		    gameExe, gameDirectory, extraArgs, explorerToken, &error);
		CloseHandle(explorerToken);
		if (launched) {
			StatusDialog::AppendLog(
			    L"\u5df2\u4ee5\u666e\u901a\u7528\u6237\u6743\u9650\u542f\u52a8\u6e38\u620f\u3002");
			return true;
		}
	}

	if (LaunchWithShellExecute(gameExe, gameDirectory, extraArgs, &error)) {
		StatusDialog::AppendLog(L"\u5df2\u901a\u8fc7 Shell \u542f\u52a8\u6e38\u620f\u3002");
		return true;
	}

	StatusDialog::AppendLogf(L"\u542f\u52a8\u5931\u8d25 (\u9519\u8bef %u: %s)",
	                       error, FormatWin32Error(error).c_str());
	StatusDialog::AppendLog(
	    L"\u53ef\u5c1d\u8bd5\u624b\u52a8\u542f\u52a8 Binaries\\MirrorsEdge.exe\uff0c"
	    L"\u7136\u540e\u8ba9\u542f\u52a8\u5668\u7b49\u5f85\u6ce8\u5165\u3002");
	return false;
}

static void WarnIfSecuRomPresent(const std::wstring &gameExe) {
	if (!GamePath::HasSecuRomProtection(gameExe)) {
		return;
	}

	StatusDialog::AppendLog(
	    L"\u8b66\u544a\uff1a\u68c0\u6d4b\u5230\u65e7\u7248 SecuROM \u4fdd\u62a4\uff08\u53ef\u80fd\u51fa\u73b0\u300c\u672a\u63d2\u5165 "
	    L"CD/DVD\u300d\u9519\u8bef\uff09\u3002");
	StatusDialog::AppendLog(
	    L"  Steam\uff1a\u5728\u5e93\u4e2d\u53f3\u952e\u6e38\u620f \u2192 \u5c5e\u6027 \u2192 \u672c\u5730\u6587\u4ef6 "
	    L"\u2192 \u9a8c\u8bc1\u6e38\u620f\u6587\u4ef6\u5b8c\u6574\u6027\uff08\u4f1a\u66ff\u6362\u4e3a\u65e0 "
	    L"SecuROM \u7248\u672c\uff09\u3002");
	StatusDialog::AppendLog(
	    L"  EA/\u96f6\u552e\u7248\uff1a\u8bf7\u4ece EA App \u91cd\u65b0\u5b89\u88c5\uff0c\u6216\u4f7f\u7528 "
	    L"PCGamingWiki \u7684 Mirror's Edge Origin Fix \u8865\u4e01\u3002");
	StatusDialog::AppendLog(
	    L"  \u8be6\u89c1 docs/troubleshooting.md \u2192 \u300c\u672a\u63d2\u5165 CD/DVD\u300d\u3002");
}

bool PrepareGameEnvironment() {
	const auto &config = LauncherConfig::Get();

	if (IsProcessRunning(config.gameProcessName.c_str())) {
		StatusDialog::AppendLog(
		    L"Mirror's Edge \u5df2\u5728\u8fd0\u884c\uff0c\u8df3\u8fc7\u542f\u52a8\u524d\u51c6\u5907\u3002\n"
		    L"\u82e5\u9700\u66f4\u65b0\u6a21\u7ec4\u6587\u4ef6\uff0c\u8bf7\u5148\u5173\u95ed\u6e38\u620f\u540e\u91cd\u542f\u542f\u52a8\u5668\u3002");
		return true;
	}

	std::wstring gameDirectory;
	if (!Paths::GetGameBinariesDirectory(gameDirectory)) {
		StatusDialog::AppendLog(
		    L"\u672a\u627e\u5230\u6e38\u620f Binaries \u76ee\u5f55\uff0c\u8df3\u8fc7\u542f\u52a8\u524d\u51c6\u5907\u3002");
		return false;
	}

	StatusDialog::SetStep(L"\u51c6\u5907\u6e38\u620f\u6587\u4ef6");
	DeployManagerDependency(gameDirectory);

	const auto displaySettings = LauncherSettings::LoadDisplaySettings();
	std::wstring applyLog;
	if (GameConfig::ApplyDisplaySettings(displaySettings, &applyLog)) {
		StatusDialog::AppendLog(applyLog.c_str());
	} else if (!applyLog.empty()) {
		StatusDialog::AppendLogf(L"\u8b66\u544a\uff1a%s", applyLog.c_str());
	}

	return DeployGraphicsProxy(gameDirectory);
}

bool LaunchGameExecutable() {
	const auto &config = LauncherConfig::Get();

	if (IsProcessRunning(config.gameProcessName.c_str())) {
		StatusDialog::AppendLog(
		    L"Mirror's Edge \u5df2\u5728\u8fd0\u884c\uff0c\u65e0\u9700\u91cd\u590d\u542f\u52a8\u3002");
		return true;
	}

	std::wstring gameDirectory;
	if (!Paths::GetGameBinariesDirectory(gameDirectory)) {
		std::wstring launcherDirectory;
		Paths::GetLauncherDirectory(launcherDirectory);
		StatusDialog::AppendLogf(
		    L"\u672a\u627e\u5230\u6e38\u620f\u3002\u8bf7\u70b9\u51fb\u300c\u6d4f\u89c8\u300d\u9009\u62e9\u6e38\u620f\u76ee\u5f55"
		    L"\uff08\u542b Binaries\\%s\uff09\u3002",
		    config.gameExecutable.c_str());
		StatusDialog::AppendLogf(L"\u542f\u52a8\u5668\u4f4d\u7f6e: %s",
		                       launcherDirectory.c_str());
		return false;
	}

	const auto gameExe = gameDirectory + L"\\" + config.gameExecutable;
	if (!PathFileExistsW(gameExe.c_str())) {
		StatusDialog::AppendLogf(
		    L"\u672a\u627e\u5230\u6e38\u620f\u53ef\u6267\u884c\u6587\u4ef6: %s\\%s",
		    gameDirectory.c_str(), config.gameExecutable.c_str());
		return false;
	}

	WarnIfSecuRomPresent(gameExe);

	StatusDialog::AppendLogf(L"\u6e38\u620f\u76ee\u5f55: %s", gameDirectory.c_str());
	StatusDialog::AppendLog(
	    L"\u6b63\u5728\u542f\u52a8 Mirror's Edge\uff08\u5185\u7f6e\u914d\u7f6e\u5b8c\u6574\u6027\u68c0\u67e5\u7ed5\u8fc7\uff09...");

	wchar_t gameRoot[MAX_PATH] = {};
	wcsncpy(gameRoot, gameDirectory.c_str(), MAX_PATH - 1);
	gameRoot[MAX_PATH - 1] = L'\0';
	PathRemoveFileSpecW(gameRoot);
	SetEnvironmentVariableA(MMOD_PRODUCT_VERSION_ENV, MMOD_PRODUCT_VERSION_STRING);
	DiagnosticsSession::PrepareForGameLaunch(gameRoot);

	const auto displaySettings = LauncherSettings::LoadDisplaySettings();
	if (!StartGameProcess(gameExe, gameDirectory, displaySettings)) {
		return false;
	}

	const auto entry = FindProcessByName(config.gameProcessName.c_str());
	if (entry.th32ProcessID) {
		InputRestore::BeginWatchingGame(entry.th32ProcessID);
	}

	StatusDialog::AppendLog(
	    L"\u6e38\u620f\u5df2\u542f\u52a8\uff0c\u8bf7\u7b49\u5f85\u4e3b\u83dc\u5355\u51fa\u73b0\u3002");
	return true;
}

bool CloseGameExecutable() {
	const auto &config = LauncherConfig::Get();

	if (!IsProcessRunning(config.gameProcessName.c_str())) {
		StatusDialog::AppendLog(
		    L"Mirror's Edge \u672a\u5728\u8fd0\u884c\uff0c\u65e0\u9700\u5173\u95ed\u3002");
		return true;
	}

	StatusDialog::AppendLog(L"\u6b63\u5728\u5f3a\u5236\u5173\u95ed\u6e38\u620f...");

	int closedCount = 0;
	while (true) {
		const auto entry = FindProcessByName(config.gameProcessName.c_str());
		if (!entry.th32ProcessID) {
			break;
		}

		const auto process =
		    OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
		if (!process) {
			StatusDialog::AppendLogf(L"\u65e0\u6cd5\u6253\u5f00\u6e38\u620f\u8fdb\u7a0b (\u9519\u8bef %u)\u3002",
			                       GetLastError());
			return false;
		}

		const auto terminated = TerminateProcess(process, 0) != FALSE;
		CloseHandle(process);
		if (!terminated) {
			StatusDialog::AppendLogf(L"\u65e0\u6cd5\u7ec8\u6b62\u6e38\u620f\u8fdb\u7a0b (\u9519\u8bef %u)\u3002",
			                       GetLastError());
			return false;
		}

		++closedCount;
	}

	StatusDialog::AppendLogf(L"\u6e38\u620f\u5df2\u5f3a\u5236\u5173\u95ed (%d \u4e2a\u8fdb\u7a0b)\u3002",
	                       closedCount);
	InputRestore::ScheduleRestoreBurst(StatusDialog::GetMainWindow());
	return true;
}
