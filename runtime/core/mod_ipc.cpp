#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

#include "engine.h"
#include "menu.h"
#include "mod_log.h"
#include "timing_constants.h"
#include "mod_ipc.h"
#include "module_contract.h"
#include "me_sdk/runtime/safe_test.h"
#include "settings.h"
#include "version.h"
#include "runtime_status_json.h"

namespace {

typedef int(__stdcall *MmHarnessFormatJsonFn)(char *out, int outChars);

void FormatSharedHarnessJson(std::string &out) {
	const HMODULE mgr = GetModuleHandleW(L"module_manager.dll");
	const auto formatJson = mgr ? reinterpret_cast<MmHarnessFormatJsonFn>(
	                                  GetProcAddress(mgr, "MmHarnessFormatJson"))
	                            : nullptr;
	if (!formatJson) {
		out.clear();
		return;
	}

	const int need = formatJson(nullptr, 0);
	if (need <= 1) {
		out = "{\"targets\":[]}";
		return;
	}

	std::vector<char> buffer(static_cast<size_t>(need), '\0');
	formatJson(buffer.data(), need);
	out.assign(buffer.data());
}

} // namespace

extern DWORD g_modIpcServerThreadId;
extern DWORD g_modIpcPumpThreadId;

namespace {

struct PendingCommand {
    std::string request;
    std::string *response = nullptr;
    HANDLE doneEvent = nullptr;
};

std::atomic<bool> serverRunning{false};
std::atomic<bool> pipeListening{false};
std::thread serverThread;
HANDLE pipeInstance = INVALID_HANDLE_VALUE;

std::mutex queueMutex;
std::queue<PendingCommand> commandQueue;
HANDLE g_pumpWakeEvent = nullptr;

static void EnsurePumpWake() {
    if (!g_pumpWakeEvent) {
        g_pumpWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_pumpWakeEvent) {
            OutputDebugStringA("core/mod_ipc: CreateEventW for pump wake failed\n");
        }
    }
}

static void SignalPumpWake() {
    EnsurePumpWake();
    SetEvent(g_pumpWakeEvent);
}

static void (*g_syncMainThreadTask)() = nullptr;
static HANDLE g_syncMainThreadDone = nullptr;

static void PumpSyncMainThreadTask() {
    if (g_syncMainThreadTask) {
        g_syncMainThreadTask();
    }
    if (g_syncMainThreadDone) {
        SetEvent(g_syncMainThreadDone);
    }
}

static bool RunSyncOnMainThread(void (*task)(), DWORD timeoutMs) {
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!done) {
        return false;
    }

    g_syncMainThreadTask = task;
    g_syncMainThreadDone = done;
    Engine::QueueMainThreadTask(PumpSyncMainThreadTask);

    const bool completed = WaitForSingleObject(done, timeoutMs) == WAIT_OBJECT_0;
    CloseHandle(done);
    g_syncMainThreadTask = nullptr;
    g_syncMainThreadDone = nullptr;
    return completed;
}

std::string Trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                              value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

std::wstring Utf8ToWide(const std::string &text) {
    if (text.empty()) {
        return {};
    }

    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 0) {
        return {};
    }

    std::vector<wchar_t> buffer(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buffer.data(), needed);
    if (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return std::wstring(buffer.begin(), buffer.end());
}

std::string WideToUtf8(const wchar_t *wide) {
    if (!wide || !wide[0]) {
        return {};
    }

    const auto size =
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], size, nullptr, nullptr);
    return result;
}

std::string ToLowerAscii(std::string value) {
    for (auto &ch : value) {
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string JsonEscape(const std::string &value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const auto ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::string GetCurrentMapName() {
    if (!Engine::IsModReady()) {
        return {};
    }

    if (auto *world = Engine::GetWorld(false)) {
        return ToLowerAscii(WideToUtf8(world->GetMapName(false).c_str()));
    }

    return {};
}

bool IsInGameplay() {
    if (!Engine::AreGameplayHooksInstalled()) {
        return false;
    }

    const auto controller = Engine::GetPlayerController(false);
    const auto pawn = Engine::GetPlayerPawn(false);
    return controller != nullptr && pawn != nullptr;
}

bool WritePipeLine(HANDLE pipe, const std::string &line) {
    std::string payload = line;
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }

    DWORD written = 0;
    return WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()),
                     &written, nullptr) != FALSE;
}

bool ReadPipeLine(HANDLE pipe, std::string &line) {
    line.clear();
    char ch = 0;
    DWORD read = 0;

    while (ReadFile(pipe, &ch, 1, &read, nullptr) && read == 1) {
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            line.push_back(ch);
        }
    }

    return !line.empty() || read == 1;
}

typedef int(__stdcall *MmMultiplayer_GetHarnessStatusFn)(
    int *connected, int *remotePlayers, float *posX, float *posY, float *posZ,
    unsigned short *yaw, int *inGameplay, wchar_t *mapOut, int mapOutChars);

struct MultiplayerHarnessSnapshot {
    bool connected = false;
    int remotePlayers = 0;
    int spawnedPlayers = 0;
    int posedPlayers = 0;
    float posX = 0.f;
    float posY = 0.f;
    float posZ = 0.f;
    unsigned short yaw = 0;
    bool inGameplay = false;
    std::string map;
};

// SEH-safe wrapper for MmMultiplayer_GetHarnessStatus.
// Called from a separate function so __try/__except doesn't conflict
// with C++ object unwinding in the caller (MultiplayerHarnessSnapshot
// contains std::string with a destructor).
static bool CallGetHarnessStatusSafe(
    MmMultiplayer_GetHarnessStatusFn fn,
    int *connectedFlag, int *remote,
    float *posX, float *posY, float *posZ, unsigned short *yaw,
    int *inGameplay, wchar_t *mapBuf, int mapBufChars)
{
    __try {
        return fn(connectedFlag, remote, posX, posY, posZ, yaw,
                  inGameplay, mapBuf, mapBufChars) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("TryReadMultiplayerHarness: SEH caught crash in MmMultiplayer_GetHarnessStatus\n");
        return false;
    }
}

static int CallGetSpawnedPlayersSafe(int(__stdcall *fn)())
{
    __try {
        return fn();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("TryReadMultiplayerHarness: SEH caught crash in MmMultiplayer_GetSpawnedPlayers\n");
        return 0;
    }
}

bool TryReadMultiplayerHarness(MultiplayerHarnessSnapshot &snap) {
    const HMODULE mod = GetModuleHandleW(L"multiplayer.dll");
    if (!mod) {
        return false;
    }

    const auto fn = reinterpret_cast<MmMultiplayer_GetHarnessStatusFn>(
        GetProcAddress(mod, "MmMultiplayer_GetHarnessStatus"));
    if (!fn) {
        return false;
    }

    int connectedFlag = 0;
    int remote = 0;
    int inGameplay = 0;
    wchar_t mapBuf[128] = {};
    if (!CallGetHarnessStatusSafe(fn, &connectedFlag, &remote,
            &snap.posX, &snap.posY, &snap.posZ, &snap.yaw,
            &inGameplay, mapBuf,
            static_cast<int>(sizeof(mapBuf) / sizeof(mapBuf[0])))) {
        return false;
    }

    snap.connected = connectedFlag != 0;
    snap.remotePlayers = remote;
    snap.inGameplay = inGameplay != 0;
    snap.map = ToLowerAscii(WideToUtf8(mapBuf));

    // Try to read spawnedPlayers from the new export; fall back to 0 if
    // the export doesn't exist (old multiplayer.dll build).
    const auto spawnedFn = reinterpret_cast<int(__stdcall *)()>(
        GetProcAddress(mod, "MmMultiplayer_GetSpawnedPlayers"));
    if (spawnedFn) {
        snap.spawnedPlayers = CallGetSpawnedPlayersSafe(spawnedFn);
    }

    const auto posedFn = reinterpret_cast<int(__stdcall *)()>(
        GetProcAddress(mod, "MmMultiplayer_GetPosedPlayers"));
    if (posedFn) {
        snap.posedPlayers = CallGetSpawnedPlayersSafe(posedFn);
    }

    return true;
}

void AppendMultiplayerHarnessJson(std::string &out) {
    MultiplayerHarnessSnapshot snap = {};

    out += ",\"multiplayer\":{";
    if (!TryReadMultiplayerHarness(snap)) {
        out += "\"connected\":false,\"remotePlayers\":0,\"spawnedPlayers\":0,"
               "\"posedPlayers\":0,"
               "\"posX\":0,\"posY\":0,\"posZ\":0,"
               "\"yaw\":0,\"inGameplay\":false,\"clientMap\":\"\"}";
        return;
    }

    out += "\"connected\":";
    out += snap.connected ? "true" : "false";
    out += ",\"remotePlayers\":";
    out += std::to_string(snap.remotePlayers);
    out += ",\"spawnedPlayers\":";
    out += std::to_string(snap.spawnedPlayers);
    out += ",\"posedPlayers\":";
    out += std::to_string(snap.posedPlayers);
    out += ",\"posX\":";
    out += std::to_string(snap.posX);
    out += ",\"posY\":";
    out += std::to_string(snap.posY);
    out += ",\"posZ\":";
    out += std::to_string(snap.posZ);
    out += ",\"yaw\":";
    out += std::to_string(snap.yaw);
    out += ",\"inGameplay\":";
    out += snap.inGameplay ? "true" : "false";
    out += ",\"clientMap\":\"";
    out += JsonEscape(snap.map);
    out += "\"}";
}

std::string BuildCoreStatusJson(bool menuOpen) {
    std::string map;
    bool inGameplay = false;
    if (Engine::IsModReady()) {
        map = GetCurrentMapName();
        inGameplay = IsInGameplay();
    }

    MultiplayerHarnessSnapshot mpSnap = {};
    if (TryReadMultiplayerHarness(mpSnap)) {
        if (map.empty() && !mpSnap.map.empty()) {
            map = mpSnap.map;
        }
        if (!inGameplay && mpSnap.inGameplay) {
            inGameplay = true;
        }
    }

    std::string out = "{\"component\":\"core\"";
    out += ",\"version\":\"";
    out += MMOD_MOD_VERSION_STRING;
    out += "\"";
    out += ",\"hostedMode\":";
    out += Engine::IsHostedMode() ? "true" : "false";
    out += ",\"menuOpen\":";
    out += menuOpen ? "true" : "false";
    out += ",\"currentMap\":\"";
    out += JsonEscape(map);
    out += "\",\"gameHwnd\":";
    const auto hwnd = Engine::GetWindow();
    out += hwnd ? std::to_string(reinterpret_cast<uintptr_t>(hwnd)) : "0";
    out += ",\"inGameplay\":";
    out += inGameplay ? "true" : "false";
    AppendMultiplayerHarnessJson(out);
    out += ",\"enabledMods\":[]";
    AppendJsonEngineStatus(out);
    out += "}";
    return out;
}

std::string BuildHarnessStatusJson() {
    MultiplayerHarnessSnapshot mpSnap = {};
    const bool mpOk = TryReadMultiplayerHarness(mpSnap);
    const bool inGameplay = (Engine::AreGameplayHooksInstalled() && mpOk && mpSnap.inGameplay);

    std::string out = "{\"component\":\"core\"";
    out += ",\"version\":\"";
    out += MMOD_MOD_VERSION_STRING;
    out += "\"";
    out += ",\"hostedMode\":";
    out += Engine::IsHostedMode() ? "true" : "false";
    out += ",\"menuOpen\":false";
    out += ",\"currentMap\":\"";
    if (mpOk && !mpSnap.map.empty()) {
        out += JsonEscape(mpSnap.map);
    }
    out += "\"";
    out += ",\"gameHwnd\":";
    const auto hwnd = Engine::GetWindow();
    out += hwnd ? std::to_string(reinterpret_cast<uintptr_t>(hwnd)) : "0";
    out += ",\"inGameplay\":";
    out += inGameplay ? "true" : "false";
    AppendMultiplayerHarnessJson(out);
    out += ",\"enabledMods\":[]";
    AppendJsonEngineStatus(out);
    out += "}";
    return out;
}

std::string BuildStatusJsonLite() {
    return BuildCoreStatusJson(false);
}

std::string EnsureMultiplayerRuntimeHooks() {
    const HMODULE mod = GetModuleHandleW(L"multiplayer.dll");
    if (!mod) {
        return "ERR multiplayer not loaded";
    }
    using EnsureFn = int(__stdcall *)();
    const auto fn = reinterpret_cast<EnsureFn>(
        GetProcAddress(mod, "MmMultiplayer_EnsureRuntimeHooks"));
    if (!fn) {
        return "ERR export missing";
    }
    return fn() ? "OK" : "ERR hooks pending";
}

bool TryHandleCommandInline(const std::string &request, std::string &response) {
    const auto cmd = Trim(request);
    if (cmd == "PING") {
        response = "PONG";
        return true;
    }

    if (cmd == "GET_STATUS") {
        response = Engine::IsModReady() ? BuildHarnessStatusJson()
                                        : BuildCoreStatusJson(false);
        return true;
    }

    if (cmd == "LIST_MODS") {
        response = "ERR deprecated: inject feature mods from Modules tab";
        return true;
    }

    if (cmd.rfind("SET_MOD ", 0) == 0) {
        response = "ERR deprecated: inject feature mods from Modules tab";
        return true;
    }

    if (cmd == "GET_UI_TARGETS") {
        FormatSharedHarnessJson(response);
        return true;
    }

    return false;
}

std::string HandleCommandOnMainThread(const std::string &request) {
    const auto cmd = Trim(request);
    if (cmd == "PING") {
        return "PONG";
    }

    if (cmd == "GET_STATUS") {
        return BuildCoreStatusJson(Menu::IsOpen());
    }

    if (cmd == "GET_UI_TARGETS") {
        std::string json;
        FormatSharedHarnessJson(json);
        return json;
    }

    if (cmd == "LIST_MODS") {
        return "ERR deprecated: inject feature mods from Modules tab";
    }

    if (cmd.rfind("SET_MOD ", 0) == 0) {
        return "ERR deprecated: inject feature mods from Modules tab";
    }

    if (cmd.rfind("SET ", 0) == 0) {
        const auto dot = cmd.find('.', 4);
        const auto space = cmd.find(' ', 4);
        if (dot == std::string::npos || space == std::string::npos ||
            dot > space) {
            return "ERR bad SET";
        }

        const auto ns = cmd.substr(4, dot - 4);
        const auto key = cmd.substr(dot + 1, space - dot - 1);
        const auto value = cmd.substr(space + 1);

        try {
            if (value == "true" || value == "false") {
                Settings::SetSetting(ns.c_str(), key.c_str(),
                                     value == "true");
            } else if (value.find('.') != std::string::npos) {
                Settings::SetSetting(ns.c_str(), key.c_str(),
                                     std::stod(value));
            } else {
                Settings::SetSetting(ns.c_str(), key.c_str(),
                                     std::stoi(value));
            }
        } catch (...) {
            Settings::SetSetting(ns.c_str(), key.c_str(), json(value));
        }

        if (ns == "player" || ns == "menu") {
            Menu::RefreshSettings();
        }

        return "OK";
    }

    if (cmd == "RELOAD_SETTINGS") {
        Settings::Load();
        return "OK";
    }

    if (cmd == "ENSURE_MP_HOOKS") {
        return EnsureMultiplayerRuntimeHooks();
    }

    if (cmd == "ENSURE_GAMEPLAY_HOOKS") {
        return Engine::EnsureGameplayHooks() ? "OK" : "ERR gameplay hooks pending";
    }

    if (cmd.rfind("CONSOLE ", 0) == 0) {
        const auto commandUtf8 = cmd.substr(8);
        if (commandUtf8.empty()) {
            return "ERR bad CONSOLE";
        }
        const auto command = Utf8ToWide(commandUtf8);
        if (command.empty()) {
            return "ERR bad CONSOLE encoding";
        }
        if (!Engine::RunConsoleCommandNow(command.c_str())) {
            return "ERR console unavailable";
        }
        return "OK";
    }

    if (cmd.rfind("LOAD_MAP ", 0) == 0) {
        const auto mapUtf8 = cmd.substr(9);
        if (mapUtf8.empty()) {
            return "ERR bad LOAD_MAP";
        }
        const auto mapName = Utf8ToWide(mapUtf8);
        if (mapName.empty()) {
            return "ERR bad LOAD_MAP encoding";
        }
        if (!Engine::LoadLevel(mapName.c_str())) {
            return "ERR load map unavailable";
        }
        return "OK";
    }

    if (cmd.rfind("INJECT_KEY ", 0) == 0) {
        // Format: INJECT_KEY <vk_hex>       e.g. INJECT_KEY 0x0D for Enter
        //         INJECT_KEY <vk_hex> UP    e.g. INJECT_KEY 0x0D UP for key-up
        // "INJECT_KEY " is 11 chars; older substr(12) skipped the leading '0' of 0x..
        const auto payload = cmd.substr(11);
        bool isUp = (payload.find("UP") != std::string::npos);
        try {
            UINT vk = static_cast<UINT>(std::stoul(payload, nullptr, 0));
            if (isUp) {
                Engine::InjectKeyUp(vk);
            } else {
                Engine::InjectKeyDown(vk);
            }
            return "OK";
        } catch (...) {
            return "ERR bad INJECT_KEY";
        }
    }

    if (cmd.rfind("START_GAME ", 0) == 0) {
        const auto mapUtf8 = cmd.substr(11);
        if (mapUtf8.empty()) {
            return "ERR bad START_GAME";
        }
        const auto mapName = Utf8ToWide(mapUtf8);
        if (mapName.empty()) {
            return "ERR bad START_GAME encoding";
        }
        if (!Engine::StartGameFromMenu(mapName.c_str())) {
            return "ERR start game unavailable";
        }
        return "OK";
    }

    // Story -> New Game (tutorial). Do NOT call ENSURE_GAMEPLAY_HOOKS before
    // this; early gameplay/level-load hooks can freeze the Story transition.
    if (cmd == "START_NEW_GAME" || cmd.rfind("START_NEW_GAME ", 0) == 0) {
        bool playCutScene = false;
        if (cmd.size() > 14) {
            const auto arg = Trim(cmd.substr(14));
            playCutScene = (arg == "cutscene" || arg == "1" || arg == "true");
        }
        if (!Engine::StartNewGameFromMenu(playCutScene)) {
            return "ERR start new game unavailable";
        }
        return "OK";
    }

    if (cmd == "FORCE_HOSTED_LIVE") {
        // Workaround: Engine::OnPostLevelLoad hook never fires because
        // UE3 LoadMap unwinds the stack with a C++ exception.  This
        // directly triggers the multiplayer hosted activation chain.
        // (QueueActivateHostedGameplay -> RequestGameplayActivation ->
        //  CompleteMultiplayerHostedActivation in hosted mode fast path).
        auto *mod = GetModuleHandleW(L"multiplayer.dll");
        if (!mod) { return "ERR multiplayer not loaded"; }
        auto *forceFn = reinterpret_cast<void(*)()>(
            GetProcAddress(mod, "MmMultiplayer_ForcePostLevelInit"));
        if (!forceFn) { return "ERR no MmMultiplayer_ForcePostLevelInit"; }
        forceFn();
        return "OK";
    }

    if (cmd == "TEST_SAFE_WRAPPERS") {
        return MeSdk::Safe::Test::RunAllSafeWrapperTests();
    }

    if (cmd == "DRAIN_SPAWNS") {
        auto *eng = GetModuleHandleW(L"engine.dll");
        if (!eng) { return "ERR engine not loaded"; }
        auto *drainFn = reinterpret_cast<void(__cdecl *)()>(
            GetProcAddress(eng, "MMOD_EngineDrainSpawnQueue"));
        if (!drainFn) { return "ERR no drain fn"; }
        drainFn();
        return "OK";
    }

    return "ERR unknown";
}

void ProcessQueuedCommands() {
    std::vector<PendingCommand> batch;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!commandQueue.empty()) {
            batch.push_back(commandQueue.front());
            commandQueue.pop();
        }
    }

    for (auto &pending : batch) {
        std::string response = HandleCommandOnMainThread(pending.request);
        if (pending.response) {
            *pending.response = response;
        }
        if (pending.doneEvent) {
            SetEvent(pending.doneEvent);
        }
    }
}

static void RunQueuedIpcOnMainThread() { ProcessQueuedCommands(); }

static void EnqueuePipeCommand(const std::string &request, std::string &response,
                               HANDLE done) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        commandQueue.push({request, &response, done});
    }
    SignalPumpWake();
    if (Engine::IsHostedMode()) {
        Engine::QueueMainThreadTask(RunQueuedIpcOnMainThread);
    }
}

DWORD WINAPI ServerThreadProc(LPVOID) {
    g_modIpcServerThreadId = GetCurrentThreadId();

    HANDLE nextInstance = INVALID_HANDLE_VALUE;

    // Create the first pipe instance before entering the loop.
    const auto createInstance = [&]() -> bool {
        nextInstance = CreateNamedPipeW(
            MMOD_CONTROL_PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
        if (nextInstance == INVALID_HANDLE_VALUE) {
            ModLog::Writef("core: CreateNamedPipe failed err=%lu",
                           GetLastError());
            return false;
        }
        pipeListening.store(true);
        return true;
    };

    while (serverRunning.load()) {
        pipeListening.store(false);

        if (nextInstance == INVALID_HANDLE_VALUE) {
            if (!createInstance()) {
                Sleep(Timing::kPipeRetryBackoffMs);
                continue;
            }
        }

        pipeInstance = nextInstance;
        nextInstance = INVALID_HANDLE_VALUE;

        static ULONGLONG s_lastPipeReadyLog = 0;
        const ULONGLONG nowReady = GetTickCount64();
        if (nowReady - s_lastPipeReadyLog >= 5000) {
            s_lastPipeReadyLog = nowReady;
            ModLog::Write("core: control pipe ready");
        }

        if (!ConnectNamedPipe(pipeInstance, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipeInstance);
            pipeInstance = INVALID_HANDLE_VALUE;
            pipeListening.store(false);
            continue;
        }

        // Pre-create the next instance while this one is busy, so the next
        // client can connect immediately without racing the create/close gap.
        createInstance();

        static ULONGLONG s_lastPipeClientLog = 0;
        const ULONGLONG nowClient = GetTickCount64();
        if (nowClient - s_lastPipeClientLog >= 5000) {
            s_lastPipeClientLog = nowClient;
            ModLog::Write("core: control pipe client connected");
        }

        while (serverRunning.load()) {
            std::string request;
            if (!ReadPipeLine(pipeInstance, request)) {
                break;
            }

            std::string response;
            if (TryHandleCommandInline(request, response)) {
                if (!WritePipeLine(pipeInstance, response)) {
                    break;
                }
                continue;
            }

            HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!done) {
                if (!WritePipeLine(pipeInstance, "ERR internal (event)")) {
                    break;
                }
                continue;
            }
            EnqueuePipeCommand(request, response, done);

            if (WaitForSingleObject(done, Timing::kIpcCallTimeoutMs) != WAIT_OBJECT_0) {
                response = "ERR timeout";
            }
            CloseHandle(done);

            if (response.empty()) {
                response = "ERR timeout";
            }

            if (!WritePipeLine(pipeInstance, response)) {
                break;
            }
        }

        DisconnectNamedPipe(pipeInstance);
        CloseHandle(pipeInstance);
        pipeInstance = INVALID_HANDLE_VALUE;
        pipeListening.store(false);
    }

    if (nextInstance != INVALID_HANDLE_VALUE) {
        CloseHandle(nextInstance);
        nextInstance = INVALID_HANDLE_VALUE;
    }

    pipeListening.store(false);
    return 0;
}

} // namespace

namespace ModIpc {

void Start() {
    if (serverRunning.exchange(true)) {
        return;
    }

    EnsurePumpWake();
    serverThread = std::thread([]() { ServerThreadProc(nullptr); });
    ModLog::Write("core: control pipe starting");
}

void Stop() {
    if (!serverRunning.exchange(false)) {
        return;
    }

    if (pipeInstance != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipeInstance, nullptr);
    }

    if (serverThread.joinable()) {
        serverThread.join();
    }
}

void Pump() {
    if (Engine::IsHostedMode() && GetCurrentThreadId() == g_modIpcPumpThreadId) {
        return;
    }
    ProcessQueuedCommands();
}

void ServicePump(const DWORD timeoutMs) {
    EnsurePumpWake();
    if (g_pumpWakeEvent) {
        WaitForSingleObject(g_pumpWakeEvent, timeoutMs);
    }
    ProcessQueuedCommands();
}

bool IsListening() { return pipeListening.load(); }

bool WaitUntilListening(const DWORD timeoutMs) {
    const DWORD deadline = GetTickCount() + timeoutMs;
    while (GetTickCount() < deadline) {
        if (pipeListening.load()) {
            return true;
        }
        Sleep(Timing::kPumpThreadIntervalMs);
    }
    return pipeListening.load();
}

} // namespace ModIpc
