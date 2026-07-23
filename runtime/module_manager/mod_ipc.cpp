#include "mod_ipc.h"

#include "menu.h"
#include "mod_console.h"
#include "mod_log.h"
#include "mod_registry.h"
#include "module_contract.h"
#include "presentation.h"
#include "presentation_internal.h"
#include "timing_constants.h"
#include "ui_harness.h"
#include "window_layout_settings.h"

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

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

std::string Trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                              value.back() == ' ')) {
        value.pop_back();
    }
    return value;
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

bool IsReadOnlyPipeCommand(const std::string &cmd) {
    // APPLY_WINDOW_LAYOUT is Win32-only (same as d3d9 proxy at CreateDevice); answer
    // on the pipe thread so harness does not block behind the pump queue / SetWindowPos.
    return cmd == "PING" || cmd == "GET_STATUS" || cmd == "GET_UI_TARGETS" ||
           cmd == "LIST_MODULES" || cmd == "APPLY_WINDOW_LAYOUT" ||
           cmd.rfind("GET_LOG", 0) == 0;
}

std::string HandleCommandOnMainThread(const std::string &request) {
    const auto cmd = Trim(request);
    if (cmd == "PING") {
        return "PONG";
    }

    if (cmd == "GET_STATUS") {
        std::string json;
        ModRegistry::FormatStatusJson(json);
        return json;
    }

    if (cmd == "GET_UI_TARGETS") {
        std::string json;
        HarnessUi::FormatJson(json);
        return json;
    }

    if (cmd == "APPLY_WINDOW_LAYOUT") {
        Presentation::RequestWindowLayoutApply();
        return "OK";
    }

    if (cmd == "LIST_MODULES") {
        std::string response;
        ModRegistry::FormatModuleList(response);
        response += "\nEND";
        return response;
    }

    if (cmd == "GET_LOG" || cmd.rfind("GET_LOG ", 0) == 0) {
        size_t maxLines = 64;
        if (cmd.size() > 8) {
            try {
                const auto n = std::stoul(cmd.substr(8));
                if (n > 0 && n <= 500) {
                    maxLines = static_cast<size_t>(n);
                }
            } catch (...) {
            }
        }

        std::vector<std::string> lines;
        ModLog::GetLines(lines);
        if (lines.size() > maxLines) {
            lines.erase(lines.begin(),
                        lines.end() - static_cast<ptrdiff_t>(maxLines));
        }

        std::string response;
        response.reserve(lines.size() * 80);
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) {
                response.push_back('\n');
            }
            response += lines[i];
        }
        response += "\nEND";
        return response;
    }

    if (cmd.rfind("INJECT ", 0) == 0) {
        const auto idUtf8 = cmd.substr(7);
        const auto id = Utf8ToWide(idUtf8);
        if (id.empty()) {
            return "ERR bad INJECT";
        }
        ModLog::Writef("mod_ipc: INJECT %s", idUtf8.c_str());
        std::string rejectReason;
        if (!ModRegistry::RequestLoad(id.c_str(), "ipc", &rejectReason)) {
            if (!rejectReason.empty()) {
                return std::string("ERR ") + rejectReason;
            }
            return "ERR inject rejected";
        }
        return "OK";
    }

    if (cmd.rfind("UNLOAD ", 0) == 0) {
        const auto idUtf8 = cmd.substr(7);
        const auto id = Utf8ToWide(idUtf8);
        if (id.empty()) {
            return "ERR bad UNLOAD";
        }
        ModLog::Writef("mod_ipc: UNLOAD %s", idUtf8.c_str());
        if (!ModRegistry::RequestUnload(id.c_str())) {
            return "ERR unload rejected";
        }
        return "OK";
    }

    if (cmd == "MENU_OPEN") {
        HostMenu::Show();
        return "OK";
    }

    if (cmd == "MENU_CLOSE") {
        HostMenu::Hide();
        return "OK";
    }

    if (cmd.rfind("MENU_TAB ", 0) == 0) {
        const auto tabName = cmd.substr(9);
        if (tabName.empty() || !HostMenu::SelectTab(tabName.c_str())) {
            return "ERR bad MENU_TAB";
        }
        if (!HostMenu::IsOpen()) {
            HostMenu::Show();
        }
        return "OK";
    }

    if (cmd == "CONSOLE_OPEN") {
        ModConsole::Show();
        return "OK";
    }

    if (cmd == "CONSOLE_CLOSE") {
        ModConsole::Hide();
        return "OK";
    }

    if (cmd.rfind("CONSOLE_EXEC ", 0) == 0) {
        const auto line = cmd.substr(13);
        if (line.empty()) {
            return "ERR bad CONSOLE_EXEC";
        }
        ModConsole::ExecuteCommand(line.c_str());
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

DWORD WINAPI ServerThreadProc(LPVOID) {
    while (serverRunning.load()) {
        pipeInstance = CreateNamedPipeW(
            MMOD_MANAGER_CONTROL_PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
            nullptr);
        if (pipeInstance == INVALID_HANDLE_VALUE) {
            Sleep(Timing::kPipeRetryBackoffMs);
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
            const auto cmd = Trim(request);
            if (IsReadOnlyPipeCommand(cmd)) {
                response = HandleCommandOnMainThread(cmd);
            } else {
                HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!done) {
                    response = "ERR internal (event)";
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    commandQueue.push({request, &response, done});
                }

                if (WaitForSingleObject(done, Timing::kIpcCallTimeoutMs) != WAIT_OBJECT_0) {
                    response = "ERR timeout";
                }
                CloseHandle(done);
            }

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
    ModLog::Write("module_manager: control pipe ready");
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

void Pump() { ProcessQueuedCommands(); }

} // namespace ModIpc
