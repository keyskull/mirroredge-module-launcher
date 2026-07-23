#include <Windows.h>

#include <mutex>
#include <string>

#include "mod_log.h"
#include "debug_log.h"
#include "module_contract.h"
#include "modhost.h"

#include <stdarg.h>
#include <stdio.h>

namespace {

std::mutex g_writeMutex;
HANDLE g_pipe = INVALID_HANDLE_VALUE;

bool EnsureConnected() {
    if (g_pipe != INVALID_HANDLE_VALUE) {
        return true;
    }

    const auto pipe = CreateFileW(MMOD_LOG_PIPE_NAME, GENERIC_WRITE, 0,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    g_pipe = pipe;
    return true;
}

void MirrorToAgentDebug(const char *message) {
    if (!AgentDebugSessionActive() || !message || !message[0]) {
        return;
    }
    AgentDebugLog("mod_log", "mmultiplayer", message, "mod_log");
}

void WriteLineLocked(const char *message) {
    if (!message || !message[0]) {
        return;
    }

    MirrorToAgentDebug(message);

    if (ModHost::IsAttached()) {
        ModHost::ForwardLog(message);
    }

    if (!EnsureConnected()) {
        return;
    }

    std::string line = message;
    if (line.back() != '\n') {
        line.push_back('\n');
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(g_pipe, line.data(), static_cast<DWORD>(line.size()),
                   &bytesWritten, nullptr) ||
        bytesWritten != line.size()) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

} // namespace

namespace ModLog {

void Initialize() {}

void Write(const char *message) {
    std::lock_guard<std::mutex> lock(g_writeMutex);
    WriteLineLocked(message);
}

void Writef(const char *format, ...) {
    char buffer[1024] = {};
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    Write(buffer);
}

} // namespace ModLog
