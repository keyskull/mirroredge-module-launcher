#include <Windows.h>

#include <deque>
#include <mutex>
#include <string>

#include "mod_log.h"
#include "debug_log.h"
#include "module_contract.h"
#include "session_file_log.h"

#include <stdarg.h>
#include <stdio.h>

namespace {

std::mutex g_writeMutex;
std::deque<std::string> g_lines;
HANDLE g_pipe = INVALID_HANDLE_VALUE;
constexpr size_t kMaxLogLines = 1000;
ULONGLONG g_sessionStartTick = 0;

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

std::string FormatLine(const wchar_t *moduleId, const char *message) {
    if (!g_sessionStartTick) {
        g_sessionStartTick = GetTickCount64();
    }

    const auto elapsedSec =
        (GetTickCount64() - g_sessionStartTick) / 1000ULL;

    char prefix[96] = {};
    if (moduleId && moduleId[0]) {
        snprintf(prefix, sizeof(prefix), "[%4llus][%ls] ",
                 static_cast<unsigned long long>(elapsedSec), moduleId);
    } else {
        snprintf(prefix, sizeof(prefix), "[%4llus][manager] ",
                 static_cast<unsigned long long>(elapsedSec));
    }

    std::string line = prefix;
    line += message ? message : "";
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    return line;
}

void MirrorToAgentDebug(const char *message) {
    if (!AgentDebugSessionActive() || !message || !message[0]) {
        return;
    }
    AgentDebugLog("mod_log", "module_manager", message, "mod_log");
}

void AppendLineLocked(const std::string &line) {
    if (line.empty()) {
        return;
    }

    g_lines.push_back(line);
    while (g_lines.size() > kMaxLogLines) {
        g_lines.pop_front();
    }

    MirrorToAgentDebug(line.c_str());
}

void WriteLineLocked(const wchar_t *moduleId, const char *message) {
    if (!message || !message[0]) {
        return;
    }

    const auto line = FormatLine(moduleId, message);
    AppendLineLocked(line);

    SessionFileLogWrite(line.c_str());

    if (!EnsureConnected()) {
        return;
    }

    std::string pipeLine = line;
    pipeLine.push_back('\n');

    DWORD bytesWritten = 0;
    if (!WriteFile(g_pipe, pipeLine.data(), static_cast<DWORD>(pipeLine.size()),
                   &bytesWritten, nullptr) ||
        bytesWritten != pipeLine.size()) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

} // namespace

namespace ModLog {

void Initialize() {
    std::lock_guard<std::mutex> lock(g_writeMutex);
    if (!g_sessionStartTick) {
        g_sessionStartTick = GetTickCount64();
    }
}

void Write(const char *message) {
    std::lock_guard<std::mutex> lock(g_writeMutex);
    WriteLineLocked(nullptr, message);
}

// Writef uses a 1024-char buffer; vsnprintf truncation is acceptable for log lines.
void Writef(const char *format, ...) {
    char buffer[1024] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Write(buffer);
}

void WriteFromModule(const wchar_t *moduleId, const char *message) {
    std::lock_guard<std::mutex> lock(g_writeMutex);
    WriteLineLocked(moduleId, message);
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_writeMutex);
    g_lines.clear();
}

void GetLines(std::vector<std::string> &lines) {
    std::lock_guard<std::mutex> lock(g_writeMutex);
    lines.assign(g_lines.begin(), g_lines.end());
}

} // namespace ModLog
