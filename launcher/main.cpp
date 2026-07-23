#include "stdafx.h"

#include "config.h"
#include "game_launch.h"
#include "game_path.h"
#include "injection_flow.h"
#include "log_server.h"
#include "config_integrity_bypass.h"
#include "product_version.h"
#include "launcher_i18n.h"
#include "ui/status_dialog.h"

static bool IsAutoMode() {
	const auto cmd = GetCommandLineW();
	return wcsstr(cmd, L"/auto") || wcsstr(cmd, L"-auto");
}

struct LauncherContext {
	bool autoMode = false;
	bool success = false;
};

static DWORD WINAPI LauncherWorker(LPVOID param) {
	auto *ctx = reinterpret_cast<LauncherContext *>(param);

	StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::Preparing));
	StatusDialog::AppendLogf(L"Mirror's Edge Module Launcher v%S (%S)",
	                         MMOD_PRODUCT_VERSION_STRING,
	                         MMOD_PRODUCT_RELEASE_DATE);

	std::wstring gameRoot;
	std::wstring pathSource;
	if (GamePath::ResolveGameRoot(gameRoot, &pathSource)) {
		StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::GamePathWithSourceFmt),
		                       pathSource.c_str(), gameRoot.c_str());
	} else {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::GamePathNotFound));
	}

	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::ProxyModeHint));
	ConfigIntegrityBypass::BeginWatching();
	if (ConfigIntegrityBypass::IsBypassDisabled()) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::BypassDisabled));
	} else {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::BypassWillApply));
	}

	if (!PrepareGameEnvironment()) {
		StatusDialog::AppendLog(LauncherI18n::T(LauncherI18n::Str::PrepIncompleteWarn));
	}

	if (StatusDialog::IsExitRequested()) {
		return 0;
	}

	if (!ctx->autoMode) {
		StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::WaitingReady));
		StatusDialog::AppendLog(LauncherI18n::T(LauncherI18n::Str::LaunchFlowManual));
	} else {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::LaunchFlowAuto));
	}

	if (StatusDialog::IsExitRequested()) {
		return false;
	}

	ctx->success = RunLauncherFlow();

	if (ctx->success) {
		StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::Done));
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::ManagerReadyManual));
	} else {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::KeepReceivingLogs));
	}

	StatusDialog::MarkFinished(ctx->success);
	return ctx->success ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	LogServer::Start();

	char debugSession[64] = {};
	if (GetEnvironmentVariableA(MMOD_DEBUG_SESSION_ENV, debugSession,
	                            static_cast<DWORD>(sizeof(debugSession))) > 0 &&
	    debugSession[0]) {
		StatusDialog::AppendLogf(L"Debug harness session: %S", debugSession);
	}

	StatusDialog::SetLaunchGameHandler(LaunchGameExecutable);
	StatusDialog::SetCloseGameHandler(CloseGameExecutable);

	StatusDialog::Create();

	LauncherContext ctx = {IsAutoMode(), false};
	HANDLE worker = CreateThread(nullptr, 0, LauncherWorker, &ctx, 0, nullptr);
	if (!worker) {
		StatusDialog::SetStep(LauncherI18n::T(LauncherI18n::Str::LaunchFailed));
		StatusDialog::AppendLog(LauncherI18n::T(LauncherI18n::Str::WorkerCreateFailed));
		StatusDialog::MarkFinished(false);
		const auto exitCode = StatusDialog::RunUntilUserCloses();
		LogServer::Stop();
		StatusDialog::Destroy();
		return exitCode;
	}

	const auto exitCode = StatusDialog::RunUntilUserCloses();
	if (StatusDialog::IsExitRequested()) {
		WaitForSingleObject(worker, 8000);
	} else {
		WaitForSingleObject(worker, INFINITE);
	}
	CloseHandle(worker);

	ConfigIntegrityBypass::StopWatching();
	LogServer::Stop();
	StatusDialog::Destroy();
	return exitCode;
}
