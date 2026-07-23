#include "stdafx.h"
#include "launcher_i18n.h"

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
		    LauncherI18n::T(LauncherI18n::Str::WarnNoManagerDll));
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
		    LauncherI18n::T(LauncherI18n::Str::DeployedManagerFmt),
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
		    LauncherI18n::T(LauncherI18n::Str::WarnManagerBusy));
		return true;
	}

	StatusDialog::AppendLogf(
	    LauncherI18n::T(LauncherI18n::Str::WarnDeployManagerFmt),
	    error);
	return false;
}

static bool DeployGraphicsProxy(const std::wstring &gameDirectory) {
	const auto &config = LauncherConfig::Get();

	std::wstring proxySource;
	if (!Paths::ResolveGraphicsProxyDll(proxySource)) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::WarnNoProxyDll));
		return false;
	}

	const auto destinationPath =
	    gameDirectory + L"\\" + config.graphicsProxyDllName;
	const auto backupPath = gameDirectory + L"\\" + config.graphicsProxyBackup;

	if (!BackupAndCopyFile(proxySource, destinationPath, backupPath)) {
		StatusDialog::AppendLogf(
		    LauncherI18n::T(LauncherI18n::Str::WarnDeployProxyFmt),
		    GetLastError());
		return false;
	}

	StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::DeployedProxyFmt),
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
			    LauncherI18n::T(LauncherI18n::Str::WarnBypassNotApplied));
		}
		resumeGuard.Resume();
		const auto resumed = ResumeAllProcessThreads(processInfo.dwProcessId);
		if (resumed > 0) {
			StatusDialog::AppendLogf(
			    LauncherI18n::T(LauncherI18n::Str::ResumedThreadsFmt),
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
	    LauncherI18n::T(LauncherI18n::Str::DirectLaunchFailFmt),
	    error, FormatWin32Error(error).c_str());

	if (const auto explorerToken = DuplicateExplorerPrimaryToken()) {
		const auto launched = LaunchWithCreateProcess(
		    gameExe, gameDirectory, extraArgs, explorerToken, &error);
		CloseHandle(explorerToken);
		if (launched) {
			StatusDialog::AppendLog(
			    LauncherI18n::T(LauncherI18n::Str::LaunchedAsUser));
			return true;
		}
	}

	if (LaunchWithShellExecute(gameExe, gameDirectory, extraArgs, &error)) {
		StatusDialog::AppendLog(LauncherI18n::T(LauncherI18n::Str::LaunchedViaShell));
		return true;
	}

	StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::LaunchFailFmt),
	                       error, FormatWin32Error(error).c_str());
	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::TryManualLaunch));
	return false;
}

static void WarnIfSecuRomPresent(const std::wstring &gameExe) {
	if (!GamePath::HasSecuRomProtection(gameExe)) {
		return;
	}

	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::WarnSecuRom));
	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::SecuRomSteam));
	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::SecuRomEa));
	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::SecuRomDocs));
}

bool PrepareGameEnvironment() {
	const auto &config = LauncherConfig::Get();

	if (IsProcessRunning(config.gameProcessName.c_str())) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::GameAlreadyRunningPrep));
		return true;
	}

	std::wstring gameDirectory;
	if (!Paths::GetGameBinariesDirectory(gameDirectory)) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::NoBinariesSkipPrep));
		return false;
	}

	StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::PreparingGameFiles));
	DeployManagerDependency(gameDirectory);

	const auto displaySettings = LauncherSettings::LoadDisplaySettings();
	std::wstring applyLog;
	if (GameConfig::ApplyDisplaySettings(displaySettings, &applyLog)) {
		StatusDialog::AppendLog(applyLog.c_str());
	} else if (!applyLog.empty()) {
		StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::WarnFmt), applyLog.c_str());
	}

	return DeployGraphicsProxy(gameDirectory);
}

bool LaunchGameExecutable() {
	const auto &config = LauncherConfig::Get();

	if (IsProcessRunning(config.gameProcessName.c_str())) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::GameAlreadyRunning));
		return true;
	}

	std::wstring gameDirectory;
	if (!Paths::GetGameBinariesDirectory(gameDirectory)) {
		std::wstring launcherDirectory;
		Paths::GetLauncherDirectory(launcherDirectory);
		StatusDialog::AppendLogf(
		    LauncherI18n::T(LauncherI18n::Str::GameNotFoundBrowseFmt),
		    config.gameExecutable.c_str());
		StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::LauncherLocationFmt),
		                       launcherDirectory.c_str());
		return false;
	}

	const auto gameExe = gameDirectory + L"\\" + config.gameExecutable;
	if (!PathFileExistsW(gameExe.c_str())) {
		StatusDialog::AppendLogf(
		    LauncherI18n::T(LauncherI18n::Str::GameExeMissingFmt),
		    gameDirectory.c_str(), config.gameExecutable.c_str());
		return false;
	}

	WarnIfSecuRomPresent(gameExe);

	StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::GameDirectoryFmt), gameDirectory.c_str());
	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::StartingGame));

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
	    LauncherI18n::T(LauncherI18n::Str::GameStartedWaitMenu));
	return true;
}

bool CloseGameExecutable() {
	const auto &config = LauncherConfig::Get();

	if (!IsProcessRunning(config.gameProcessName.c_str())) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::GameNotRunningClose));
		return true;
	}

	StatusDialog::AppendLog(LauncherI18n::T(LauncherI18n::Str::ForceClosingGame));

	int closedCount = 0;
	while (true) {
		const auto entry = FindProcessByName(config.gameProcessName.c_str());
		if (!entry.th32ProcessID) {
			break;
		}

		const auto process =
		    OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
		if (!process) {
			StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::OpenProcessFailFmt),
			                       GetLastError());
			return false;
		}

		const auto terminated = TerminateProcess(process, 0) != FALSE;
		CloseHandle(process);
		if (!terminated) {
			StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::TerminateFailFmt),
			                       GetLastError());
			return false;
		}

		++closedCount;
	}

	StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::ForceClosedFmt),
	                       closedCount);
	InputRestore::ScheduleRestoreBurst(StatusDialog::GetMainWindow());
	return true;
}
