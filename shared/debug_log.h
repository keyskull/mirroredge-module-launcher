#pragma once

// Unified NDJSON agent-debug log for in-process mods and d3d9 proxy.
// Configure via environment:
//   MMOD_DEBUG_SESSION  - session id (default: "default")
//   MMOD_DEBUG_LOG      - full path override for log file
// Default log: %TEMP%\mirroredge-debug\<session>.ndjson
// Manifest:    %TEMP%\mirroredge-debug\last-session.json

#include "module_contract.h"

#include <Windows.h>

#include <mutex>
#include <stdio.h>
#include <string.h>

#ifndef MMOD_DEBUG_SESSION_ENV
#define MMOD_DEBUG_SESSION_ENV "MMOD_DEBUG_SESSION"
#endif

#ifndef MMOD_DEBUG_LOG_ENV
#define MMOD_DEBUG_LOG_ENV "MMOD_DEBUG_LOG"
#endif

#ifndef MMOD_DEBUG_DIR_NAME
#define MMOD_DEBUG_DIR_NAME L"mirroredge-debug"
#endif

namespace AgentDebugLogDetail {

inline void JsonEscape(const char *src, char *dst, size_t dstSize) {
    if (!dst || dstSize == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t out = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(src);
         *p && out + 2 < dstSize; ++p) {
        const unsigned char c = *p;
        if (c == '"' || c == '\\') {
            if (out + 2 >= dstSize) {
                break;
            }
            dst[out++] = '\\';
            dst[out++] = static_cast<char>(c);
        } else if (c < 0x20) {
            if (out + 1 >= dstSize) {
                break;
            }
            dst[out++] = ' ';
        } else {
            dst[out++] = static_cast<char>(c);
        }
    }
    dst[out] = '\0';
}

inline void EnsureManifest(const wchar_t *logPath, const char *sessionId) {
    wchar_t tempPath[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) {
        return;
    }

    wchar_t manifestPath[MAX_PATH] = {};
    _snwprintf(manifestPath, MAX_PATH - 1, L"%s%s\\last-session.json",
               tempPath, MMOD_DEBUG_DIR_NAME);
    manifestPath[MAX_PATH - 1] = L'\0';

    char manifest[1024] = {};
    char escapedPath[768] = {};
    char pathUtf8[512] = {};
    if (logPath) {
        WideCharToMultiByte(CP_UTF8, 0, logPath, -1, pathUtf8,
                            static_cast<int>(sizeof(pathUtf8)), nullptr,
                            nullptr);
    }
    JsonEscape(pathUtf8, escapedPath, sizeof(escapedPath));

    char escapedSession[128] = {};
    JsonEscape(sessionId, escapedSession, sizeof(escapedSession));

    snprintf(manifest, sizeof(manifest),
             "{\"sessionId\":\"%s\",\"logPath\":\"%s\",\"startedAt\":%llu}\n",
             escapedSession, escapedPath,
             static_cast<unsigned long long>(GetTickCount64()));

    const auto file = CreateFileW(
        manifestPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, manifest, static_cast<DWORD>(strlen(manifest)), &written,
              nullptr);
    CloseHandle(file);
}

inline bool ResolveLogPath(wchar_t *outPath, size_t outChars,
                           char *sessionOut, size_t sessionOutSize) {
    if (!outPath || outChars == 0) {
        return false;
    }

    char sessionId[64] = "default";
    if (sessionOut && sessionOutSize > 0) {
        strncpy(sessionOut, sessionId, sessionOutSize - 1);
        sessionOut[sessionOutSize - 1] = '\0';
    }

    char envSession[64] = {};
    if (GetEnvironmentVariableA(MMOD_DEBUG_SESSION_ENV, envSession,
                                static_cast<DWORD>(sizeof(envSession))) > 0 &&
        envSession[0]) {
        strncpy(sessionId, envSession, sizeof(sessionId) - 1);
        sessionId[sizeof(sessionId) - 1] = '\0';
        if (sessionOut && sessionOutSize > 0) {
            strncpy(sessionOut, sessionId, sessionOutSize - 1);
            sessionOut[sessionOutSize - 1] = '\0';
        }
    }

    char envLogUtf8[MAX_PATH] = {};
    if (GetEnvironmentVariableA(MMOD_DEBUG_LOG_ENV, envLogUtf8,
                              static_cast<DWORD>(sizeof(envLogUtf8))) > 0 &&
        envLogUtf8[0]) {
        MultiByteToWideChar(CP_UTF8, 0, envLogUtf8, -1, outPath,
                            static_cast<int>(outChars));
        EnsureManifest(outPath, sessionId);
        return true;
    }

    wchar_t tempPath[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) {
        return false;
    }

    wchar_t dirPath[MAX_PATH] = {};
    _snwprintf(dirPath, MAX_PATH - 1, L"%s%s", tempPath, MMOD_DEBUG_DIR_NAME);
    dirPath[MAX_PATH - 1] = L'\0';
    CreateDirectoryW(dirPath, nullptr);

    wchar_t narrowSession[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, sessionId, -1, narrowSession,
                        static_cast<int>(_countof(narrowSession)));

    _snwprintf(outPath, outChars - 1, L"%s\\%s.ndjson", dirPath,
               narrowSession);
    outPath[outChars - 1] = L'\0';
    EnsureManifest(outPath, sessionId);
    return true;
}

inline void WriteLine(const char *line) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    wchar_t logPath[MAX_PATH] = {};
    if (!ResolveLogPath(logPath, _countof(logPath), nullptr, 0)) {
        return;
    }

    const auto file = CreateFileW(
        logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    WriteFile(file, "\n", 1, &written, nullptr);
    CloseHandle(file);
}

inline bool AgentDiagnosticsEnabled() {
    static int cached = -1;
    if (cached >= 0) {
        return cached != 0;
    }

    char buf[8] = {};
    if (GetEnvironmentVariableA(MMOD_DEBUG_SESSION_ENV, buf,
                                static_cast<DWORD>(sizeof(buf))) > 0 &&
        buf[0]) {
        cached = 1;
        return true;
    }

    if (GetEnvironmentVariableA(MMOD_DIAGNOSTICS_ENV, buf,
                                static_cast<DWORD>(sizeof(buf))) > 0 &&
        buf[0] == '1') {
        cached = 1;
        return true;
    }

    if (GetEnvironmentVariableA(MMOD_SESSION_LOG_ENV, buf,
                                static_cast<DWORD>(sizeof(buf))) > 0 &&
        buf[0]) {
        cached = 1;
        return true;
    }

    cached = 0;
    return false;
}

inline bool AgentDebugSessionActive() { return AgentDiagnosticsEnabled(); }

} // namespace AgentDebugLogDetail

inline bool AgentDebugSessionActive() {
    return AgentDebugLogDetail::AgentDebugSessionActive();
}

inline void AgentDebugLog(const char *component, const char *location,
                          const char *message, const char *hypothesisId,
                          uintptr_t a = 0, uintptr_t b = 0, uintptr_t c = 0,
                          int d = 0) {
    char sessionId[64] = "default";
    wchar_t logPath[MAX_PATH] = {};
    AgentDebugLogDetail::ResolveLogPath(logPath, _countof(logPath), sessionId,
                                        sizeof(sessionId));

    char escLocation[192] = {};
    char escMessage[192] = {};
    char escHypothesis[64] = {};
    char escComponent[64] = {};
    char escSession[64] = {};
    AgentDebugLogDetail::JsonEscape(location, escLocation, sizeof(escLocation));
    AgentDebugLogDetail::JsonEscape(message, escMessage, sizeof(escMessage));
    AgentDebugLogDetail::JsonEscape(hypothesisId, escHypothesis,
                                    sizeof(escHypothesis));
    AgentDebugLogDetail::JsonEscape(component, escComponent, sizeof(escComponent));
    AgentDebugLogDetail::JsonEscape(sessionId, escSession, sizeof(escSession));

    char line[768] = {};
    snprintf(line, sizeof(line),
             "{\"sessionId\":\"%s\",\"timestamp\":%llu,\"component\":\"%s\","
             "\"location\":\"%s\",\"message\":\"%s\",\"hypothesisId\":\"%s\","
             "\"data\":{\"a\":%u,\"b\":%u,\"c\":%u,\"d\":%d}}",
             escSession, static_cast<unsigned long long>(GetTickCount64()),
             escComponent, escLocation, escMessage, escHypothesis,
             static_cast<unsigned>(a), static_cast<unsigned>(b),
             static_cast<unsigned>(c), d);
    AgentDebugLogDetail::WriteLine(line);
}
