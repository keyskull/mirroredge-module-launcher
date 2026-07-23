#pragma once

// Durable text log for troubleshooting on any machine (not only harness).
// Enable via:
//   MMOD_DIAGNOSTICS=1
//   MMOD_DEBUG_SESSION=<id>   (harness / launcher auto)
//   MMOD_SESSION_LOG=<full path to session.log>
// Default path when session id is set:
//   <MMOD_GAME_ROOT>/logs/<session>/session.log
//   else %TEMP%\mirroredge-debug\<session>\session.log
//
// Companion files in the same directory:
//   session.ndjson  (when MMOD_DEBUG_LOG points here)
//   environment.json (written once per process tree)

#include "module_contract.h"

#include <ShlObj.h>
#include <Shlwapi.h>
#include <Windows.h>

#include <mutex>
#include <stdio.h>
#include <string.h>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

namespace SessionFileLogDetail {

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

inline bool EnvTruthy(const char *name) {
	char buf[8] = {};
	return GetEnvironmentVariableA(name, buf, static_cast<DWORD>(sizeof(buf))) > 0 &&
	       buf[0] == '1';
}

inline bool SessionLoggingEnabled() {
	static int cached = -1;
	if (cached >= 0) {
		return cached != 0;
	}

	char sessionBuf[4] = {};
	char logBuf[4] = {};
	if ((GetEnvironmentVariableA(MMOD_DEBUG_SESSION_ENV, sessionBuf,
	                            static_cast<DWORD>(sizeof(sessionBuf))) > 0 &&
	     sessionBuf[0]) ||
	    EnvTruthy(MMOD_DIAGNOSTICS_ENV) ||
	    (GetEnvironmentVariableA(MMOD_SESSION_LOG_ENV, logBuf,
	                            static_cast<DWORD>(sizeof(logBuf))) > 0 &&
	     logBuf[0])) {
		cached = 1;
	} else {
		cached = 0;
	}
	return cached != 0;
}

inline bool ResolveSessionId(char *sessionOut, size_t sessionOutSize) {
	if (!sessionOut || sessionOutSize == 0) {
		return false;
	}

	strncpy(sessionOut, "default", sessionOutSize - 1);
	sessionOut[sessionOutSize - 1] = '\0';

	char envSession[64] = {};
	if (GetEnvironmentVariableA(MMOD_DEBUG_SESSION_ENV, envSession,
	                            static_cast<DWORD>(sizeof(envSession))) > 0 &&
	    envSession[0]) {
		strncpy(sessionOut, envSession, sessionOutSize - 1);
		sessionOut[sessionOutSize - 1] = '\0';
		return true;
	}
	return sessionOut[0] != '\0';
}

inline bool ResolveLogPath(wchar_t *outPath, size_t outChars) {
	if (!outPath || outChars == 0) {
		return false;
	}

	wchar_t envLog[MAX_PATH] = {};
	char envLogUtf8[MAX_PATH] = {};
	if (GetEnvironmentVariableA(MMOD_SESSION_LOG_ENV, envLogUtf8,
	                            static_cast<DWORD>(sizeof(envLogUtf8))) > 0 &&
	    envLogUtf8[0]) {
		MultiByteToWideChar(CP_UTF8, 0, envLogUtf8, -1, envLog,
		                    static_cast<int>(_countof(envLog)));
		wcsncpy(outPath, envLog, outChars - 1);
		outPath[outChars - 1] = L'\0';
		return true;
	}

	char sessionId[64] = {};
	if (!ResolveSessionId(sessionId, sizeof(sessionId))) {
		return false;
	}

	wchar_t sessionWide[64] = {};
	MultiByteToWideChar(CP_UTF8, 0, sessionId, -1, sessionWide,
	                    static_cast<int>(_countof(sessionWide)));

	char gameRootUtf8[MAX_PATH] = {};
	if (GetEnvironmentVariableA(MMOD_GAME_ROOT_ENV, gameRootUtf8,
	                            static_cast<DWORD>(sizeof(gameRootUtf8))) > 0 &&
	    gameRootUtf8[0]) {
		wchar_t gameRoot[MAX_PATH] = {};
		MultiByteToWideChar(CP_UTF8, 0, gameRootUtf8, -1, gameRoot,
		                    static_cast<int>(_countof(gameRoot)));

		wchar_t dir[MAX_PATH] = {};
		_snwprintf(dir, MAX_PATH - 1, L"%s\\%s\\%s", gameRoot,
		           MMOD_LOGS_DIR_NAME, sessionWide);
		dir[MAX_PATH - 1] = L'\0';
		SHCreateDirectoryExW(nullptr, dir, nullptr);
		_snwprintf(outPath, outChars - 1, L"%s\\session.log", dir);
		outPath[outChars - 1] = L'\0';
		return true;
	}

	wchar_t tempPath[MAX_PATH] = {};
	if (GetTempPathW(MAX_PATH, tempPath) == 0) {
		return false;
	}

	wchar_t dir[MAX_PATH] = {};
	_snwprintf(dir, MAX_PATH - 1, L"%s%s\\%s", tempPath, MMOD_DEBUG_DIR_NAME,
	           sessionWide);
	dir[MAX_PATH - 1] = L'\0';
	SHCreateDirectoryExW(nullptr, dir, nullptr);
	_snwprintf(outPath, outChars - 1, L"%s\\session.log", dir);
	outPath[outChars - 1] = L'\0';
	return true;
}

inline void WriteEnvironmentSnapshotLocked(const wchar_t *logPath) {
	if (!logPath || !logPath[0]) {
		return;
	}

	wchar_t envPath[MAX_PATH] = {};
	wcsncpy(envPath, logPath, MAX_PATH - 1);
	envPath[MAX_PATH - 1] = L'\0';
	PathRemoveFileSpecW(envPath);
	PathAppendW(envPath, L"environment.json");

	if (PathFileExistsW(envPath)) {
		return;
	}

	char sessionId[64] = "default";
	ResolveSessionId(sessionId, sizeof(sessionId));

	char gameRoot[512] = {};
	GetEnvironmentVariableA(MMOD_GAME_ROOT_ENV, gameRoot, sizeof(gameRoot));

	char computer[64] = {};
	DWORD computerSize = static_cast<DWORD>(sizeof(computer));
	GetComputerNameA(computer, &computerSize);

	OSVERSIONINFOA osvi = {};
	osvi.dwOSVersionInfoSize = sizeof(osvi);
#pragma warning(push)
#pragma warning(disable : 4996)
	GetVersionExA(&osvi);
#pragma warning(pop)

	char modulePath[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, modulePath, MAX_PATH);

	char productVersion[32] = {};
	GetEnvironmentVariableA(MMOD_PRODUCT_VERSION_ENV, productVersion,
	                      static_cast<DWORD>(sizeof(productVersion)));

	char escSession[128] = {};
	char escGameRoot[768] = {};
	char escComputer[128] = {};
	char escModule[768] = {};
	char escProduct[64] = {};
	JsonEscape(sessionId, escSession, sizeof(escSession));
	JsonEscape(gameRoot, escGameRoot, sizeof(escGameRoot));
	JsonEscape(computer, escComputer, sizeof(escComputer));
	JsonEscape(modulePath, escModule, sizeof(escModule));
	JsonEscape(productVersion, escProduct, sizeof(escProduct));

	char body[2048] = {};
	snprintf(body, sizeof(body),
	         "{\"sessionId\":\"%s\",\"startedAt\":%llu,\"productVersion\":\"%s\","
	         "\"computer\":\"%s\",\"os\":{\"major\":%lu,\"minor\":%lu,\"build\":%lu},"
	         "\"gameRoot\":\"%s\",\"process\":\"%s\"}\n",
	         escSession, static_cast<unsigned long long>(GetTickCount64()),
	         escProduct, escComputer, osvi.dwMajorVersion, osvi.dwMinorVersion,
	         osvi.dwBuildNumber, escGameRoot, escModule);

	const auto file =
	    CreateFileW(envPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	DWORD written = 0;
	WriteFile(file, body, static_cast<DWORD>(strlen(body)), &written, nullptr);
	CloseHandle(file);
}

inline void AppendLine(const char *line) {
	if (!SessionLoggingEnabled() || !line || !line[0]) {
		return;
	}

	static std::mutex mutex;
	std::lock_guard<std::mutex> lock(mutex);

	wchar_t logPath[MAX_PATH] = {};
	if (!ResolveLogPath(logPath, _countof(logPath))) {
		return;
	}

	WriteEnvironmentSnapshotLocked(logPath);

	wchar_t dir[MAX_PATH] = {};
	wcsncpy(dir, logPath, MAX_PATH - 1);
	dir[MAX_PATH - 1] = L'\0';
	PathRemoveFileSpecW(dir);
	SHCreateDirectoryExW(nullptr, dir, nullptr);

	const auto file =
	    CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	std::string payload = line;
	if (payload.empty() || payload.back() != '\n') {
		payload.push_back('\n');
	}

	DWORD written = 0;
	WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()), &written,
	          nullptr);
	CloseHandle(file);
}

} // namespace SessionFileLogDetail

inline bool SessionFileLoggingEnabled() {
	return SessionFileLogDetail::SessionLoggingEnabled();
}

inline void SessionFileLogWrite(const char *line) {
	SessionFileLogDetail::AppendLine(line);
}
