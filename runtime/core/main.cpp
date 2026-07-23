#include <windows.h>
#include <atomic>

#include "core_bridge.h"
#include "engine_module_client.h"
#include "engine.h"
#include "engine_loader.h"
#include "loader.h"
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

#include "timing_constants.h"

static HMODULE g_selfModule = nullptr;
static std::atomic<bool> initWorkerStarted{false};
static std::atomic<bool> initComplete{false};

static bool EnsureEngineLoaded(HMODULE self) {
	g_selfModule = self;
	if (!EngineLoader::EnsureLoaded(self)) {
		return false;
	}
	return true;
}

static void CompleteModInitialization() {
	OutputDebugStringA("=== core: CompleteModInitialization ENTER ===\n");
	if (initComplete.load()) {
		OutputDebugStringA("=== core: CompleteModInitialization already complete ===\n");
		return;
	}
	if (!Engine::IsGameReadyForModInit()) {
		OutputDebugStringA("=== core: CompleteModInitialization game not ready ===\n");
		ModLog::Write("init: game not ready yet");
		MpDebugLog("main.cpp:CompleteModInitialization", "game_not_ready", "H4");
		return;
	}

	OutputDebugStringA("=== core: CompleteModInitialization game ready, starting init ===\n");
	ModLog::Write("init: starting mod initialization");
	MpDebugLog("main.cpp:CompleteModInitialization", "init_start", "H4");
	Engine::BeginInitialization();

	Settings::Load();

	OutputDebugStringA("=== core: CompleteModInitialization calling InitializeSDK ===\n");
	if (!Engine::InitializeSDK()) {
		OutputDebugStringA("=== core: CompleteModInitialization InitializeSDK FAIL ===\n");
		ModLog::Write("init: InitializeSDK failed");
		MpDebugLog("main.cpp:CompleteModInitialization", "sdk_fail", "H4");
		return;
	}
	OutputDebugStringA("=== core: CompleteModInitialization SDK OK, initing menu ===\n");

	if (!Menu::Initialize()) {
		OutputDebugStringA("=== core: CompleteModInitialization Menu FAIL ===\n");
		ModLog::Write("init: Menu::Initialize failed");
		MpDebugLog("main.cpp:CompleteModInitialization", "menu_fail", "H4");
		return;
	}
	OutputDebugStringA("=== core: CompleteModInitialization menu OK, starting IPC ===\n");

	ModIpc::Start();
	OutputDebugStringA("=== core: CompleteModInitialization ModIpc::Start returned ===\n");
	if (!ModIpc::WaitUntilListening(3000)) {
		OutputDebugStringA("=== core: CompleteModInitialization pipe not listening ===\n");
		ModLog::Write("init: control pipe not listening before ready signal");
	}
	OutputDebugStringA("=== core: CompleteModInitialization marking ready ===\n");
	Engine::MarkReady();
	initComplete = true;
	Loader::ApplyAutoLoadModules();
	ModLog::Write("init: core ready");
	MpDebugLog("main.cpp:CompleteModInitialization", "init_ready", "H4", 1, 0, 0);
	OutputDebugStringA("=== core: CompleteModInitialization DONE ===\n");
}

static DWORD WINAPI InitWorkerStandalone(LPVOID) {
	g_modIpcPumpThreadId = GetCurrentThreadId();
	ModLog::Initialize();
	ModLog::Write("inject: core loaded (standalone)");

	Engine::SetDeferredInitCallback(CompleteModInitialization);

	Sleep(Timing::kStandaloneInitWaitMs);

	while (!GetModuleHandle(L"d3d9.dll")) {
		Sleep(Timing::kD3D9PollIntervalMs);
	}

	ModLog::Write("inject: d3d9.dll loaded");
	ModLog::Write("inject: installing message bootstrap");
	Engine::InstallPeekMessageBootstrap();
	Engine::InstallRendererCapture();

	for (auto i = 0; i < Timing::kGameReadyMaxPolls && !Engine::IsGameReadyForModInit(); ++i) {
		Sleep(Timing::kD3D9PollIntervalMs);
	}

	Sleep(Timing::kPostBootstrapSettleMs);
	Engine::QueueMainThreadTask(CompleteModInitialization);

	for (auto i = 0; i < Timing::kInitCompleteMaxPolls && !initComplete; ++i) {
		ModIpc::ServicePump(Timing::kD3D9PollIntervalMs);
	}

	return initComplete ? 0 : 1;
}

static DWORD WINAPI InitWorkerHosted(LPVOID) {
#ifdef _DEBUG
	// Direct file trace — bypasses all logging that may fail
	{
		HANDLE h = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
		                      FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h != INVALID_HANDLE_VALUE) {
			const char *msg = "=== core: InitWorkerHosted ENTER ===\r\n";
			DWORD w;
			WriteFile(h, msg, (DWORD)strlen(msg), &w, nullptr);
			CloseHandle(h);
		}
	}
#endif
	OutputDebugStringA("=== core: InitWorkerHosted ENTER ===\n");
	g_modIpcPumpThreadId = GetCurrentThreadId();
	ModLog::Initialize();
	ModLog::Write("init: core hosted by module_manager");
	MpDebugLog("main.cpp:InitWorkerHosted", "worker_start", "H4",
	           GetCurrentThreadId(), ModHost::IsAttached() ? 1u : 0u);

	Engine::SetDeferredInitCallback(CompleteModInitialization);

	DWORD lastQueueTick = 0;
	const auto initDeadline = GetTickCount() + Timing::kInitDeadlineMs;
	DWORD loopCount = 0;
	BOOL firstReady = FALSE;
	while (GetTickCount() < initDeadline && !initComplete) {
		const DWORD now = GetTickCount();
		BOOL gameReady = Engine::IsGameReadyForModInit();
		if (!firstReady && gameReady) {
			firstReady = TRUE;
			OutputDebugStringA("=== core: InitWorkerHosted game is now ready ===\n");
#ifdef _DEBUG
			HANDLE h = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
			                      FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h != INVALID_HANDLE_VALUE) {
				const char *msg = "=== core: gameReady=1 ===\r\n";
				DWORD w;
				WriteFile(h, msg, (DWORD)strlen(msg), &w, nullptr);
				CloseHandle(h);
			}
#endif
		}
		if (gameReady && now - lastQueueTick >= Timing::kPipeRetryBackoffMs) {
			lastQueueTick = now;
			++loopCount;
			if (loopCount <= 3 || loopCount % 50 == 0) {
				char lmsg[128];
				snprintf(lmsg, sizeof(lmsg), "=== core: InitWorkerHosted queuing CompleteModInit #%lu ===\n", static_cast<unsigned long>(loopCount));
				OutputDebugStringA(lmsg);
			}
			if (const auto *host = ModHost::Get()) {
				if (host->QueueMainThreadTask) {
					host->QueueMainThreadTask(CompleteModInitialization);
				} else {
					CompleteModInitialization();
				}
			} else {
				CompleteModInitialization();
			}
		}
		ModIpc::ServicePump(Timing::kOneFrameMs);
	}

	OutputDebugStringA("=== core: InitWorkerHosted loop exit initComplete=");
	OutputDebugStringA(initComplete.load() ? "1 ===\n" : "0 ===\n");
	MpDebugLog("main.cpp:InitWorkerHosted", "game_ready_poll_done", "H4",
	           initComplete.load() ? 1u : 0u,
	           Engine::IsGameReadyForModInit() ? 1u : 0u);

	char endMsg[128];
	snprintf(endMsg, sizeof(endMsg), "=== core: InitWorkerHosted EXIT ret=%d ===\n", initComplete ? 0 : 1);
	OutputDebugStringA(endMsg);
	
#ifdef _DEBUG
	// Direct file trace at exit
	{
		HANDLE h = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
		                      FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h != INVALID_HANDLE_VALUE) {
			char buf[128];
			snprintf(buf, sizeof(buf), "=== core: InitWorkerHosted EXIT initComplete=%d gameReady=%d loopCount=%lu ===\r\n",
			         initComplete.load() ? 1 : 0,
			         Engine::IsGameReadyForModInit() ? 1 : 0,
			         static_cast<unsigned long>(loopCount));
			DWORD w;
			WriteFile(h, buf, (DWORD)strlen(buf), &w, nullptr);
			CloseHandle(h);
		}
	}
#endif
	
	return initComplete ? 0 : 1;
}

static bool StartInitWorker(bool hosted) {
#ifdef _DEBUG
	HANDLE h = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
	                      FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h != INVALID_HANDLE_VALUE) {
		char buf[128];
		snprintf(buf, sizeof(buf), "=== core: StartInitWorker ENTER hosted=%d alreadyStarted=%d ===\r\n",
		         hosted ? 1 : 0, initWorkerStarted.load() ? 1 : 0);
		DWORD w;
		WriteFile(h, buf, (DWORD)strlen(buf), &w, nullptr);
		CloseHandle(h);
	}
#endif

	if (initWorkerStarted.exchange(true)) {
#ifdef _DEBUG
		HANDLE h2 = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
		                       FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h2 != INVALID_HANDLE_VALUE) {
			const char *msg = "=== core: StartInitWorker ALREADY STARTED ===\r\n";
			DWORD w;
			WriteFile(h2, msg, (DWORD)strlen(msg), &w, nullptr);
			CloseHandle(h2);
		}
#endif
		return true;
	}

	const auto thread = CreateThread(
	    nullptr, 0, hosted ? InitWorkerHosted : InitWorkerStandalone, nullptr, 0,
	    nullptr);
	if (!thread) {
#ifdef _DEBUG
		HANDLE h3 = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
		                       FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h3 != INVALID_HANDLE_VALUE) {
			char buf2[128];
			snprintf(buf2, sizeof(buf2), "=== core: StartInitWorker CreateThread FAILED err=%lu ===\r\n", GetLastError());
			DWORD w;
			WriteFile(h3, buf2, (DWORD)strlen(buf2), &w, nullptr);
			CloseHandle(h3);
		}
#endif
		initWorkerStarted = false;
		return false;
	}

#ifdef _DEBUG
	HANDLE h4 = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
	                       FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h4 != INVALID_HANDLE_VALUE) {
		char buf3[128];
		snprintf(buf3, sizeof(buf3), "=== core: StartInitWorker thread created id=%lu ===\r\n",
		         GetThreadId(thread));
		DWORD w;
		WriteFile(h4, buf3, (DWORD)strlen(buf3), &w, nullptr);
		CloseHandle(h4);
	}
#endif

	CloseHandle(thread);
	return true;
}

extern "C" __declspec(dllexport) bool MMOD_PluginInitialize(const ModHostApi *host,
                                                            HMODULE self) {
#ifdef _DEBUG
	{
		HANDLE h = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
		                      FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h != INVALID_HANDLE_VALUE) {
			const char *msg = "=== core: MMOD_PluginInitialize ENTER ===\r\n";
			DWORD w;
			WriteFile(h, msg, (DWORD)strlen(msg), &w, nullptr);
			CloseHandle(h);
		}
	}
#endif
	OutputDebugStringA("=== core: MMOD_PluginInitialize ENTER ===\n");
	MpDebugLog("main.cpp:MMOD_PluginInitialize", "plugin_init_enter", "H3",
	           reinterpret_cast<uintptr_t>(host),
	           host ? host->version : 0u,
	           host && host->version >= MMOD_HOST_API_VERSION ? 1 : 0);
	if (!host || host->version < MMOD_HOST_API_VERSION || !host->ui) {
#ifdef _DEBUG
		{
			HANDLE h = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
			                      FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h != INVALID_HANDLE_VALUE) {
				const char *msg = "=== core: MMOD_PluginInitialize FAIL invalid host ===\r\n";
				DWORD w;
				WriteFile(h, msg, (DWORD)strlen(msg), &w, nullptr);
				CloseHandle(h);
			}
		}
#endif
		OutputDebugStringA("=== core: MMOD_PluginInitialize FAIL invalid host ===\n");
		return false;
	}

	if (!EnsureEngineLoaded(self)) {
#ifdef _DEBUG
		{
			HANDLE h = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
			                      FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h != INVALID_HANDLE_VALUE) {
				const char *msg = "=== core: MMOD_PluginInitialize FAIL EnsureEngineLoaded ===\r\n";
				DWORD w;
				WriteFile(h, msg, (DWORD)strlen(msg), &w, nullptr);
				CloseHandle(h);
			}
		}
#endif
		OutputDebugStringA("=== core: MMOD_PluginInitialize FAIL EnsureEngineLoaded ===\n");
		return false;
	}

	ModHost::Attach(host);
	PluginUi::Bind(host->ui);
	ModHost::SetSelfModule(self);
	CoreBridge::Install();
	EngineModuleClient::InstallBorderlessHost(host);
	Engine::SetHostedMode(true);
	bool ret = StartInitWorker(true);
#ifdef _DEBUG
	{
		HANDLE h = CreateFileW(L"C:\\Temp\\core_init_trace.txt", FILE_APPEND_DATA,
		                      FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h != INVALID_HANDLE_VALUE) {
			char buf[64];
			snprintf(buf, sizeof(buf), "=== core: MMOD_PluginInitialize EXIT ret=%d ===\r\n", ret ? 1 : 0);
			DWORD w;
			WriteFile(h, buf, (DWORD)strlen(buf), &w, nullptr);
			CloseHandle(h);
		}
	}
#endif
	OutputDebugStringA("=== core: MMOD_PluginInitialize ret=");
	if (!ret) {
		OutputDebugStringA("0 FAIL ===\n");
	} else {
		OutputDebugStringA("1 OK ===\n");
	}
	return ret;
}

extern "C" __declspec(dllexport) void MMOD_PluginShutdown(HMODULE self) {
	(void)self;
	Hook::ReleaseProcessThreadSuspensions();
	CoreBridge::Clear();
	ModLog::Write("init: core hosted shutdown");
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
	if (reason == DLL_PROCESS_ATTACH && GetModuleHandle(L"MirrorsEdge.exe")) {
		DisableThreadLibraryCalls(module);

		if (GetModuleHandle(L"module_manager.dll")) {
			g_selfModule = module;
			return TRUE;
		}

		if (!EnsureEngineLoaded(module)) {
			return FALSE;
		}
		CoreBridge::Install();
		StartInitWorker(false);
	}

	if (reason == DLL_PROCESS_DETACH && !reserved) {
		Hook::ReleaseProcessThreadSuspensions();
		CoreBridge::Clear();
	}

	return TRUE;
}
