#include "mod_log.h"

#include "debug_log.h"
#include "feature_plugin_host.h"
#include "module_contract.h"
#include "session_file_log.h"
#include "version.h"

#include <Windows.h>

#include <mutex>
#include <string>

#include <stdarg.h>
#include <stdio.h>

#ifndef MMOD_MOD_ID
#error feature_mod_log.cpp requires MMOD_MOD_ID from the plugin version.h
#endif

namespace {

std::mutex g_writeMutex;
HANDLE g_pipe = INVALID_HANDLE_VALUE;

bool EnsureConnected() {
	if (g_pipe != INVALID_HANDLE_VALUE) {
		return true;
	}

	const auto pipe = CreateFileW(MMOD_LOG_PIPE_NAME, GENERIC_WRITE, 0, nullptr,
	                              OPEN_EXISTING, 0, nullptr);
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
	AgentDebugLog("mod_log", MMOD_MOD_ID, message, "mod_log");
}

void WriteLineLocked(const char *message) {
	if (!message || !message[0]) {
		return;
	}

	MirrorToAgentDebug(message);

	SessionFileLogWrite(message);

	if (FeaturePluginHost::IsAttached()) {
		FeaturePluginHost::ForwardLog(message);
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

// Writef uses a 1024-char buffer; vsnprintf truncation is acceptable for log lines.
void Writef(const char *format, ...) {
	char buffer[1024] = {};
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	Write(buffer);
}

} // namespace ModLog
