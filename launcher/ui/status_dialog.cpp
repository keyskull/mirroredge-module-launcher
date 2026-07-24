#include "stdafx.h"

#include "game_config.h"
#include "game_patch.h"
#include "game_path.h"
#include "input_restore.h"
#include "launcher_settings.h"
#include "launcher_i18n.h"
#include "paths.h"
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
	kLanguageLabel = 1023,
	kLanguage = 1024,
	kTabs = 1025,
	kConfigSyncStatus = 1026,
	kApplyConfig = 1027,
	kDeployStatus = 1028,
	kModulesStatus = 1029,
	kDetectModules = 1030,
	kUpdateModules = 1031,
	kPatchDependencies = 1032,
	kFixPhysX = 1033,
};

enum : UINT {
	kSetStep = WM_APP + 1,
	kAppendLog = WM_APP + 2,
	kMarkFinished = WM_APP + 3,
	kAppendModLog = WM_APP + 4,
	kUpdateCheckDone = WM_APP + 6,
	kPatchDone = WM_APP + 7,
	kConfigSyncTimerId = 0xC501,
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
HWND g_modulesStatus = nullptr;
HWND g_detectModules = nullptr;
HWND g_updateModules = nullptr;
HWND g_patchDependencies = nullptr;
HWND g_fixPhysX = nullptr;
HWND g_languageLabel = nullptr;
HWND g_language = nullptr;
HWND g_tabs = nullptr;
HWND g_configSyncStatus = nullptr;
HWND g_applyConfig = nullptr;
HWND g_deployStatus = nullptr;
int g_currentTab = 0;
HFONT g_font = nullptr;
HFONT g_statusFont = nullptr;
bool g_finished = false;
bool g_success = false;
std::atomic<bool> g_exitRequested{false};
std::atomic<bool> g_updateBusy{false};
std::atomic<bool> g_patchBusy{false};
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
	SetWindowTextW(g_close, LauncherI18n::T(LauncherI18n::Str::Close));
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

	SetWindowTextW(g_gamePath, LauncherI18n::T(LauncherI18n::Str::GamePathMissingHint));
}

struct UpdateCheckResultPayload {
	UpdateCheck::ReleaseInfo info;
	bool manual = false;
};


void SetPatchUiBusy(bool busy) {
	const BOOL enable = busy ? FALSE : TRUE;
	if (g_detectModules) {
		EnableWindow(g_detectModules, enable);
	}
	if (g_updateModules) {
		EnableWindow(g_updateModules, enable);
	}
	if (g_patchDependencies) {
		EnableWindow(g_patchDependencies, enable);
	}
	if (g_fixPhysX) {
		EnableWindow(g_fixPhysX, enable);
	}
}

void LogUiDownloadProgress(unsigned long long downloaded, unsigned long long total,
                           void *) {
	if (total > 0) {
		const unsigned pct =
		    static_cast<unsigned>((downloaded * 100ULL) / total);
		StatusDialog::AppendLogf(
		    LauncherI18n::T(LauncherI18n::Str::DownloadProgressPctFmt), pct,
		    downloaded / 1024ULL, total / 1024ULL);
	} else {
		StatusDialog::AppendLogf(
		    LauncherI18n::T(LauncherI18n::Str::DownloadProgressBytesFmt),
		    downloaded / 1024ULL);
	}
}

enum class PatchAction : int {
	UpdateModules = 1,
	PatchDependencies = 2,
	FixPhysX = 3,
};

DWORD WINAPI PatchWorker(LPVOID param) {
	const auto action = static_cast<PatchAction>(reinterpret_cast<INT_PTR>(param));
	bool ok = false;
	switch (action) {
	case PatchAction::UpdateModules: {
		std::wstring gameRoot;
		if (Paths::GetGameRootDirectory(gameRoot)) {
			ok = GamePatch::PatchModulesToGame(gameRoot);
		} else {
			StatusDialog::AppendLog(
			    LauncherI18n::T(LauncherI18n::Str::ModulesNoGame));
		}
		break;
	}
	case PatchAction::PatchDependencies:
		ok = GamePatch::PatchDependenciesToGame();
		break;
	case PatchAction::FixPhysX:
		ok = GamePatch::PatchPhysXToGame();
		break;
	}
	if (g_window) {
		PostMessageW(g_window, kPatchDone, ok ? 1 : 0, 0);
	} else {
		g_patchBusy = false;
	}
	return ok ? 0 : 1;
}

void BeginPatchAction(PatchAction action) {
	if (g_patchBusy.exchange(true)) {
		AppendLogLine(LauncherI18n::T(LauncherI18n::Str::PatchBusy));
		return;
	}
	SetPatchUiBusy(true);
	HANDLE thread = CreateThread(
	    nullptr, 0, PatchWorker,
	    reinterpret_cast<LPVOID>(static_cast<INT_PTR>(action)), 0, nullptr);
	if (!thread) {
		g_patchBusy = false;
		SetPatchUiBusy(false);
		AppendLogLine(LauncherI18n::T(LauncherI18n::Str::PatchWorkerFail));
		return;
	}
	CloseHandle(thread);
}

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
		           LauncherI18n::T(LauncherI18n::Str::UpdateAvailableFmt),
		           payload.info.version.c_str(),
		           UpdateCheck::LocalVersionString().c_str());
		line[255] = L'\0';
		if (g_updateStatus) {
			SetWindowTextW(g_updateStatus, line);
		}
		AppendLogLine(line);
		if (soft) {
			AppendLogLine(
			    LauncherI18n::T(LauncherI18n::Str::UpdateDismissedHint));
		} else if (!payload.manual) {
			MessageBoxW(g_window, line, LauncherI18n::T(LauncherI18n::Str::AppTitle),
			            MB_ICONINFORMATION | MB_OK);
			LauncherSettings::SaveDismissedUpdateVersion(payload.info.version);
		}
		break;
	}
	case UpdateCheck::CheckStatus::UpToDate:
		_snwprintf(line, 255, LauncherI18n::T(LauncherI18n::Str::UpToDateFmt),
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
			    LauncherI18n::T(LauncherI18n::Str::NoGithubRelease));
		}
		if (payload.manual) {
			AppendLogLine(
			    LauncherI18n::T(LauncherI18n::Str::NoReleaseLog));
		}
		break;
	case UpdateCheck::CheckStatus::Skipped:
		if (g_updateStatus) {
			SetWindowTextW(g_updateStatus,
			               LauncherI18n::T(LauncherI18n::Str::UpdateSkipped));
		}
		break;
	default:
		if (g_updateStatus) {
			SetWindowTextW(
			    g_updateStatus,
			    LauncherI18n::T(LauncherI18n::Str::UpdateCheckFailed));
		}
		if (payload.manual || !payload.info.errorMessage.empty()) {
			AppendLogLine(
			    (std::wstring(LauncherI18n::T(LauncherI18n::Str::UpdateCheckFailedPrefix)) + payload.info.errorMessage)
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
		AppendLogLine(LauncherI18n::T(LauncherI18n::Str::UpdateCheckBusy));
		return;
	}
	if (g_updateStatus) {
		SetWindowTextW(g_updateStatus, LauncherI18n::T(LauncherI18n::Str::CheckingUpdate));
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
		AppendLogLine(LauncherI18n::T(LauncherI18n::Str::UpdateThreadFail));
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
		           LauncherI18n::T(LauncherI18n::Str::CloseBeforeUpgradeFmt),
		           blocking.c_str());
		msg[255] = L'\0';
		MessageBoxW(g_window, msg, LauncherI18n::T(LauncherI18n::Str::AppTitle),
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
	    LauncherI18n::T(LauncherI18n::Str::DownloadingRelease));
	std::wstring err;
	if (!UpdateCheck::DownloadFile(g_latestRelease.downloadUrl, zipFile, &err,
	                               LogUiDownloadProgress, nullptr)) {
		StatusDialog::AppendLog((std::wstring(LauncherI18n::T(LauncherI18n::Str::DownloadFailedPrefix)) + err).c_str());
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 1;
	}

	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::UnpackingUpdate));
	std::wstring batPath;
	if (!UpdateCheck::PrepareApplyUpdate(zipFile, GetCurrentProcessId(), batPath,
	                                     &err)) {
		StatusDialog::AppendLog(
		    (std::wstring(LauncherI18n::T(LauncherI18n::Str::PrepareUpgradeFailedPrefix)) + err).c_str());
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 1;
	}

	const int confirm = MessageBoxW(
	    g_window,
	    LauncherI18n::T(LauncherI18n::Str::ConfirmApplyUpdate),
	    LauncherI18n::T(LauncherI18n::Str::AppTitle), MB_ICONQUESTION | MB_YESNO);
	if (confirm != IDYES) {
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 0;
	}

	if (!UpdateCheck::LaunchApplyBat(batPath, &err)) {
		StatusDialog::AppendLog(
		    (std::wstring(LauncherI18n::T(LauncherI18n::Str::LaunchBatFailedPrefix)) + err).c_str());
		g_updateBusy = false;
		PostMessageW(g_window, kUpdateCheckDone, 1, 0);
		return 1;
	}

	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::ApplyingUpdate));
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
		AppendLogLine(LauncherI18n::T(LauncherI18n::Str::UpgradeThreadFail));
		return;
	}
	CloseHandle(thread);
}

struct ResolutionPreset {
	int width;
	int height;
};

constexpr int kMatchWindowPresetWidth = -1;

constexpr ResolutionPreset kWindowedResolutionPresets[] = {
    {1920, 1080},
    {1600, 1200},
    {1280, 720},
    {1280, 1024},
    {0, 0},
};

constexpr ResolutionPreset kBorderlessResolutionPresets[] = {
    {kMatchWindowPresetWidth, kMatchWindowPresetWidth},
    {1920, 1080},
    {1600, 1200},
    {1280, 720},
    {1280, 1024},
    {0, 0},
};

std::wstring FormatResolutionPresetLabel(const ResolutionPreset &preset) {
	if (preset.width == kMatchWindowPresetWidth) {
		return LauncherI18n::T(LauncherI18n::Str::PresetMatchWindow);
	}
	if (preset.width <= 0 || preset.height <= 0) {
		return LauncherI18n::T(LauncherI18n::Str::PresetCustom);
	}
	wchar_t buffer[64] = {};
	_snwprintf(buffer, 63, L"%d x %d", preset.width, preset.height);
	buffer[63] = L'\0';
	return buffer;
}

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
		const std::wstring label = FormatResolutionPresetLabel(presets[i]);
		SendMessageW(g_resolutionPreset, CB_ADDSTRING, 0,
		             reinterpret_cast<LPARAM>(label.c_str()));
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


void PopulateDisplayModeCombo() {
	if (!g_displayMode) {
		return;
	}
	const auto sel = static_cast<int>(SendMessageW(g_displayMode, CB_GETCURSEL, 0, 0));
	SendMessageW(g_displayMode, CB_RESETCONTENT, 0, 0);
	SendMessageW(g_displayMode, CB_ADDSTRING, 0,
	             reinterpret_cast<LPARAM>(LauncherI18n::T(LauncherI18n::Str::ModeWindowed)));
	SendMessageW(g_displayMode, CB_ADDSTRING, 0,
	             reinterpret_cast<LPARAM>(LauncherI18n::T(LauncherI18n::Str::ModeFullscreen)));
	SendMessageW(g_displayMode, CB_ADDSTRING, 0,
	             reinterpret_cast<LPARAM>(LauncherI18n::T(LauncherI18n::Str::ModeBorderless)));
	if (sel >= 0) {
		SendMessageW(g_displayMode, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
	}
}


void SetControlsVisible(const HWND *handles, size_t count, bool visible) {
	const int cmd = visible ? SW_SHOW : SW_HIDE;
	for (size_t i = 0; i < count; ++i) {
		if (handles[i]) {
			ShowWindow(handles[i], cmd);
		}
	}
}

// Tab headers must stay clickable, but the tab body must not sit above page
// controls — otherwise after a tab click, Browse/path stop receiving mouse input.
void SendTabControlToBack() {
	if (!g_tabs) {
		return;
	}
	SetWindowPos(g_tabs, HWND_BOTTOM, 0, 0, 0, 0,
	             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

DisplaySettings ReadDisplaySettingsFromUi() {
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

	return settings;
}

void RefreshConfigSyncStatus() {
	if (!g_window) {
		return;
	}

	const auto expected = ReadDisplaySettingsFromUi();
	const auto sync = GameConfig::EvaluateConfigSync(expected);

	if (g_configSyncStatus) {
		if (sync.displayMatched) {
			SetWindowTextW(g_configSyncStatus,
			               LauncherI18n::T(LauncherI18n::Str::ConfigSynced));
		} else {
			std::wstring reasons;
			auto appendReason = [&](LauncherI18n::Str id) {
				if (!reasons.empty()) {
					reasons += L"; ";
				}
				reasons += LauncherI18n::T(id);
			};
			if (!sync.tdEngineOk) {
				appendReason(LauncherI18n::Str::ConfigReasonTdMissing);
			} else if (!sync.tdContentOk) {
				appendReason(LauncherI18n::Str::ConfigReasonDisplay);
			}
			if (!sync.windowLayoutOk) {
				appendReason(LauncherI18n::Str::ConfigReasonWindow);
			}
			if (reasons.empty()) {
				appendReason(LauncherI18n::Str::ConfigReasonDisplay);
			}

			wchar_t line[256] = {};
			_snwprintf(line, 255, L"%s (%s)",
			           LauncherI18n::T(LauncherI18n::Str::ConfigUnsyncedPrefix),
			           reasons.c_str());
			line[255] = L'\0';
			SetWindowTextW(g_configSyncStatus, line);
		}
	}

	if (g_applyConfig) {
		EnableWindow(g_applyConfig, sync.displayMatched ? FALSE : TRUE);
	}

	if (g_deployStatus) {
		wchar_t line[256] = {};
		_snwprintf(line, 255,
		           LauncherI18n::T(LauncherI18n::Str::DeployStatusFmt),
		           sync.proxyPresent
		               ? LauncherI18n::T(LauncherI18n::Str::DeployPresent)
		               : LauncherI18n::T(LauncherI18n::Str::DeployAbsent),
		           sync.managerPresent
		               ? LauncherI18n::T(LauncherI18n::Str::DeployPresent)
		               : LauncherI18n::T(LauncherI18n::Str::DeployAbsent));
		line[255] = L'\0';
		SetWindowTextW(g_deployStatus, line);
	}
}

void RefreshModulesSyncStatus(bool logDetails) {
	if (!g_modulesStatus) {
		return;
	}

	std::wstring gameRoot;
	if (!Paths::GetGameRootDirectory(gameRoot)) {
		SetWindowTextW(g_modulesStatus,
		               LauncherI18n::T(LauncherI18n::Str::ModulesNoGame));
		return;
	}

	const auto status = GamePatch::EvaluateModulesSync(gameRoot);
	SetWindowTextW(g_modulesStatus, status.summary.c_str());
	if (logDetails) {
		AppendLogLine(status.summary.c_str());
	}
}

void ShowTabPage(int index) {
	const HWND launchControls[] = {
	    g_gamePathLabel, g_gamePath, g_browseGamePath, g_skipStartup,
	    g_skipConfigIntegrity, g_deployStatus,
	};
	const HWND displayControls[] = {
	    g_displayModeLabel, g_displayMode, g_resolutionLabel, g_resolutionPreset,
	    g_resX,            g_resY,        g_scaleLabel,      g_scale,
	    g_scaleValue,      g_configSyncStatus, g_applyConfig,
	};
	const HWND updateControls[] = {
	    g_updateStatus, g_checkUpdate, g_upgradeNow, g_modulesStatus,
	    g_detectModules, g_updateModules, g_patchDependencies, g_fixPhysX,
	};

	SetControlsVisible(launchControls, _countof(launchControls), index == 0);
	SetControlsVisible(displayControls, _countof(displayControls), index == 1);
	SetControlsVisible(updateControls, _countof(updateControls), index == 2);
	g_currentTab = index;
	SendTabControlToBack();

	if (index == 1) {
		UpdateBorderlessControlsEnabled();
		UpdateResolutionFieldsEnabled();
	}
	RefreshConfigSyncStatus();
	if (index == 2) {
		RefreshModulesSyncStatus(false);
	}
}

void RefreshTabTitles() {
	if (!g_tabs) {
		return;
	}
	TCITEMW item = {};
	item.mask = TCIF_TEXT;
	const wchar_t *titles[] = {
	    LauncherI18n::T(LauncherI18n::Str::TabLaunch),
	    LauncherI18n::T(LauncherI18n::Str::TabDisplay),
	    LauncherI18n::T(LauncherI18n::Str::TabUpdate),
	};
	for (int i = 0; i < 3; ++i) {
		item.pszText = const_cast<wchar_t *>(titles[i]);
		TabCtrl_SetItem(g_tabs, i, &item);
	}
}

void PopulateLanguageCombo() {
	if (!g_language) {
		return;
	}
	SendMessageW(g_language, CB_RESETCONTENT, 0, 0);
	SendMessageW(g_language, CB_ADDSTRING, 0,
	             reinterpret_cast<LPARAM>(LauncherI18n::LanguageDisplayName(LauncherI18n::Lang::Zh)));
	SendMessageW(g_language, CB_ADDSTRING, 0,
	             reinterpret_cast<LPARAM>(LauncherI18n::LanguageDisplayName(LauncherI18n::Lang::En)));
	const auto sel = LauncherI18n::Current() == LauncherI18n::Lang::En ? 1 : 0;
	SendMessageW(g_language, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
}

void ApplyLocalizedUi() {
	wchar_t windowTitle[128] = {};
	_snwprintf(windowTitle, 127, L"%s v%hs", LauncherI18n::T(LauncherI18n::Str::AppTitle),
	           MMOD_PRODUCT_VERSION_STRING);
	windowTitle[127] = L'\0';
	if (g_window) {
		SetWindowTextW(g_window, windowTitle);
	}
	if (g_gamePathLabel) {
		SetWindowTextW(g_gamePathLabel, LauncherI18n::T(LauncherI18n::Str::GamePath));
	}
	if (g_browseGamePath) {
		SetWindowTextW(g_browseGamePath, LauncherI18n::T(LauncherI18n::Str::Browse));
	}
	if (g_displayModeLabel) {
		SetWindowTextW(g_displayModeLabel, LauncherI18n::T(LauncherI18n::Str::DisplayMode));
	}
	if (g_resolutionLabel) {
		SetWindowTextW(g_resolutionLabel, LauncherI18n::T(LauncherI18n::Str::RenderResolution));
	}
	if (g_scaleLabel) {
		SetWindowTextW(g_scaleLabel, LauncherI18n::T(LauncherI18n::Str::WindowScalePct));
	}
	if (g_skipStartup) {
		SetWindowTextW(g_skipStartup, LauncherI18n::T(LauncherI18n::Str::SkipIntroMovies));
	}
	if (g_skipConfigIntegrity) {
		SetWindowTextW(g_skipConfigIntegrity, LauncherI18n::T(LauncherI18n::Str::SkipIniIntegrity));
	}
	if (g_updateStatus && !g_updateBusy.load() && !g_hasLatestRelease) {
		SetWindowTextW(g_updateStatus, LauncherI18n::T(LauncherI18n::Str::UpdateUnchecked));
	}
	if (g_checkUpdate) {
		SetWindowTextW(g_checkUpdate, LauncherI18n::T(LauncherI18n::Str::CheckUpdate));
	}
	if (g_upgradeNow) {
		SetWindowTextW(g_upgradeNow, LauncherI18n::T(LauncherI18n::Str::UpgradeNow));
	}
	if (g_modulesStatus) {
		const int len = GetWindowTextLengthW(g_modulesStatus);
		if (len <= 0) {
			SetWindowTextW(g_modulesStatus,
			               LauncherI18n::T(LauncherI18n::Str::ModulesStatusUnchecked));
		}
	}
	if (g_detectModules) {
		SetWindowTextW(g_detectModules,
		               LauncherI18n::T(LauncherI18n::Str::DetectModules));
	}
	if (g_updateModules) {
		SetWindowTextW(g_updateModules,
		               LauncherI18n::T(LauncherI18n::Str::UpdateModules));
	}
	if (g_patchDependencies) {
		SetWindowTextW(g_patchDependencies,
		               LauncherI18n::T(LauncherI18n::Str::PatchDependencies));
	}
	if (g_fixPhysX) {
		SetWindowTextW(g_fixPhysX, LauncherI18n::T(LauncherI18n::Str::FixPhysX));
	}
	if (g_launch) {
		SetWindowTextW(g_launch, LauncherI18n::T(LauncherI18n::Str::LaunchGame));
	}
	if (g_closeGame) {
		SetWindowTextW(g_closeGame, LauncherI18n::T(LauncherI18n::Str::CloseGame));
	}
	if (g_languageLabel) {
		SetWindowTextW(g_languageLabel, LauncherI18n::T(LauncherI18n::Str::Language));
	}
	if (g_applyConfig) {
		SetWindowTextW(g_applyConfig, LauncherI18n::T(LauncherI18n::Str::ApplyConfig));
	}
	UpdateCloseButton();
	PopulateLanguageCombo();
	PopulateDisplayModeCombo();
	RefreshTabTitles();
	ShowTabPage(g_currentTab);
	const auto resSel = g_resolutionPreset
	                        ? static_cast<int>(SendMessageW(g_resolutionPreset, CB_GETCURSEL, 0, 0))
	                        : -1;
	PopulateResolutionPresetCombo();
	if (g_resolutionPreset && resSel >= 0) {
		SendMessageW(g_resolutionPreset, CB_SETCURSEL, static_cast<WPARAM>(resSel), 0);
	}
	UpdateGamePathField();
	RefreshConfigSyncStatus();
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
	const DisplaySettings settings = ReadDisplaySettingsFromUi();
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
	case kPatchDone: {
		g_patchBusy = false;
		SetPatchUiBusy(false);
		if (wparam != 0) {
			RefreshModulesSyncStatus(true);
			RefreshConfigSyncStatus();
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
		if (wparam == kConfigSyncTimerId) {
			if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
				RefreshConfigSyncStatus();
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
			RefreshConfigSyncStatus();
			return 0;
		}
		break;
	}
	case WM_NOTIFY: {
		auto *hdr = reinterpret_cast<LPNMHDR>(lparam);
		if (hdr && hdr->hwndFrom == g_tabs && hdr->code == TCN_SELCHANGE) {
			ShowTabPage(TabCtrl_GetCurSel(g_tabs));
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
				AppendLogLine((std::wstring(LauncherI18n::T(LauncherI18n::Str::WarningPrefix)) + applyLog).c_str());
			}
			RefreshConfigSyncStatus();
			g_launchHandler();
			return 0;
		}
		if (LOWORD(wparam) == kApplyConfig) {
			SaveDisplaySettingsFromUi();
			const auto displaySettings = LauncherSettings::LoadDisplaySettings();
			std::wstring applyLog;
			if (GameConfig::ApplyDisplaySettings(displaySettings, &applyLog)) {
				if (!applyLog.empty()) {
					AppendLogLine(applyLog.c_str());
				}
			} else if (!applyLog.empty()) {
				AppendLogLine((std::wstring(LauncherI18n::T(LauncherI18n::Str::WarningPrefix)) +
				               applyLog)
				                  .c_str());
			}
			RefreshConfigSyncStatus();
			return 0;
		}
		if (LOWORD(wparam) == kCloseGame && g_closeGameHandler) {
			g_closeGameHandler();
			return 0;
		}
		if (LOWORD(wparam) == kBrowseGamePath) {
			std::wstring gameRoot;
			if (GamePath::BrowseForGameRoot(hwnd, gameRoot)) {
				if (g_gamePath) {
					SetWindowTextW(g_gamePath, gameRoot.c_str());
				}
				AppendLogLine((std::wstring(LauncherI18n::T(LauncherI18n::Str::PathSavedPrefix)) + gameRoot).c_str());
				RefreshConfigSyncStatus();
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
		if (LOWORD(wparam) == kDetectModules) {
			RefreshModulesSyncStatus(true);
			return 0;
		}
		if (LOWORD(wparam) == kUpdateModules) {
			BeginPatchAction(PatchAction::UpdateModules);
			return 0;
		}
		if (LOWORD(wparam) == kPatchDependencies) {
			BeginPatchAction(PatchAction::PatchDependencies);
			return 0;
		}
		if (LOWORD(wparam) == kFixPhysX) {
			BeginPatchAction(PatchAction::FixPhysX);
			return 0;
		}
		if (LOWORD(wparam) == kLanguage && HIWORD(wparam) == CBN_SELCHANGE) {
			const auto sel =
			    static_cast<int>(SendMessageW(g_language, CB_GETCURSEL, 0, 0));
			const auto lang = sel == 1 ? LauncherI18n::Lang::En : LauncherI18n::Lang::Zh;
			if (lang != LauncherI18n::Current()) {
				LauncherI18n::SetLanguage(lang);
				ApplyLocalizedUi();
			}
			return 0;
		}
		if (LOWORD(wparam) == kDisplayMode && HIWORD(wparam) == CBN_SELCHANGE) {
			UpdateBorderlessControlsEnabled();
			RefreshConfigSyncStatus();
			return 0;
		}
		if (LOWORD(wparam) == kResolutionPreset && HIWORD(wparam) == CBN_SELCHANGE) {
			const auto presetIndex = static_cast<int>(
			    SendMessageW(g_resolutionPreset, CB_GETCURSEL, 0, 0));
			ApplyResolutionPresetToFields(presetIndex);
			RefreshConfigSyncStatus();
			return 0;
		}
		if ((LOWORD(wparam) == kSkipStartup ||
		     LOWORD(wparam) == kSkipConfigIntegrity) &&
		    HIWORD(wparam) == BN_CLICKED) {
			RefreshConfigSyncStatus();
			return 0;
		}
		if ((LOWORD(wparam) == kResX || LOWORD(wparam) == kResY) &&
		    HIWORD(wparam) == EN_CHANGE) {
			RefreshConfigSyncStatus();
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
		KillTimer(hwnd, kConfigSyncTimerId);
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

	LauncherI18n::Initialize();

	INITCOMMONCONTROLSEX controls = {sizeof(controls),
	                                 ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
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
	_snwprintf(windowTitle, 127, L"%s v%hs", LauncherI18n::T(LauncherI18n::Str::AppTitle),
	           MMOD_PRODUCT_VERSION_STRING);
	windowTitle[127] = L'\0';

	g_window = CreateWindowExW(
	    WS_EX_APPWINDOW, className,
	    windowTitle,
	    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
	    CW_USEDEFAULT, 640, 600, nullptr, nullptr, instance, nullptr);

	if (!g_window) {
		MessageBoxW(nullptr, LauncherI18n::T(LauncherI18n::Str::CreateWindowFailed),
		            LauncherI18n::T(LauncherI18n::Str::AppTitle), MB_ICONERROR | MB_OK);
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
	    0, L"STATIC", LauncherI18n::T(LauncherI18n::Str::PreparingEllipsis),
	    WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 12, 400, 28, g_window,
	    reinterpret_cast<HMENU>(kStatus), instance, nullptr);

	g_languageLabel = CreateWindowExW(
	    0, L"STATIC", LauncherI18n::T(LauncherI18n::Str::Language),
	    WS_CHILD | WS_VISIBLE | SS_RIGHT, 400, 16, 72, 20, g_window,
	    reinterpret_cast<HMENU>(kLanguageLabel), instance, nullptr);

	g_language = CreateWindowExW(
	    0, L"COMBOBOX", L"",
	    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 476, 12, 136,
	    120, g_window, reinterpret_cast<HMENU>(kLanguage), instance, nullptr);

	const int tabX = 12;
	const int tabY = 42;
	const int tabW = 600;
	const int tabH = 220;

	g_tabs = CreateWindowExW(
	    0, WC_TABCONTROLW, L"",
	    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, tabX, tabY, tabW, tabH, g_window,
	    reinterpret_cast<HMENU>(kTabs), instance, nullptr);

	TCITEMW tabItem = {};
	tabItem.mask = TCIF_TEXT;
	tabItem.pszText = const_cast<wchar_t *>(LauncherI18n::T(LauncherI18n::Str::TabLaunch));
	TabCtrl_InsertItem(g_tabs, 0, &tabItem);
	tabItem.pszText = const_cast<wchar_t *>(LauncherI18n::T(LauncherI18n::Str::TabDisplay));
	TabCtrl_InsertItem(g_tabs, 1, &tabItem);
	tabItem.pszText = const_cast<wchar_t *>(LauncherI18n::T(LauncherI18n::Str::TabUpdate));
	TabCtrl_InsertItem(g_tabs, 2, &tabItem);

	RECT contentRc = {0, 0, tabW, tabH};
	TabCtrl_AdjustRect(g_tabs, FALSE, &contentRc);
	MapWindowPoints(g_tabs, g_window, reinterpret_cast<POINT *>(&contentRc), 2);
	const int cx = contentRc.left + 10;
	const int cy = contentRc.top + 10;
	const int cw = contentRc.right - contentRc.left - 20;

	g_gamePathLabel = CreateWindowExW(
	    0, L"STATIC", LauncherI18n::T(LauncherI18n::Str::GamePath),
	    WS_CHILD | WS_VISIBLE | SS_LEFT, cx, cy, 80, 20, g_window,
	    reinterpret_cast<HMENU>(kGamePathLabel), instance, nullptr);

	g_gamePath = CreateWindowExW(
	    WS_EX_CLIENTEDGE, L"EDIT", L"",
	    WS_CHILD | WS_VISIBLE | ES_LEFT | ES_READONLY | ES_AUTOHSCROLL, cx + 84,
	    cy - 2, cw - 170, 22, g_window, reinterpret_cast<HMENU>(kGamePath),
	    instance, nullptr);

	g_browseGamePath = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::Browse),
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, cx + cw - 80, cy - 4, 80, 24,
	    g_window, reinterpret_cast<HMENU>(kBrowseGamePath), instance, nullptr);

	g_skipStartup = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::SkipIntroMovies),
	    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, cx, cy + 30, cw, 22, g_window,
	    reinterpret_cast<HMENU>(kSkipStartup), instance, nullptr);

	g_skipConfigIntegrity = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::SkipIniIntegrity),
	    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, cx, cy + 54, cw, 22, g_window,
	    reinterpret_cast<HMENU>(kSkipConfigIntegrity), instance, nullptr);

	g_deployStatus = CreateWindowExW(
	    0, L"STATIC", L"",
	    WS_CHILD | WS_VISIBLE | SS_LEFT, cx, cy + 82, cw, 20, g_window,
	    reinterpret_cast<HMENU>(kDeployStatus), instance, nullptr);

	g_displayModeLabel = CreateWindowExW(
	    0, L"STATIC", LauncherI18n::T(LauncherI18n::Str::DisplayMode),
	    WS_CHILD | SS_LEFT, cx, cy, 90, 20, g_window,
	    reinterpret_cast<HMENU>(kDisplayModeLabel), instance, nullptr);

	g_displayMode = CreateWindowExW(
	    0, L"COMBOBOX", L"",
	    WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, cx + 94, cy - 2, 180, 200,
	    g_window, reinterpret_cast<HMENU>(kDisplayMode), instance, nullptr);

	g_resolutionLabel = CreateWindowExW(
	    0, L"STATIC", LauncherI18n::T(LauncherI18n::Str::RenderResolution),
	    WS_CHILD | SS_LEFT, cx, cy + 30, 90, 20, g_window,
	    reinterpret_cast<HMENU>(kResolutionLabel), instance, nullptr);

	g_resolutionPreset = CreateWindowExW(
	    0, L"COMBOBOX", L"",
	    WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, cx + 94, cy + 28, 200, 200,
	    g_window, reinterpret_cast<HMENU>(kResolutionPreset), instance, nullptr);

	g_resX = CreateWindowExW(
	    WS_EX_CLIENTEDGE, L"EDIT", L"1920",
	    WS_CHILD | ES_NUMBER | ES_CENTER, cx + 302, cy + 28, 52, 22, g_window,
	    reinterpret_cast<HMENU>(kResX), instance, nullptr);

	g_resY = CreateWindowExW(
	    WS_EX_CLIENTEDGE, L"EDIT", L"1080",
	    WS_CHILD | ES_NUMBER | ES_CENTER, cx + 360, cy + 28, 52, 22, g_window,
	    reinterpret_cast<HMENU>(kResY), instance, nullptr);

	g_scaleLabel = CreateWindowExW(
	    0, L"STATIC", LauncherI18n::T(LauncherI18n::Str::WindowScalePct),
	    WS_CHILD | SS_LEFT, cx, cy + 60, 90, 20, g_window,
	    reinterpret_cast<HMENU>(kScaleLabel), instance, nullptr);

	g_scale = CreateWindowExW(
	    0, TRACKBAR_CLASSW, L"",
	    WS_CHILD | TBS_AUTOTICKS, cx + 94, cy + 56, cw - 160, 28, g_window,
	    reinterpret_cast<HMENU>(kScale), instance, nullptr);
	SendMessageW(g_scale, TBM_SETRANGE, TRUE, MAKELPARAM(25, 100));
	SendMessageW(g_scale, TBM_SETTICFREQ, 5, 0);

	g_scaleValue = CreateWindowExW(
	    0, L"STATIC", L"50%",
	    WS_CHILD | SS_LEFT, cx + cw - 48, cy + 60, 48, 20, g_window,
	    reinterpret_cast<HMENU>(kScaleValue), instance, nullptr);

	g_configSyncStatus = CreateWindowExW(
	    0, L"STATIC", L"",
	    WS_CHILD | SS_LEFT, cx, cy + 92, cw - 120, 20, g_window,
	    reinterpret_cast<HMENU>(kConfigSyncStatus), instance, nullptr);

	g_applyConfig = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::ApplyConfig),
	    WS_CHILD | BS_PUSHBUTTON, cx + cw - 110, cy + 88, 110, 26, g_window,
	    reinterpret_cast<HMENU>(kApplyConfig), instance, nullptr);

	g_updateStatus = CreateWindowExW(
	    0, L"STATIC", LauncherI18n::T(LauncherI18n::Str::UpdateUnchecked),
	    WS_CHILD | SS_LEFT, cx, cy, cw, 22, g_window,
	    reinterpret_cast<HMENU>(kUpdateStatus), instance, nullptr);

	g_checkUpdate = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::CheckUpdate),
	    WS_CHILD | BS_PUSHBUTTON, cx, cy + 30, 140, 28, g_window,
	    reinterpret_cast<HMENU>(kCheckUpdate), instance, nullptr);

	g_upgradeNow = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::UpgradeNow),
	    WS_CHILD | BS_PUSHBUTTON, cx + 150, cy + 30, 140, 28, g_window,
	    reinterpret_cast<HMENU>(kUpgradeNow), instance, nullptr);
	EnableWindow(g_upgradeNow, FALSE);

	g_modulesStatus = CreateWindowExW(
	    0, L"STATIC", LauncherI18n::T(LauncherI18n::Str::ModulesStatusUnchecked),
	    WS_CHILD | SS_LEFT, cx, cy + 66, cw, 22, g_window,
	    reinterpret_cast<HMENU>(kModulesStatus), instance, nullptr);

	g_detectModules = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::DetectModules),
	    WS_CHILD | BS_PUSHBUTTON, cx, cy + 92, 120, 28, g_window,
	    reinterpret_cast<HMENU>(kDetectModules), instance, nullptr);

	g_updateModules = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::UpdateModules),
	    WS_CHILD | BS_PUSHBUTTON, cx + 128, cy + 92, 120, 28, g_window,
	    reinterpret_cast<HMENU>(kUpdateModules), instance, nullptr);

	g_patchDependencies = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::PatchDependencies),
	    WS_CHILD | BS_PUSHBUTTON, cx + 256, cy + 92, 100, 28, g_window,
	    reinterpret_cast<HMENU>(kPatchDependencies), instance, nullptr);

	g_fixPhysX = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::FixPhysX),
	    WS_CHILD | BS_PUSHBUTTON, cx + 364, cy + 92, 110, 28, g_window,
	    reinterpret_cast<HMENU>(kFixPhysX), instance, nullptr);

	const int logY = tabY + tabH + 10;
	g_log = CreateWindowExW(
	    WS_EX_CLIENTEDGE, L"EDIT", L"",
	    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
	        ES_READONLY | ES_AUTOVSCROLL,
	    12, logY, 600, 230, g_window, reinterpret_cast<HMENU>(kLog), instance,
	    nullptr);

	const int btnY = logY + 242;
	g_launch = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::LaunchGame),
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, btnY, 100, 30, g_window,
	    reinterpret_cast<HMENU>(kLaunchGame), instance, nullptr);

	g_closeGame = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::CloseGame),
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 118, btnY, 100, 30, g_window,
	    reinterpret_cast<HMENU>(kCloseGame), instance, nullptr);

	g_close = CreateWindowExW(
	    0, L"BUTTON", LauncherI18n::T(LauncherI18n::Str::Close),
	    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 512, btnY, 100, 30, g_window,
	    reinterpret_cast<HMENU>(kClose), instance, nullptr);

	SendMessageW(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(g_statusFont),
	             TRUE);
	SendMessageW(g_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_languageLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_language, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
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
	SendMessageW(g_deployStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_configSyncStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_applyConfig, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_updateStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_checkUpdate, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_upgradeNow, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_modulesStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_detectModules, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_updateModules, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_patchDependencies, WM_SETFONT, reinterpret_cast<WPARAM>(g_font),
	             TRUE);
	SendMessageW(g_fixPhysX, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_log, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_close, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_launch, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
	SendMessageW(g_closeGame, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

	PopulateLanguageCombo();
	PopulateDisplayModeCombo();
	PopulateResolutionPresetCombo();
	TabCtrl_SetCurSel(g_tabs, 0);
	ShowTabPage(0);
	SendTabControlToBack();

	ShowWindow(g_window, SW_SHOW);
	UpdateWindow(g_window);
	ApplyLocalizedUi();
	UpdateGamePathField();
	LoadDisplaySettingsToUi();
	RefreshConfigSyncStatus();
	SetTimer(g_window, kConfigSyncTimerId, 2000, nullptr);
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
	g_deployStatus = nullptr;
	g_configSyncStatus = nullptr;
	g_applyConfig = nullptr;
	g_updateStatus = nullptr;
	g_checkUpdate = nullptr;
	g_upgradeNow = nullptr;
	g_languageLabel = nullptr;
	g_language = nullptr;
	g_tabs = nullptr;
	g_currentTab = 0;
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
