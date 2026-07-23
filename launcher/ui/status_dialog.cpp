#include "stdafx.h"

#include "game_config.h"
#include "game_path.h"
#include "input_restore.h"
#include "launcher_settings.h"
#include "product_version.h"
#include "update_check.h"
#include "window_layout_settings.h"

#include "ui/status_dialog.h"

#include <stdarg.h>
#include <atomic>

namespace {

enum ControlId : int {
	kStatus = 1001,
	kLog = 1002,
	kClose = 1003,
	kLaunchGame = 1004,
	kCloseGame = 1005,
	kGamePathLabel = 1006,
	kGamePath = 1007,
	kBrowseGamePath = 1008,
	kDisplayModeLabel = 1009,
	kDisplayMode = 1010,
	kResolutionLabel = 1011,
	kResolutionPreset = 1012,
	kResX = 1013,
	kResY = 1014,
	kScaleLabel = 1015,
	kScale = 1016,
	kScaleValue = 1017,
	kSkipStartup = 1018,
	kSkipConfigIntegrity = 1019,
	kUpdateStatus = 1020,
	kCheckUpdate = 1021,
	kUpgradeNow = 1022,
};

enum : UINT {
	kSetStep = WM_APP + 1,
	kAppendLog = WM_APP + 2,
	kMarkFinished = WM_APP + 3,
	kAppendModLog = WM_APP + 4,
	kUpdateCheckDone = WM_APP + 6,
};

HWND g_window = nullptr;
HWND g_status = nullptr;
HWND g_log = nullptr;
HWND g_close = nullptr;
HWND g_launch = nullptr;
HWND g_closeGame = nullptr;
HWND g_gamePathLabel = nullptr;
HWND g_gamePath = nullptr;
HWND g_browseGamePath = nullptr;
HWND g_displayModeLabel = nullptr;
HWND g_displayMode = nullptr;
HWND g_resolutionLabel = nullptr;
HWND g_resolutionPreset = nullptr;
HWND g_resX = nullptr;
HWND g_resY = nullptr;
HWND g_scaleLabel = nullptr;
HWND g_scale = nullptr;
HWND g_scaleValue = nullptr;
HWND g_skipStartup = nullptr;
HWND g_skipConfigIntegrity = nullptr;
HWND g_updateStatus = nullptr;
HWND g_checkUpdate = nullptr;
HWND g_upgradeNow = nullptr;
HFONT g_font = nullptr;
HFONT g_statusFont = nullptr;
bool g_finished = false;
bool g_success = false;
std::atomic<bool> g_exitRequested{false};
std::atomic<bool> g_updateBusy{false};
StatusDialog::LaunchGameCallback g_launchHandler = nullptr;
StatusDialog::CloseGameCallback g_closeGameHandler = nullptr;
std::mutex g_pendingMutex;
std::vector<std::wstring> g_pendingLogs;
UpdateCheck::ReleaseInfo g_latestRelease;
bool g_hasLatestRelease = false;

void FreeUiString(LPARAM value) {
	if (value) {
		free(reinterpret_cast<void *>(value));
	}
}

void AppendLogLine(const wchar_t *text) {
	if (!g_log || !text) {
		return;
	}

	const auto length = GetWindowTextLengthW(g_log);
	SendMessageW(g_log, EM_SETSEL, length, length);
	SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text));
	SendMessageW(g_log, EM_REPLACESEL, FALSE,
	             reinterpret_cast<LPARAM>(L"\r\n"));
	SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

void FlushPendingLogs() {
	std::vector<std::wstring> pending;
	{
		std::lock_guard<std::mutex> lock(g_pendingMutex);
		pending.swap(g_pendingLogs);
	}

	for (const auto &line : pending) {
		AppendLogLine(line.c_str());
	}
}

void UpdateCloseButton() {
	if (!g_close) {
		return;
	}

	EnableWindow(g_close, TRUE);
	SetWindowTextW(g_close, L"\u5173\u95ed");
}

void CloseLauncherWindow(HWND hwnd) {
	g_exitRequested = true;
	DestroyWindow(hwnd);
}

void UpdateGamePathField() {
	if (!g_gamePath) {
		return;
	}

	std::wstring gameRoot;
	std::wstring sourceLabel;
	if (GamePath::ResolveGameRoot(gameRoot, &sourceLabel)) {
		SetWindowTextW(g_gamePath, gameRoot.c_str());
		return;
	}

	SetWindowTextW(g_gamePath,
	               L"(\u672a\u627e\u5230\u6e38\u620f\u8def\u5f84\uff0c\u8bf7\u70b9\u51fb\u300c\u6d4f\u89c8\u300d)");
}

struct UpdateCheckResultPayload {
	UpdateCheck::ReleaseInfo info;
	bool manual = false;
};

void SetUpdateUiIdle() {
	if (g_checkUpdate) {
		EnableWindow(g_checkUpdate, TRUE);
	}
	if (g_upgradeNow) {
		const bool canUpgrade =
		    g_hasLatestRelease &&
		    g_latestRelease.status == UpdateCheck::CheckStatus::UpdateAvailable;
		EnableWindow(g_upgradeNow, canUpgrade ? TRUE : FALSE);
	}
}

void ApplyUpdateCheckResult(const UpdateCheckResultPayload &payload) {
	g_latestRelease = payload.info;
	g_hasLatestRelease = true;
	g_updateBusy = false;
	SetUpdateUiIdle();

	wchar_t line[256] = {};
	switch (payload.info.status) {
	case UpdateCheck::CheckStatus::UpdateAvailable: {
		std::string dismissed;
		LauncherSettings::LoadDismissedUpdateVersion(dismissed);
		const bool soft = !payload.manual && !dismissed.empty() &&
		                  dismissed == payload.info.version;
		_snwprintf(line, 255,
		           L"\u53d1\u73b0\u65b0\u7248\u672c %S\uff08\u5f53\u524d %S\uff09",
		           payload.info.version.c_str(),
		           UpdateCheck::LocalVersionString().c_str());
		line[255] = L'\0';
		if (g_updateStatus) {
			SetWindowTextW(g_updateStatus, line);
		}
		AppendLogLine(line);
		if (soft) {
			AppendLogLine(
			    L"\u8be5\u7248\u672c\u66fe\u88ab\u5ffd\u7565\uff1b\u53ef\u70b9\u300c\u7acb\u5373\u5347\u7ea7\u300d\u3002");
		} else if (!payload.manual) {
			MessageBoxW(g_window, line, L"Mirror's Edge Module Launcher",
			            MB_ICONINFORMATION | MB_OK);
			LauncherSettings::SaveDismissedUpdateVersion(payload.info.version);
		}
		break;
	}
	case UpdateCheck::CheckStatus::UpToDate:
		_snwprintf(line, 255, L"\u5df2\u662f\u6700\u65b0\u7248\u672c %S",
		           UpdateCheck::LocalVersionString().c_str());
		line[255] = L'\0';
		if (g_updateStatus) {
			SetWindowTextW(g_updateStatus, line);
		}
		if (payload.manual) {
			AppendLogLine(line);
		}
		break;
	case UpdateCheck::CheckStatus::NoRelease:
		if (g_updateStatus) {
			SetWindowTextW(
			    g_updateStatus,
			    L"\u6682\u65e0 GitHub Release\uff08\u65e0\u6cd5\u68c0\u6d4b\u66f4\u65b0\uff09");
		}
		if (payload.manual) {
			AppendLogLine(
			    L"\u68c0\u67e5\u66f4\u65b0\uff1a\u4ed3\u5e93\u5c1a\u65e0 Release\u3002");
		}
		break;
	case UpdateCheck::CheckStatus::Skipped:
		if (g_updateStatus) {
			SetWindowTextW(g_updateStatus,
			               L"\u5df2\u8df3\u8fc7\u81ea\u52a8\u68c0\u67e5\u66f4\u65b0");
		}
		break;
	default:
		if (g_updateStatus) {
			SetWindowTextW(
			    g_updateStatus,
			    L"\u68c0\u67e5\u66f4\u65b0\u5931\u8d25\uff08\u79bb\u7ebf\u6216\u7f51\u7edc\u9519\u8bef\uff09");
		}
		if (payload.manual || !payload.info.errorMessage.empty()) {
			AppendLogLine(
			    (L"\u68c0\u67e5\u66f4\u65b0\u5931\u8d25: " + payload.info.errorMessage)
			        .c_str());
		}
		break;
	}
}

struct UpdateWorkerArgs {
	bool manual = false;
};

DWORD WINAPI UpdateCheckWorker(LPVOID param) {
	auto *args = reinterpret_cast<UpdateWorkerArgs *>(param);
	const bool manual = args ? args->manual : false;
	delete args;

	auto *payload = new UpdateCheckResultPayload();
	payload->manual = manual;
	if (!manual && LauncherSettings::LoadSkipUpdateCheck()) {
		payload->info.status = UpdateCheck::CheckStatus::Skipped;
	} else {
		payload->info = UpdateCheck::FetchLatestRelease();
	}

	if (g_window) {
		if (!PostMessageW(g_window, kUpdateCheckDone, 0,
		                  reinterpret_cast<LPARAM>(payload))) {
			delete payload;
			g_updateBusy = false;
		}
	} else {
		delete payload;
		g_updateBusy = false;
	}
	return 0;
}

void BeginUpdateCheck(bool manual) {
	if (g_updateBusy.exchange(true)) {
		AppendLogLine(L"\u66f4\u65b0\u68c0\u67e5\u5df2\u5728\u8fdb\u884c\u4e2d\u2026");
		return;
	}
	if (g_updateStatus) {
		SetWindowTextW(g_updateStatus, L"\u6b63\u5728\u68c0\u67e5\u66f4\u65b0\u2026");
	}
	if (g_checkUpdate) {
		EnableWindow(g_checkUpdate, FALSE);
	}
	if (g_upgradeNow) {
		EnableWindow(g_upgradeNow, FALSE);
	}

	auto *args = new UpdateWorkerArgs{manual};
	HANDLE thread =
	    CreateThread(nullptr, 0, UpdateCheckWorker, args, 0, nullptr);
	if (!thread) {
		delete args;
		g_updateBusy = false;
		SetUpdateUiIdle();
		AppendLogLine(L"\u65e0\u6cd5\u542f\u52a8\u66f4\u65b0\u68c0\u67e5\u7ebf\u7a0b\u3002");
		return;
	}
	CloseHandle(thread);
}

DWORD WINAPI UpgradeWorker(LPVOID) {
	if (!g_hasLatestRelease ||
	    g_latestRelease.status != UpdateCheck::CheckStatus::UpdateAvailable ||
	    g_latestRelease.downloadUrl.empty()) {
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 1;
	}

	std::wstring blocking;
	if (UpdateCheck::BlockingProcessesRunning(&blocking)) {
		wchar_t msg[256] = {};
		_snwprintf(msg, 255,
		           L"\u8bf7\u5148\u5173\u95ed %s \u540e\u518d\u5347\u7ea7\u3002",
		           blocking.c_str());
		msg[255] = L'\0';
		MessageBoxW(g_window, msg, L"Mirror's Edge Module Launcher",
		            MB_ICONWARNING | MB_OK);
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 1;
	}

	wchar_t temp[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, temp);
	const std::wstring zipFile =
	    std::wstring(temp) + L"mirroredge-launcher-update.zip";

	StatusDialog::AppendLog(
	    L"\u6b63\u5728\u4e0b\u8f7d\u6700\u65b0 Release\u2026");
	std::wstring err;
	if (!UpdateCheck::DownloadFile(g_latestRelease.downloadUrl, zipFile,
	                               &err)) {
		StatusDialog::AppendLog((L"\u4e0b\u8f7d\u5931\u8d25: " + err).c_str());
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 1;
	}

	StatusDialog::AppendLog(
	    L"\u6b63\u5728\u89e3\u538b\u5e76\u51c6\u5907\u66ff\u6362\u6587\u4ef6\u2026");
	std::wstring batPath;
	if (!UpdateCheck::PrepareApplyUpdate(zipFile, GetCurrentProcessId(), batPath,
	                                     &err)) {
		StatusDialog::AppendLog(
		    (L"\u51c6\u5907\u5347\u7ea7\u5931\u8d25: " + err).c_str());
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 1;
	}

	const int confirm = MessageBoxW(
	    g_window,
	    L"\u4e0b\u8f7d\u5b8c\u6210\u3002\u542f\u52a8\u5668\u5c06\u9000\u51fa\u5e76\u81ea\u52a8\u5b89\u88c5\u65b0\u7248\u672c\uff0c\u7136\u540e\u91cd\u542f\u3002\u7ee7\u7eed\uff1f",
	    L"Mirror's Edge Module Launcher", MB_ICONQUESTION | MB_YESNO);
	if (confirm != IDYES) {
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 0;
	}

	if (!UpdateCheck::LaunchApplyBat(batPath, &err)) {
		StatusDialog::AppendLog(
		    (L"\u542f\u52a8\u5347\u7ea7\u811a\u672c\u5931\u8d25: " + err).c_str());
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 1;
	}

	StatusDialog::AppendLog(
	    L"\u6b63\u5728\u5e94\u7528\u5347\u7ea7\u5e76\u91cd\u542f\u2026");
	StatusDialog::RequestExit();
	if (g_window) {
		PostMessageW(g_window, WM_CLOSE, 0, 0);
	}
	return 0;
}

void BeginUpgrade() {
	if (g_updateBusy.exchange(true)) {
		return;
	}
	if (g_checkUpdate) {
		EnableWindow(g_checkUpdate, FALSE);
	}
	if (g_upgradeNow) {
		EnableWindow(g_upgradeNow, FALSE);
	}
	HANDLE thread = CreateThread(nullptr, 0, UpgradeWorker, nullptr, 0, nullptr);
	if (!thread) {
		g_updateBusy = false;
		SetUpdateUiIdle();
		AppendLogLine(L"\u65e0\u6cd5\u542f\u52a8\u5347\u7ea7\u7ebf\u7a0b\u3002");
		return;
	}
	CloseHandle(thread);
}

struct ResolutionPreset {
	const wchar_t *label;
	int width;
	int height;
};

constexpr int kMatchWindowPresetWidth = -1;

constexpr ResolutionPreset kWindowedResolutionPresets[] = {
    {L"1920 x 1080", 1920, 1080},
    {L"1600 x 1200", 1600, 1200},
    {L"1280 x 720", 1280, 720},
    {L"1280 x 1024", 1280, 1024},
    {L"\u81ea\u5b9a\u4e49 (Custom)", 0, 0},
};

constexpr ResolutionPreset kBorderlessResolutionPresets[] = {
    {L"\u5339\u914d\u7a97\u53e3 (Match window)", kMatchWindowPresetWidth,
     kMatchWindowPresetWidth},
    {L"1920 x 1080", 1920, 1080},
    {L"1600 x 1200", 1600, 1200},
    {L"1280 x 720", 1280, 720},
    {L"1280 x 1024", 1280, 1024},
    {L"\u81ea\u5b9a\u4e49 (Custom)", 0, 0},
};

bool IsBorderlessModeSelected() {
	if (!g_displayMode) {
		return false;
	}

	const auto mode =
	    static_cast<int>(SendMessageW(g_displayMode, CB_GETCURSEL, 0, 0));
	return mode == static_cast<int>(DisplayMode::Borderless);
}

const ResolutionPreset *GetActiveResolutionPresets(size_t &count) {
	if (IsBorderlessModeSelected()) {
		count = _countof(kBorderlessResolutionPresets);
		return kBorderlessResolutionPresets;
	}

	count = _countof(kWindowedResolutionPresets);
	return kWindowedResolutionPresets;
}

void ComputeMatchWindowResolution(int &resX, int &resY) {
	float scale = 0.5f;
	if (g_scale) {
		const auto scalePercent =
		    static_cast<int>(SendMessageW(g_scale, TBM_GETPOS, 0, 0));
		scale = static_cast<float>(scalePercent) / 100.0f;
	}

	WindowLayout_ComputeMatchWindowResolution(scale, resX, resY);
}

void PopulateResolutionPresetCombo() {
	if (!g_resolutionPreset) {
		return;
	}

	SendMessageW(g_resolutionPreset, CB_RESETCONTENT, 0, 0);
	size_t count = 0;
	const auto *presets = GetActiveResolutionPresets(count);
	for (size_t i = 0; i < count; ++i) {
		SendMessageW(g_resolutionPreset, CB_ADDSTRING, 0,
		             reinterpret_cast<LPARAM>(presets[i].label));
	}
}

bool IsMatchWindowPresetSelected() {
	if (!g_resolutionPreset || !IsBorderlessModeSelected()) {
		return false;
	}

	const auto index =
	    static_cast<int>(SendMessageW(g_resolutionPreset, CB_GETCURSEL, 0, 0));
	return index == 0;
}

void UpdateScaleValueLabel() {
	if (!g_scale || !g_scaleValue) {
		return;
	}

	const auto pos = static_cast<int>(SendMessageW(g_scale, TBM_GETPOS, 0, 0));
	wchar_t buffer[32] = {};
	swprintf(buffer, 32, L"%d%%", pos);
	SetWindowTextW(g_scaleValue, buffer);
}

void UpdateResolutionFieldsEnabled() {
	if (!g_resolutionPreset || !g_resX || !g_resY) {
		return;
	}

	size_t count = 0;
	GetActiveResolutionPresets(count);
	const auto index =
	    static_cast<int>(SendMessageW(g_resolutionPreset, CB_GETCURSEL, 0, 0));
	const bool matchWindow = IsMatchWindowPresetSelected();
	const bool custom = index == static_cast<int>(count) - 1;
	EnableWindow(g_resX, custom ? TRUE : FALSE);
	EnableWindow(g_resY, custom ? TRUE : FALSE);

	if (matchWindow) {
		int resX = 0;
		int resY = 0;
		ComputeMatchWindowResolution(resX, resY);
		wchar_t buffer[16] = {};
		swprintf(buffer, 16, L"%d", resX);
		SetWindowTextW(g_resX, buffer);
		swprintf(buffer, 16, L"%d", resY);
		SetWindowTextW(g_resY, buffer);
	}
}

int FindResolutionPresetIndex(bool borderless, bool renderMatchWindow, int width,
                              int height) {
	size_t count = 0;
	const auto *presets =
	    borderless ? kBorderlessResolutionPresets : kWindowedResolutionPresets;
	count = borderless ? _countof(kBorderlessResolutionPresets)
	                     : _countof(kWindowedResolutionPresets);

	if (borderless && renderMatchWindow) {
		return 0;
	}

	const int customIndex = static_cast<int>(count) - 1;
	for (int i = borderless ? 1 : 0; i < customIndex; ++i) {
		if (presets[i].width == width && presets[i].height == height) {
			return i;
		}
	}
	return customIndex;
}

void ApplyResolutionPresetToFields(int presetIndex) {
	if (!g_resX || !g_resY) {
		return;
	}

	size_t count = 0;
	const auto *presets = GetActiveResolutionPresets(count);
	if (presetIndex < 0 || presetIndex >= static_cast<int>(count)) {
		return;
	}

	const auto &preset = presets[presetIndex];
	if (preset.width == kMatchWindowPresetWidth) {
		UpdateResolutionFieldsEnabled();
		return;
	}

	if (preset.width <= 0 || preset.height <= 0) {
		UpdateResolutionFieldsEnabled();
		return;
	}

	wchar_t buffer[16] = {};
	swprintf(buffer, 16, L"%d", preset.width);
	SetWindowTextW(g_resX, buffer);
	swprintf(buffer, 16, L"%d", preset.height);
	SetWindowTextW(g_resY, buffer);
	UpdateResolutionFieldsEnabled();
}

void UpdateBorderlessControlsEnabled() {
	if (!g_displayMode) {
		return;
	}

	const bool wasMatchWindow = IsMatchWindowPresetSelected();
	int resX = 1920;
	int resY = 1080;
	if (g_resX && g_resY) {
		wchar_t buffer[16] = {};
		GetWindowTextW(g_resX, buffer, _countof(buffer));
		resX = _wtoi(buffer);
		GetWindowTextW(g_resY, buffer, _countof(buffer));
		resY = _wtoi(buffer);
	}

	PopulateResolutionPresetCombo();

	const auto mode =
	    static_cast<int>(SendMessageW(g_displayMode, CB_GETCURSEL, 0, 0));
	const bool borderless = mode == static_cast<int>(DisplayMode::Borderless);
	EnableWindow(g_scaleLabel, borderless ? TRUE : FALSE);
	EnableWindow(g_scale, borderless ? TRUE : FALSE);
	EnableWindow(g_scaleValue, borderless ? TRUE : FALSE);

	if (g_resolutionLabel) {
		EnableWindow(g_resolutionLabel, TRUE);
	}
	if (g_resolutionPreset) {
		EnableWindow(g_resolutionPreset, TRUE);
		const bool renderMatchWindow = borderless && wasMatchWindow;
		const int presetIndex = FindResolutionPresetIndex(
		    borderless, renderMatchWindow, resX, resY);
		SendMessageW(g_resolutionPreset, CB_SETCURSEL,
		             static_cast<WPARAM>(presetIndex), 0);
		ApplyResolutionPresetToFields(presetIndex);
	}
}

void LoadDisplaySettingsToUi() {
	const auto settings = LauncherSettings::LoadDisplaySettings();

	if (g_displayMode) {
		SendMessageW(g_displayMode, CB_SETCURSEL,
		             static_cast<WPARAM>(settings.mode), 0);
	}

	if (g_resolutionPreset) {
		const bool borderless = settings.mode == DisplayMode::Borderless;
		PopulateResolutionPresetCombo();
		const auto presetIndex = FindResolutionPresetIndex(
		    borderless, settings.renderMatchWindow, settings.resX,
		    settings.resY);
		SendMessageW(g_resolutionPreset, CB_SETCURSEL,
		             static_cast<WPARAM>(presetIndex), 0);
		ApplyResolutionPresetToFields(presetIndex);
		if (!borderless || !settings.renderMatchWindow) {
			size_t count = 0;
			GetActiveResolutionPresets(count);
			if (presetIndex == static_cast<int>(count) - 1) {
				wchar_t buffer[16] = {};
				swprintf(buffer, 16, L"%d", settings.resX);
				SetWindowTextW(g_resX, buffer);
				swprintf(buffer, 16, L"%d", settings.resY);
				SetWindowTextW(g_resY, buffer);
			}
		}
	}

	if (g_scale) {
		const auto scalePercent =
		    static_cast<int>(settings.scale * 100.0f + 0.5f);
		SendMessageW(g_scale, TBM_SETPOS, TRUE,
		             static_cast<LPARAM>(scalePercent));
		UpdateScaleValueLabel();
	}

	if (g_skipStartup) {
		SendMessageW(g_skipStartup, BM_SETCHECK,
		             settings.skipStartupMovies ? BST_CHECKED : BST_UNCHECKED,
		             0);
	}

	if (g_skipConfigIntegrity) {
		const bool skipCheck = LauncherSettings::LoadSkipConfigIntegrityCheck();
		SendMessageW(g_skipConfigIntegrity, BM_SETCHECK,
		             skipCheck ? BST_CHECKED : BST_UNCHECKED, 0);
	}

	UpdateBorderlessControlsEnabled();
	UpdateResolutionFieldsEnabled();
}

void SaveDisplaySettingsFromUi() {
	DisplaySettings settings = LauncherSettings::LoadDisplaySettings();

	if (g_displayMode) {
		const auto mode =
		    static_cast<int>(SendMessageW(g_displayMode, CB_GETCURSEL, 0, 0));
		if (mode >= 0 && mode <= static_cast<int>(DisplayMode::Borderless)) {
			settings.mode = static_cast<DisplayMode>(mode);
		}
	}

	if (g_resolutionPreset) {
		const bool borderless = settings.mode == DisplayMode::Borderless;
		const auto presetIndex =
		    static_cast<int>(SendMessageW(g_resolutionPreset, CB_GETCURSEL, 0, 0));
		size_t count = 0;
		const auto *presets = GetActiveResolutionPresets(count);

		if (borderless) {
			settings.renderMatchWindow = presetIndex == 0;
		} else {
			settings.renderMatchWindow = false;
		}

		if (!settings.renderMatchWindow && presetIndex >= 0 &&
		    presetIndex < static_cast<int>(count) - 1) {
			settings.resX = presets[presetIndex].width;
			settings.resY = presets[presetIndex].height;
		} else if (!settings.renderMatchWindow && g_resX && g_resY) {
			wchar_t buffer[16] = {};
			GetWindowTextW(g_resX, buffer, _countof(buffer));
			settings.resX = _wtoi(buffer);
			GetWindowTextW(g_resY, buffer, _countof(buffer));
			settings.resY = _wtoi(buffer);
		} else if (settings.renderMatchWindow) {
			ComputeMatchWindowResolution(settings.resX, settings.resY);
		}
	}

	if (g_scale) {
		const auto scalePercent =
		    static_cast<int>(SendMessageW(g_scale, TBM_GETPOS, 0, 0));
		settings.scale = static_cast<float>(scalePercent) / 100.0f;
	}

	if (g_skipStartup) {
		settings.skipStartupMovies =
		    SendMessageW(g_skipStartup, BM_GETCHECK, 0, 0) == BST_CHECKED;
	}

	LauncherSettings::SaveDisplaySettings(settings);

	if (g_skipConfigIntegrity) {
		const bool skipCheck =
		    SendMessageW(g_skipConfigIntegrity, BM_GETCHECK, 0, 0) == BST_CHECKED;
		LauncherSettings::SaveSkipConfigIntegrityCheck(skipCheck);
	}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                            LPARAM lparam) {
	switch (message) {
	case kSetStep: {
		if (g_status && lparam) {
			SetWindowTextW(g_status, reinterpret_cast<const wchar_t *>(lparam));
		}
		FreeUiString(lparam);
		return 0;
	}
	case kAppendLog: {
		if (lparam) {
			AppendLogLine(reinterpret_cast<const wchar_t *>(lparam));
		}
		FreeUiString(lparam);
		return 0;
	}
	case kMarkFinished: {
		g_finished = true;
		g_success = wparam != 0;
		UpdateCloseButton();
		return 0;
	}
	case kAppendModLog: {
		if (lparam) {
			AppendLogLine(reinterpret_cast<const wchar_t *>(lparam));
		}
		FreeUiString(lparam);
		return 0;
	}
	case kUpdateCheckDone: {
		if (wparam == 1) {
			g_updateBusy = false;
			SetUpdateUiIdle();
			return 0;
		}
		auto *payload = reinterpret_cast<UpdateCheckResultPayload *>(lparam);
		if (payload) {
			ApplyUpdateCheckResult(*payload);
			delete payload;
		}
		return 0;
	}
	case InputRestore::kRestoreInputMessage: {
		InputRestore::ScheduleRestoreBurst(hwnd);
		return 0;
	}
	case WM_TIMER: {
		if (wparam == InputRestore::kRestoreInputTimerId) {
			if (!InputRestore::HandleRestoreTimer(hwnd)) {
				KillTimer(hwnd, InputRestore::kRestoreInputTimerId);
			}
			return 0;
		}
		break;
	}
	case WM_HSCROLL: {
		if (reinterpret_cast<HWND>(lparam) == g_scale) {
			UpdateScaleValueLabel();
			if (IsMatchWindowPresetSelected()) {
				UpdateResolutionFieldsEnabled();
			}
			return 0;
		}
		break;
	}
	case WM_COMMAND: {
		if (LOWORD(wparam) == kLaunchGame && g_launchHandler) {
			SaveDisplaySettingsFromUi();
			const auto displaySettings = LauncherSettings::LoadDisplaySettings();
			std::wstring applyLog;
			if (GameConfig::ApplyDisplaySettings(displaySettings, &applyLog)) {
				if (!applyLog.empty()) {
					AppendLogLine(applyLog.c_str());
				}
			} else if (!applyLog.empty()) {
				AppendLogLine((L"\u8b66\u544a\uff1a" + applyLog).c_str());
			}
			g_launchHandler();
			return 0;
		}
		if (LOWORD(wparam) == kCloseGame && g_closeGameHandler) {
			g_closeGameHandler();
			return 0;
		}
		if (LOWORD(wparam) == kBrowseGamePath) {
			std::wstring gameRoot;
			if (GamePath::BrowseForGameRoot(hwnd, gameRoot)) {
				UpdateGamePathField();
				AppendLogLine((L"\u6e38\u620f\u8def\u5f84\u5df2\u4fdd\u5b58: " + gameRoot).c_str());
			}
			return 0;
		}
		if (LOWORD(wparam) == kCheckUpdate) {
			BeginUpdateCheck(true);
			return 0;
		}
		if (LOWORD(wparam) == kUpgradeNow) {
			BeginUpgrade();
			return 0;
		}
		if (LOWORD(wparam) == kDisplayMode && HIWORD(wparam) == CBN_SELCHANGE) {
			UpdateBorderlessControlsEnabled();
			return 0;
		}
		if (LOWORD(wparam) == kResolutionPreset && HIWORD(wparam) == CBN_SELCHANGE) {
			const auto presetIndex = static_cast<int>(
			    SendMessageW(g_resolutionPreset, CB_GETCURSEL, 0, 0));
			ApplyResolutionPresetToFields(presetIndex);
			return 0;
		}
		if (LOWORD(wparam) == kClose) {
			CloseLauncherWindow(hwnd);
			return 0;
		}
		return 0;
	}
	case WM_CLOSE: {
		CloseLauncherWindow(hwnd);
		return 0;
	}
	case WM_DESTROY: {
		PostQuitMessage(0);
		return 0;
	}
	default:
		break;
	}

	return DefWindowProcW(hwnd, message, wparam, lparam);
}

void PostOwnedString(UINT message, WPARAM wparam, const wchar_t *text) {
	if (!g_window || !text) {
		return;
	}

	const auto size = (wcslen(text) + 1) * sizeof(wchar_t);
	auto *copy = static_cast<wchar_t *>(malloc(size));
	if (!copy) {
		return;
	}

	memcpy(copy, text, size);
	if (!PostMessageW(g_window, message, wparam, reinterpret_cast<LPARAM>(copy))) {
		free(copy);
	}
}

} // namespace

namespace StatusDialog {

void SetLaunchGameHandler(LaunchGameCallback handler) { g_launchHandler = handler; }

void SetCloseGameHandler(CloseGameCallback handler) {
	g_closeGameHandler = handler;
}

void Create() {
	if (g_window) {
		return;
	}

	INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&controls);

	const auto instance = GetModuleHandleW(nullptr);
	const wchar_t *className = L"mirroredge_module_launcher_dialog";

	WNDCLASSEXW windowClass = {sizeof(windowClass)};
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = instance;
	windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	windowClass.lpszClassName = className;
	RegisterClassExW(&windowClass);

	wchar_t windowTitle[128] = {};
	_snwprintf(windowTitle, 127, L"Mirror's Edge Module Launcher v%hs",
	           MMOD_PRODUCT_VERSION_STRING);
	windowTitle[127] = L'\0';

	g_window = CreateWindowExW(
	    WS_EX_APPWINDOW, className,
	    windowTitle,
	    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
	    CW_USEDEFAULT, 640, 620, nullptr, nullptr, instance, nullptr);

	if (!g_window) {
		MessageBoxW(nullptr,
		            L"\u65e0\u6cd5\u521b\u5efa\u542f\u52a8\u5668\u7a97\u53e3\u3002\u8bf7\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u8fd0\u884c "
		            L"ModuleLauncher.bat\u3002",
		            L"Mirror's Edge Module Launcher", MB_ICONERROR | MB_OK);
		return;
	}

	g_font = CreateFontW(
	    -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
	    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
	    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	g_statusFont = CreateFontW(
	    -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
	    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
	    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

	g_status = CreateWindowExW(
	    0, L"STATIC", L"\u51c6\u5907\u4e2d...",
	    WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 12, 600, 28, g_window,
	    reinterpret_cast<HMENU>(kStatus), instance, nullptr);

	g_gamePathLabel = CreateWindowExW(
	    0, L"STATIC", L"\u6e38\u620f\u8def\u5f84:",
	    WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 42, 80, 20, g_window,
	    reinterpret_cast<HMENU>(kGamePathLabel), instance, nullptr);

	g_gamePath = CreateWindowExW(
	    WS_EX_CLIENTEDGE, L"EDIT", L"",
	    WS_CHILD | WS_VISIBLE | ES_LEFT | ES_READONLY | ES_AUTOHSCROLL, 96, 40,
	    430, 22, g_window, reinterpret_cast<HMENU>(kGamePath), instance,
	    nullptr);

	g_browseGamePath = CreateWindowExW(
	    0, L"BUTTON", L"\u6d4f\u89c8...",
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 532, 38, 80, 24, g_window,
	    reinterpret_cast<HMENU>(kBrowseGamePath), instance, nullptr);

	g_displayModeLabel = CreateWindowExW(
	    0, L"STATIC", L"\u663e\u793a\u6a21\u5f0f:",
	    WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 68, 72, 20, g_window,
	    reinterpret_cast<HMENU>(kDisplayModeLabel), instance, nullptr);

	g_displayMode = CreateWindowExW(
	    0, L"COMBOBOX", L"",
	    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 88, 66, 160,
	    200, g_window, reinterpret_cast<HMENU>(kDisplayMode), instance, nullptr);

	g_resolutionLabel = CreateWindowExW(
	    0, L"STATIC", L"\u6e32\u67d3\u5206\u8fa8\u7387:",
	    WS_CHILD | WS_VISIBLE | SS_LEFT, 260, 68, 72, 20, g_window,
	    reinterpret_cast<HMENU>(kResolutionLabel), instance, nullptr);

	g_resolutionPreset = CreateWindowExW(
	    0, L"COMBOBOX", L"",
	    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 320, 66, 140,
	    200, g_window, reinterpret_cast<HMENU>(kResolutionPreset), instance,
	    nullptr);

	g_resX = CreateWindowExW(
	    WS_EX_CLIENTEDGE, L"EDIT", L"1920",
	    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER, 466, 66, 52, 22,
	    g_window, reinterpret_cast<HMENU>(kResX), instance, nullptr);

	g_resY = CreateWindowExW(
	    WS_EX_CLIENTEDGE, L"EDIT", L"1080",
	    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER, 532, 66, 52, 22,
	    g_window, reinterpret_cast<HMENU>(kResY), instance, nullptr);

	g_scaleLabel = CreateWindowExW(
	    0, L"STATIC", L"\u7a97\u53e3\u5927\u5c0f %:",
	    WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 96, 72, 20, g_window,
	    reinterpret_cast<HMENU>(kScaleLabel), instance, nullptr);

	g_scale = CreateWindowExW(
	    0, TRACKBAR_CLASSW, L"",
	    WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 104, 92, 360, 28, g_window,
	    reinterpret_cast<HMENU>(kScale), instance, nullptr);
	SendMessageW(g_scale, TBM_SETRANGE, TRUE, MAKELPARAM(25, 100));
	SendMessageW(g_scale, TBM_SETTICFREQ, 5, 0);

	g_scaleValue = CreateWindowExW(
	    0, L"STATIC", L"50%",
	    WS_CHILD | WS_VISIBLE | SS_LEFT, 472, 96, 48, 20, g_window,
	    reinterpret_cast<HMENU>(kScaleValue), instance, nullptr);

	g_skipStartup = CreateWindowExW(
	    0, L"BUTTON",
	    L"\u8df3\u8fc7\u7247\u5934 (StartupMovie)",
	    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, 122, 220, 22, g_window,
	    reinterpret_cast<HMENU>(kSkipStartup), instance, nullptr);

	g_skipConfigIntegrity = CreateWindowExW(
	    0, L"BUTTON",
	    L"\u8df3\u8fc7 Default*.ini \u5b8c\u6574\u6027\u68c0\u6d4b",
	    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, 146, 320, 22, g_window,
	    reinterpret_cast<HMENU>(kSkipConfigIntegrity), instance, nullptr);

	g_updateStatus = CreateWindowExW(
	    0, L"STATIC", L"\u66f4\u65b0\uff1a\u672a\u68c0\u67e5",
	    WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 172, 360, 22, g_window,
	    reinterpret_cast<HMENU>(kUpdateStatus), instance, nullptr);

	g_checkUpdate = CreateWindowExW(
	    0, L"BUTTON", L"\u68c0\u67e5\u66f4\u65b0",
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 380, 170, 100, 24, g_window,
	    reinterpret_cast<HMENU>(kCheckUpdate), instance, nullptr);

	g_upgradeNow = CreateWindowExW(
	    0, L"BUTTON", L"\u7acb\u5373\u5347\u7ea7",
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 488, 170, 100, 24, g_window,
	    reinterpret_cast<HMENU>(kUpgradeNow), instance, nullptr);
	EnableWindow(g_upgradeNow, FALSE);

	g_log = CreateWindowExW(
	    WS_EX_CLIENTEDGE, L"EDIT", L"",
	    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
	        ES_READONLY | ES_AUTOVSCROLL,
	    12, 202, 600, 250, g_window, reinterpret_cast<HMENU>(kLog), instance,
	    nullptr);

	g_launch = CreateWindowExW(
	    0, L"BUTTON", L"\u542f\u52a8\u6e38\u620f",
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 464, 100, 30, g_window,
	    reinterpret_cast<HMENU>(kLaunchGame), instance, nullptr);

	g_closeGame = CreateWindowExW(
	    0, L"BUTTON", L"\u5173\u95ed\u6e38\u620f",
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 118, 464, 100, 30, g_window,
	    reinterpret_cast<HMENU>(kCloseGame), instance, nullptr);

	g_close = CreateWindowExW(
	    0, L"BUTTON", L"\u5173\u95ed",
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 512, 464, 100, 30,
	    g_window, reinterpret_cast<HMENU>(kClose), instance, nullptr);

	SendMessageW(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(g_statusFont),
	             TRUE);
	SendMessageW(g_gamePathLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_gamePath, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_browseGamePath, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_displayModeLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_displayMode, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_resolutionLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_resolutionPreset, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_resX, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_resY, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_scaleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_scaleValue, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_skipStartup, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_skipConfigIntegrity, WM_SETFONT,
	             reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_updateStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_checkUpdate, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_upgradeNow, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_log, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_close, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_launch, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_closeGame, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

	SendMessageW(g_displayMode, CB_ADDSTRING, 0,
	             reinterpret_cast<LPARAM>(L"\u7a97\u53e3\u5316 (Windowed)"));
	SendMessageW(g_displayMode, CB_ADDSTRING, 0,
	             reinterpret_cast<LPARAM>(L"\u5168\u5c4f (Fullscreen)"));
	SendMessageW(g_displayMode, CB_ADDSTRING, 0,
	             reinterpret_cast<LPARAM>(L"\u65e0\u8fb9\u6846 (Borderless)"));

	for (const auto &preset : kWindowedResolutionPresets) {
		SendMessageW(g_resolutionPreset, CB_ADDSTRING, 0,
		             reinterpret_cast<LPARAM>(preset.label));
	}

	ShowWindow(g_window, SW_SHOW);
	UpdateWindow(g_window);
	UpdateGamePathField();
	LoadDisplaySettingsToUi();
	FlushPendingLogs();
	BeginUpdateCheck(false);
}

void Destroy() {
	if (g_font) {
		DeleteObject(g_font);
		g_font = nullptr;
	}
	if (g_statusFont) {
		DeleteObject(g_statusFont);
		g_statusFont = nullptr;
	}

	g_window = nullptr;
	g_status = nullptr;
	g_log = nullptr;
	g_close = nullptr;
	g_launch = nullptr;
	g_closeGame = nullptr;
	g_gamePathLabel = nullptr;
	g_gamePath = nullptr;
	g_browseGamePath = nullptr;
	g_displayModeLabel = nullptr;
	g_displayMode = nullptr;
	g_resolutionLabel = nullptr;
	g_resolutionPreset = nullptr;
	g_resX = nullptr;
	g_resY = nullptr;
	g_scaleLabel = nullptr;
	g_scale = nullptr;
	g_scaleValue = nullptr;
	g_skipStartup = nullptr;
	g_skipConfigIntegrity = nullptr;
	g_updateStatus = nullptr;
	g_checkUpdate = nullptr;
	g_upgradeNow = nullptr;
	g_launchHandler = nullptr;
	g_closeGameHandler = nullptr;
	g_finished = false;
	g_success = false;
	g_exitRequested = false;
	g_success = false;
	g_hasLatestRelease = false;
}

void SetStep(const wchar_t *text) {
	if (!text || !g_window) {
		return;
	}
	PostOwnedString(kSetStep, 0, text);
}

void AppendLog(const wchar_t *text) {
	if (!text) {
		return;
	}

	if (!g_window) {
		std::lock_guard<std::mutex> lock(g_pendingMutex);
		g_pendingLogs.emplace_back(text);
		return;
	}

	PostOwnedString(kAppendLog, 0, text);
}

void AppendLogf(const wchar_t *format, ...) {
	wchar_t buffer[1024] = {};
	va_list args;
	va_start(args, format);
	_vsnwprintf(buffer, _countof(buffer), format, args);
	buffer[_countof(buffer) - 1] = L'\0';
	va_end(args);
	AppendLog(buffer);
}

void AppendModLog(const wchar_t *text) {
	if (!text) {
		return;
	}

	std::wstring prefixed = L"[mod] ";
	prefixed += text;

	if (!g_window) {
		std::lock_guard<std::mutex> lock(g_pendingMutex);
		g_pendingLogs.emplace_back(prefixed);
		return;
	}

	PostOwnedString(kAppendModLog, 0, prefixed.c_str());
}

void MarkFinished(bool success) {
	g_success = success;
	if (!g_window) {
		g_finished = true;
		return;
	}
	PostMessageW(g_window, kMarkFinished, success ? 1 : 0, 0);
}

void RequestExit() { g_exitRequested = true; }

bool IsExitRequested() { return g_exitRequested.load(); }

void StartUpdateCheck(bool manual) { BeginUpdateCheck(manual); }

int RunUntilUserCloses() {
	if (!g_window) {
		return (g_success || g_exitRequested) ? 0 : 1;
	}

	MSG message = {};
	while (GetMessageW(&message, nullptr, 0, 0) > 0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}

	return (g_success || g_exitRequested) ? 0 : 1;
}

HWND GetMainWindow() { return g_window; }

void RefreshGamePathDisplay() { UpdateGamePathField(); }

void SaveDisplaySettingsFromUi() { ::SaveDisplaySettingsFromUi(); }

} // namespace StatusDialog
