#include <windows.h>
#include <atomic>

#include "engine.h"
#include "menu.h"
#include "mod_log.h"
#include "mod_ipc.h"
#include "modhost.h"
#include "mod_host_api.h"
#include "plugin_ui.h"
#include "settings.h"

#include "agent_log.h"
#include "hook.h"

extern DWORD g_modIpcPumpThreadId;

static std::atomic<bool> initWorkerStarted{false};
static std::atomic<bool> initComplete{false};

static void CompleteModInitialization() {
	if (!Engine::IsGameReadyForModInit()) {
		ModLog::Write("init: game not ready yet");
		MpDebugLog("main.cpp:CompleteModInitialization", "game_not_ready", "H4");
		return;
	}

	ModLog::Write("init: starting mod initialization");
	MpDebugLog("main.cpp:CompleteModInitialization", "init_start", "H4");
	Engine::BeginInitialization();

	Settings::Load();

	if (!Engine::InitializeSDK()) {
		ModLog::Write("init: InitializeSDK failed");
		MpDebugLog("main.cpp:CompleteModInitialization", "sdk_fail", "H4");
		return;
	}

	if (!Menu::Initialize()) {
		ModLog::Write("init: Menu::Initialize failed");
		MpDebugLog("main.cpp:CompleteModInitialization", "menu_fail", "H4");
		return;
	}

	Engine::MarkReady();
	initComplete = true;
	ModLog::Write("init: mod ready");
	MpDebugLog("main.cpp:CompleteModInitialization", "init_ready", "H4", 1, 0, 0);
}

static DWORD WINAPI InitWorkerStandalone(LPVOID) {
	g_modIpcPumpThreadId = GetCurrentThreadId();
	ModLog::Initialize();
	ModLog::Write("inject: mmultiplayer loaded (standalone)");

	Engine::SetDeferredInitCallback(CompleteModInitialization);

	Sleep(8000);

	while (!GetModuleHandle(L"d3d9.dll")) {
		Sleep(100);
	}

	ModLog::Write("inject: d3d9.dll loaded");
	ModLog::Write("inject: installing message bootstrap");
	Engine::InstallPeekMessageBootstrap();
	Engine::InstallRendererCapture();

	for (auto i = 0; i < 300 && !Engine::IsGameReadyForModInit(); ++i) {
		Sleep(100);
	}

	Sleep(3000);
	Engine::QueueMainThreadTask(CompleteModInitialization);

	for (auto i = 0; i < 900 && !initComplete; ++i) {
		ModIpc::Pump();
		Sleep(100);
	}

	return initComplete ? 0 : 1;
}

static DWORD WINAPI InitWorkerHosted(LPVOID) {
	g_modIpcPumpThreadId = GetCurrentThreadId();
	ModLog::Initialize();
	ModLog::Write("init: mmultiplayer hosted by module_manager");
	MpDebugLog("main.cpp:InitWorkerHosted", "worker_start", "H4",
	           GetCurrentThreadId(), ModHost::IsAttached() ? 1u : 0u);

	Engine::SetDeferredInitCallback(CompleteModInitialization);

	for (auto i = 0; i < 300 && !Engine::IsGameReadyForModInit(); ++i) {
		Sleep(100);
	}

	MpDebugLog("main.cpp:InitWorkerHosted", "game_ready_poll_done", "H4",
	           Engine::IsGameReadyForModInit() ? 1u : 0u, 0);

	Sleep(3000);

	if (const auto *host = ModHost::Get()) {
		if (host->QueueMainThreadTask) {
			host->QueueMainThreadTask(CompleteModInitialization);
		} else {
			CompleteModInitialization();
		}
	} else {
		CompleteModInitialization();
	}

	for (auto i = 0; i < 900 && !initComplete; ++i) {
		ModIpc::Pump();
		Sleep(100);
	}

	while (true) {
		ModIpc::Pump();
		Sleep(33);
	}

	return initComplete ? 0 : 1;
}

static bool StartInitWorker(bool hosted) {
	if (initWorkerStarted.exchange(true)) {
		return true;
	}

	const auto thread = CreateThread(
	    nullptr, 0, hosted ? InitWorkerHosted : InitWorkerStandalone, nullptr, 0,
	    nullptr);
	if (!thread) {
		initWorkerStarted = false;
		return false;
	}

	CloseHandle(thread);
	return true;
}

extern "C" __declspec(dllexport) bool MMOD_PluginInitialize(const ModHostApi *host,
                                                            HMODULE self) {
	MpDebugLog("main.cpp:MMOD_PluginInitialize", "plugin_init_enter", "H3",
	           reinterpret_cast<uintptr_t>(host),
	           host ? host->version : 0u,
	           host && host->version >= MMOD_HOST_API_VERSION ? 1 : 0);
	if (!host || host->version < MMOD_HOST_API_VERSION || !host->ui) {
		return false;
	}

	ModHost::Attach(host);
	PluginUi::Bind(host->ui);
	ModHost::SetSelfModule(self);
	Engine::SetHostedMode(true);
	return StartInitWorker(true);
}

extern "C" __declspec(dllexport) void MMOD_PluginShutdown(HMODULE self) {
	(void)self;
	Hook::ReleaseProcessThreadSuspensions();
	ModLog::Write("init: mmultiplayer hosted shutdown");
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
	if (reason == DLL_PROCESS_ATTACH && GetModuleHandle(L"MirrorsEdge.exe")) {
		DisableThreadLibraryCalls(module);

		if (GetModuleHandle(L"module_manager.dll")) {
			return TRUE;
		}

		StartInitWorker(false);
	}

	if (reason == DLL_PROCESS_DETACH && !reserved) {
		Hook::ReleaseProcessThreadSuspensions();
	}

	return TRUE;
}
