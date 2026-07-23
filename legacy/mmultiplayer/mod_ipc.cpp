#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

#include "engine.h"
#include "menu.h"
#include "mod_ipc.h"
#include "module_contract.h"
#include "me_sdk/runtime/init.h"
#include "me_sdk/runtime/sdk_errors.h"
#include "settings.h"

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

namespace {

struct PendingCommand {
    std::string request;
    std::string *response = nullptr;
    HANDLE doneEvent = nullptr;
};

std::atomic<bool> serverRunning{false};
std::thread serverThread;
HANDLE pipeInstance = INVALID_HANDLE_VALUE;

std::mutex queueMutex;
std::queue<PendingCommand> commandQueue;

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

void AppendSdkStatusJson(std::string &out) {
    const auto err = MeSdk::GetLastSdkError();
    const auto &st = MeSdk::GetLastRuntimeStatus();
    out += ",\"sdkError\":";
    out += std::to_string(static_cast<uint32_t>(err));
    out += ",\"sdkErrorName\":\"";
    out += JsonEscape(MeSdk::SdkErrorName(err));
    out += "\"";
    out += ",\"sdkImageSize\":";
    out += std::to_string(st.imageSize);
    out += ",\"sdkGNamesCount\":";
    out += std::to_string(st.gnamesCount);
    out += ",\"sdkGObjectsCount\":";
    out += std::to_string(st.gobjectsCount);
}

typedef int(__stdcall *MmMultiplayer_GetHarnessStatusFn)(
    int *connected, int *remotePlayers, float *posX, float *posY, float *posZ,
    unsigned short *yaw, int *inGameplay, wchar_t *mapOut, int mapOutChars);

bool TryReadMultiplayerHarness(bool &connected, int &remotePlayers, float &posX,
                               float &posY, float &posZ, unsigned short &yaw) {
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
    if (!fn(&connectedFlag, &remote, &posX, &posY, &posZ, &yaw, &inGameplay,
            mapBuf, static_cast<int>(sizeof(mapBuf) / sizeof(mapBuf[0])))) {
        return false;
    }

    connected = connectedFlag != 0;
    remotePlayers = remote;
    return true;
}

void AppendMultiplayerHarnessJson(std::string &out) {
    bool connected = false;
    int remotePlayers = 0;
    float posX = 0.f;
    float posY = 0.f;
    float posZ = 0.f;
    unsigned short yaw = 0;

    out += ",\"mpConnected\":";
    if (TryReadMultiplayerHarness(connected, remotePlayers, posX, posY, posZ, yaw)) {
        out += connected ? "true" : "false";
        out += ",\"mpRemotePlayers\":";
        out += std::to_string(remotePlayers);
        out += ",\"mpPosX\":";
        out += std::to_string(posX);
        out += ",\"mpPosY\":";
        out += std::to_string(posY);
        out += ",\"mpPosZ\":";
        out += std::to_string(posZ);
        out += ",\"mpYaw\":";
        out += std::to_string(yaw);
        return;
    }

    out += "false";
    out += ",\"mpRemotePlayers\":0";
    out += ",\"mpPosX\":0";
    out += ",\"mpPosY\":0";
    out += ",\"mpPosZ\":0";
    out += ",\"mpYaw\":0";
}

std::string BuildStatusJsonLite() {
    std::string out = "{\"component\":\"mmultiplayer\"";
    out += ",\"modReady\":";
    out += Engine::IsModReady() ? "true" : "false";
    out += ",\"initializing\":";
    out += Engine::IsInitializing() ? "true" : "false";
    out += ",\"gameReady\":";
    out += Engine::IsGameReadyForModInit() ? "true" : "false";
    AppendSdkStatusJson(out);
    out += ",\"presentationHooks\":";
    out += Engine::ArePresentationHooksInstalled() ? "true" : "false";
    out += ",\"gameplayHooks\":";
    out += Engine::AreGameplayHooksInstalled() ? "true" : "false";
    out += ",\"proxyActive\":";
    out += Engine::IsModD3D9ProxyActive() ? "true" : "false";
    out += ",\"hostedMode\":";
    out += Engine::IsHostedMode() ? "true" : "false";
    out += ",\"menuOpen\":false";
    out += ",\"enabledModCount\":0";

    out += ",\"currentMap\":\"";
    if (Engine::IsModReady()) {
        out += JsonEscape(GetCurrentMapName());
    }
    out += "\",\"gameHwnd\":";
    const auto hwnd = Engine::GetWindow();
    out += hwnd ? std::to_string(reinterpret_cast<uintptr_t>(hwnd)) : "0";
    out += ",\"inGameplay\":";
    out += IsInGameplay() ? "true" : "false";
    AppendMultiplayerHarnessJson(out);
    out += ",\"enabledMods\":[]}";
    return out;
}

bool TryHandleCommandInline(const std::string &request, std::string &response) {
    const auto cmd = Trim(request);
    if (cmd == "PING") {
        response = "PONG";
        return true;
    }

    if (cmd == "GET_STATUS" && Engine::IsHostedMode()) {
        response = BuildStatusJsonLite();
        return true;
    }

    if (cmd == "LIST_MODS") {
        response = "ERR deprecated: inject feature mods from Modules tab";
        return true;
    }

    if (cmd == "RELOAD_SETTINGS" && Engine::IsHostedMode()) {
        Settings::Load();
        Menu::RefreshSettings();
        response = "OK";
        return true;
    }

    if (cmd == "GET_UI_TARGETS") {
        FormatSharedHarnessJson(response);
        return true;
    }

    if (cmd.rfind("SET_MOD ", 0) == 0) {
        response = "ERR deprecated: inject feature mods from Modules tab";
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
        std::string out = "{\"component\":\"mmultiplayer\"";
        out += ",\"modReady\":";
        out += Engine::IsModReady() ? "true" : "false";
        out += ",\"initializing\":";
        out += Engine::IsInitializing() ? "true" : "false";
        out += ",\"gameReady\":";
        out += Engine::IsGameReadyForModInit() ? "true" : "false";
        AppendSdkStatusJson(out);
        out += ",\"presentationHooks\":";
        out += Engine::ArePresentationHooksInstalled() ? "true" : "false";
        out += ",\"gameplayHooks\":";
        out += Engine::AreGameplayHooksInstalled() ? "true" : "false";
        out += ",\"proxyActive\":";
        out += Engine::IsModD3D9ProxyActive() ? "true" : "false";
        out += ",\"hostedMode\":";
        out += Engine::IsHostedMode() ? "true" : "false";
        out += ",\"menuOpen\":";
        out += Menu::IsOpen() ? "true" : "false";
        out += ",\"enabledModCount\":0";
        out += ",\"currentMap\":\"";
        if (Engine::IsModReady()) {
            out += JsonEscape(GetCurrentMapName());
        }
        out += "\",\"gameHwnd\":";
        const auto hwnd = Engine::GetWindow();
        out += hwnd ? std::to_string(reinterpret_cast<uintptr_t>(hwnd)) : "0";
        out += ",\"inGameplay\":";
        out += IsInGameplay() ? "true" : "false";

        AppendMultiplayerHarnessJson(out);
        out += ",\"enabledMods\":[]}";
        return out;
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

DWORD WINAPI ServerThreadProc(LPVOID) {
    g_modIpcServerThreadId = GetCurrentThreadId();
    while (serverRunning.load()) {
        pipeInstance = CreateNamedPipeW(
            MMOD_CONTROL_PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
            nullptr);
        if (pipeInstance == INVALID_HANDLE_VALUE) {
            Sleep(200);
            continue;
        }

        if (!ConnectNamedPipe(pipeInstance, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipeInstance);
            pipeInstance = INVALID_HANDLE_VALUE;
            continue;
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
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                commandQueue.push({request, &response, done});
            }

            if (Engine::IsHostedMode()) {
                Engine::QueueMainThreadTask(RunQueuedIpcOnMainThread);
            }

            WaitForSingleObject(done, 10000);
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
    }

    return 0;
}

} // namespace

namespace ModIpc {

void Start() {
    if (serverRunning.exchange(true)) {
        return;
    }

    serverThread = std::thread([]() { ServerThreadProc(nullptr); });
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
    if (!Engine::IsHostedMode()) {
        ProcessQueuedCommands();
        return;
    }

    Engine::QueueMainThreadTask(RunQueuedIpcOnMainThread);
}

} // namespace ModIpc
