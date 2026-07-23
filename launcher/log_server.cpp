#include "stdafx.h"

#include "config.h"
#include "log_server.h"
#include "timing_constants.h"
#include "ui/status_dialog.h"

#include "session_file_log.h"

#include <atomic>
#include <string>
#include <thread>

namespace {

std::thread g_serverThread;
std::atomic<bool> g_stopRequested{false};

std::wstring Utf8ToWide(const std::string &text) {
	if (text.empty()) {
		return L"";
	}

	const auto length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
	                                        static_cast<int>(text.size()), nullptr,
	                                        0);
	if (length <= 0) {
		return std::wstring(text.begin(), text.end());
	}

	std::wstring wide(length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                    &wide[0], length);
	return wide;
}

void RelayModLine(const std::string &line) {
	if (line.empty()) {
		return;
	}

	const auto wide = Utf8ToWide(line);
	StatusDialog::AppendModLog(wide.c_str());

	if (SessionFileLoggingEnabled()) {
		SessionFileLogWrite(line.c_str());
	}
}

void HandleClient(HANDLE pipe) {
	std::string buffer;
	char chunk[512] = {};

	while (!g_stopRequested.load()) {
		DWORD bytesRead = 0;
		if (!ReadFile(pipe, chunk, sizeof(chunk), &bytesRead, nullptr) ||
		    bytesRead == 0) {
			break;
		}

		buffer.append(chunk, chunk + bytesRead);
		for (;;) {
			const auto newline = buffer.find('\n');
			if (newline == std::string::npos) {
				break;
			}

			auto line = buffer.substr(0, newline);
			buffer.erase(0, newline + 1);
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
				line.pop_back();
			}
			RelayModLine(line);
		}
	}

	if (!buffer.empty()) {
		while (!buffer.empty() && (buffer.back() == '\r' || buffer.back() == '\n')) {
			buffer.pop_back();
		}
		RelayModLine(buffer);
	}

	DisconnectNamedPipe(pipe);
	CloseHandle(pipe);
}

void ServerLoop() {
	const auto &config = LauncherConfig::Get();
	const auto pipeName = config.moduleLogPipeName.c_str();

	while (!g_stopRequested.load()) {
		const auto pipe = CreateNamedPipeW(
		    pipeName, PIPE_ACCESS_INBOUND,
		    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
		    4096, 4096, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE) {
			Sleep(LauncherTiming::kPipeCreateRetryMs);
			continue;
		}

		const auto connected =
		    ConnectNamedPipe(pipe, nullptr) ? TRUE
		                                    : (GetLastError() == ERROR_PIPE_CONNECTED);
		if (!connected) {
			CloseHandle(pipe);
			Sleep(LauncherTiming::kPipeConnectRetryMs);
			continue;
		}

		HandleClient(pipe);
	}
}

} // namespace

namespace LogServer {

void Start() {
	if (g_serverThread.joinable()) {
		return;
	}

	g_stopRequested = false;
	g_serverThread = std::thread(ServerLoop);
}

void Stop() {
	g_stopRequested = true;

	const auto &config = LauncherConfig::Get();
	HANDLE wakePipe =
	    CreateFileW(config.moduleLogPipeName.c_str(), GENERIC_WRITE, 0, nullptr,
	                OPEN_EXISTING, 0, nullptr);
	if (wakePipe != INVALID_HANDLE_VALUE) {
		CloseHandle(wakePipe);
	}

	if (g_serverThread.joinable()) {
		g_serverThread.join();
	}
}

} // namespace LogServer
