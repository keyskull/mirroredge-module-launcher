#include "stdafx.h"

#include "config.h"
#include "game_launch.h"
#include "game_path.h"
#include "injection_flow.h"
#include "log_server.h"
#include "config_integrity_bypass.h"
#include "product_version.h"
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

	StatusDialog::SetStep(L"\u51c6\u5907\u4e2d");
	StatusDialog::AppendLogf(L"Mirror's Edge Module Launcher v%S (%S)",
	                         MMOD_PRODUCT_VERSION_STRING,
	                         MMOD_PRODUCT_RELEASE_DATE);

	std::wstring gameRoot;
	std::wstring pathSource;
	if (GamePath::ResolveGameRoot(gameRoot, &pathSource)) {
		StatusDialog::AppendLogf(L"\u6e38\u620f\u8def\u5f84 (%s): %s",
		                       pathSource.c_str(), gameRoot.c_str());
	} else {
		StatusDialog::AppendLog(
		    L"\u672a\u81ea\u52a8\u627e\u5230\u6e38\u620f\u8def\u5f84\u3002\u8bf7\u70b9\u51fb\u300c\u6d4f\u89c8\u300d\u624b\u52a8\u9009\u62e9\u3002");
	}

	StatusDialog::AppendLog(
	    L"d3d9 \u4ee3\u7406\u6a21\u5f0f\uff1amodule_manager \u7531\u4ee3\u7406\u52a0\u8f7d\uff0c\u65e0\u9700\u7ba1\u7406\u5458\u6743\u9650\u3002");
	ConfigIntegrityBypass::BeginWatching();
	if (ConfigIntegrityBypass::IsBypassDisabled()) {
		StatusDialog::AppendLog(
		    L"MMOD_DISABLE_CONFIG_BYPASS=1: \u914d\u7f6e\u5b8c\u6574\u6027\u7ed5\u8fc7\u5df2\u7981\u7528\u3002");
	} else {
		StatusDialog::AppendLog(
		    L"\u542f\u52a8\u6e38\u620f\u65f6\u5c06\u5e94\u7528\u5185\u7f6e\u914d\u7f6e\u5b8c\u6574\u6027\u7ed5\u8fc7\u3002");
	}

	if (!PrepareGameEnvironment()) {
		StatusDialog::AppendLog(
		    L"\u8b66\u544a\uff1a\u6e38\u620f\u6587\u4ef6\u51c6\u5907\u672a\u5b8c\u5168\u5b8c\u6210\u3002\u82e5\u542f\u52a8\u5931\u8d25\uff0c"
		    L"\u8bf7\u68c0\u67e5 Binaries \u76ee\u5f55\u4e2d\u662f\u5426\u5b58\u5728\u4e0e\u4ee3\u7406\u51b2\u7a81\u7684 DLL\u3002");
	}

	if (StatusDialog::IsExitRequested()) {
		return 0;
	}

	if (!ctx->autoMode) {
		StatusDialog::SetStep(L"\u7b49\u5f85\u5c31\u7eea");
		StatusDialog::AppendLog(
		    L"\u542f\u52a8\u6d41\u7a0b\uff1a\n"
		    L"1. \u70b9\u51fb\u300c\u542f\u52a8\u6e38\u620f\u300d\u6216\u81ea\u884c\u542f\u52a8 Mirror's Edge\n"
		    L"2. d3d9 \u4ee3\u7406\u52a0\u8f7d module_manager.dll\uff0c\u542f\u52a8\u5668\u7b49\u5f85\u5c31\u7eea\u4fe1\u53f7\n"
		    L"3. \u6e38\u620f\u5185\u6309 Insert/F10 \u6253\u5f00 Module Manager\uff0c\u5728 Modules \u6807\u7b7e\u6ce8\u5165 mmultiplayer");
	} else {
		StatusDialog::AppendLog(
		    L"\u81ea\u52a8\u6a21\u5f0f\uff1a\u7b49\u5f85 d3d9 \u4ee3\u7406\u52a0\u8f7d module_manager\u3002");
	}

	if (StatusDialog::IsExitRequested()) {
		return false;
	}

	ctx->success = RunLauncherFlow();

	if (ctx->success) {
		StatusDialog::SetStep(L"\u5b8c\u6210");
		StatusDialog::AppendLog(
		    L"module_manager \u5c31\u7eea\u3002\u5207\u56de\u6e38\u620f\u6309 Insert/F10\uff0c\u5728 Module Manager \u2192 Modules \u6ce8\u5165\u5176\u4ed6\u6a21\u7ec4\u3002");
	} else {
		StatusDialog::AppendLog(
		    L"\u542f\u52a8\u5668\u5c06\u7ee7\u7eed\u63a5\u6536\u6a21\u7ec4\u65e5\u5fd7\uff08\u82e5\u4ee3\u7406\u52a0\u8f7d\u6210\u529f\u4f46\u521d\u59cb\u5316\u8f83\u6162\uff09\u3002");
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
		StatusDialog::SetStep(L"\u542f\u52a8\u5931\u8d25");
		StatusDialog::AppendLog(L"\u65e0\u6cd5\u521b\u5efa\u5de5\u4f5c\u7ebf\u7a0b\u3002");
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
