#include "stdafx.h"

#include "diagnostics_session.h"
#include "deploy_settings.h"
#include "module_contract.h"
#include "ui/status_dialog.h"

#include <ShlObj.h>
#include <Shlwapi.h>
#include <Windows.h>

#include <cstdio>
#include <fstream>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

namespace {

std::string WideToUtf8(const std::wstring &text) {
	if (text.empty()) {
		return {};
	}
	const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
	                                     static_cast<int>(text.size()), nullptr,
	                                     0, nullptr, nullptr);
	if (size <= 0) {
		return {};
	}
	std::string out(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                    out.empty() ? nullptr : &out[0], size, nullptr, nullptr);
	return out;
}

std::wstring Utf8ToWide(const std::string &text) {
	if (text.empty()) {
		return {};
	}
	const int size =
	    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                        nullptr, 0);
	if (size <= 0) {
		return {};
	}
	std::wstring out(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                    out.empty() ? nullptr : &out[0], size);
	return out;
}

void JsonEscape(const char *src, std::string &dst) {
	dst.clear();
	if (!src) {
		return;
	}
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(src); *p;
	     ++p) {
		const unsigned char c = *p;
		if (c == '"' || c == '\\') {
			dst.push_back('\\');
			dst.push_back(static_cast<char>(c));
		} else if (c < 0x20) {
			dst.push_back(' ');
		} else {
			dst.push_back(static_cast<char>(c));
		}
	}
}

std::string MakeSessionId() {
	SYSTEMTIME st = {};
	GetLocalTime(&st);
	char buf[64] = {};
	snprintf(buf, sizeof(buf), "%04u%02u%02u-%02u%02u%02u-%04X",
	         static_cast<unsigned int>(st.wYear), static_cast<unsigned int>(st.wMonth),
	         static_cast<unsigned int>(st.wDay), static_cast<unsigned int>(st.wHour),
	         static_cast<unsigned int>(st.wMinute), static_cast<unsigned int>(st.wSecond),
	         GetTickCount() & 0xFFFF);
	return buf;
}

bool EnvAlreadySet(const char *name) {
	char buf[4] = {};
	return GetEnvironmentVariableA(name, buf, static_cast<DWORD>(sizeof(buf))) > 0 &&
	       buf[0];
}

bool WriteManifest(const std::wstring &gameRoot, const std::string &sessionId,
                   const std::wstring &sessionLog, const std::wstring &ndjsonLog) {
	const auto sessionUtf8 = WideToUtf8(sessionLog);
	const auto ndjsonUtf8 = WideToUtf8(ndjsonLog);
	const auto gameRootUtf8 = WideToUtf8(gameRoot);

	std::string escSession;
	std::string escSessionLog;
	std::string escNdjson;
	std::string escGameRoot;
	JsonEscape(sessionId.c_str(), escSession);
	JsonEscape(sessionUtf8.c_str(), escSessionLog);
	JsonEscape(ndjsonUtf8.c_str(), escNdjson);
	JsonEscape(gameRootUtf8.c_str(), escGameRoot);

	const auto body = std::string("{\"sessionId\":\"") + escSession +
	                  "\",\"sessionLog\":\"" + escSessionLog +
	                  "\",\"ndjsonLog\":\"" + escNdjson + "\",\"gameRoot\":\"" +
	                  escGameRoot + "\",\"startedAt\":" +
	                  std::to_string(GetTickCount64()) + "}\n";

	auto writeFile = [&](const std::wstring &path) {
		wchar_t dir[MAX_PATH] = {};
		wcsncpy(dir, path.c_str(), MAX_PATH - 1);
		dir[MAX_PATH - 1] = L'\0';
		PathRemoveFileSpecW(dir);
		if (dir[0]) {
			SHCreateDirectoryExW(nullptr, dir, nullptr);
		}
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream) {
			return false;
		}
		stream.write(body.data(), static_cast<std::streamsize>(body.size()));
		return stream.good();
	};

	const auto gameManifest =
	    gameRoot + L"\\" + MMOD_LOGS_DIR_NAME + L"\\last-session.json";
	writeFile(gameManifest);

	wchar_t tempPath[MAX_PATH] = {};
	if (GetTempPathW(MAX_PATH, tempPath) > 0) {
		std::wstring tempManifest =
		    std::wstring(tempPath) + MMOD_DEBUG_DIR_NAME + L"\\last-session.json";
		writeFile(tempManifest);
	}

	return true;
}

} // namespace

namespace DiagnosticsSession {

bool IsEnabledForLaunch(const std::wstring &gameRoot) {
	if (EnvAlreadySet(MMOD_DEBUG_SESSION_ENV) || EnvAlreadySet(MMOD_DIAGNOSTICS_ENV) ||
	    EnvAlreadySet(MMOD_SESSION_LOG_ENV)) {
		return true;
	}

	bool enabled = false;
	DeploySettings::LoadDiagnosticsEnabled(enabled, gameRoot);
	return enabled;
}

bool PrepareForGameLaunch(const std::wstring &gameRoot) {
	if (!IsEnabledForLaunch(gameRoot)) {
		return false;
	}

	const bool externalDebugLog = EnvAlreadySet(MMOD_DEBUG_LOG_ENV);

	std::string sessionId;
    if (EnvAlreadySet(MMOD_DEBUG_SESSION_ENV)) {
        char buf[64] = {};
        GetEnvironmentVariableA(MMOD_DEBUG_SESSION_ENV, buf, sizeof(buf));
        sessionId = buf;
    } else {
        sessionId = MakeSessionId();
        SetEnvironmentVariableA(MMOD_DEBUG_SESSION_ENV, sessionId.c_str());
    }
    if (!EnvAlreadySet(MMOD_DIAGNOSTICS_ENV)) {
        SetEnvironmentVariableA(MMOD_DIAGNOSTICS_ENV, "1");
    }

    const auto gameRootUtf8 = WideToUtf8(gameRoot);
    if (!gameRootUtf8.empty()) {
        SetEnvironmentVariableA(MMOD_GAME_ROOT_ENV, gameRootUtf8.c_str());
    }

    const auto sessionWide = Utf8ToWide(sessionId);
    std::wstring sessionDir =
        gameRoot + L"\\" + MMOD_LOGS_DIR_NAME + L"\\" + sessionWide;
    SHCreateDirectoryExW(nullptr, sessionDir.c_str(), nullptr);

    const std::wstring sessionLog = sessionDir + L"\\session.log";
    const std::wstring ndjsonLog = sessionDir + L"\\session.ndjson";

    if (!EnvAlreadySet(MMOD_SESSION_LOG_ENV)) {
        const auto sessionLogUtf8 = WideToUtf8(sessionLog);
        SetEnvironmentVariableA(MMOD_SESSION_LOG_ENV, sessionLogUtf8.c_str());
    }
    if (!EnvAlreadySet(MMOD_DEBUG_LOG_ENV)) {
        const auto ndjsonUtf8 = WideToUtf8(ndjsonLog);
        SetEnvironmentVariableA(MMOD_DEBUG_LOG_ENV, ndjsonUtf8.c_str());
    }

    if (!externalDebugLog) {
        WriteManifest(gameRoot, sessionId, sessionLog, ndjsonLog);
    }

	StatusDialog::AppendLogf(
	    L"\u8bca\u65ad\u65e5\u5fd7\u5df2\u542f\u7528 (session %S)\u3002\u6587\u4ef6: %s",
	    sessionId.c_str(), sessionLog.c_str());
	StatusDialog::AppendLog(
	    L"  \u6536\u96c6\u5305: tools\\collect-diagnostics.ps1 -GameRoot <\u6e38\u620f\u76ee\u5f55>");

	return true;
}

} // namespace DiagnosticsSession
