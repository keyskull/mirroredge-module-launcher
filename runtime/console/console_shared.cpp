#include "console_shared.h"

#include <Windows.h>

#include <cctype>
#include <cstring>

namespace ConsoleShared {

namespace {

const ManagerConsoleBridge *g_bridge = nullptr;

bool g_show = false;
bool g_focusInput = false;
bool g_autoScroll = true;
char g_input[512] = {};
std::deque<std::string> g_history;
int g_historyNav = -1;
std::string g_historyDraft;

constexpr size_t kMaxHistory = 64;

struct CollectLinesCtx {
	std::vector<std::string> *lines;
};

void CollectLine(const char *line, void *ctx) {
	auto *collect = static_cast<CollectLinesCtx *>(ctx);
	if (collect && collect->lines && line) {
		collect->lines->emplace_back(line);
	}
}

} // namespace

const ManagerConsoleBridge &Bridge() {
	static const ManagerConsoleBridge kEmpty = {};
	return g_bridge ? *g_bridge : kEmpty;
}

void SetBridge(const ManagerConsoleBridge *bridge, const PluginUiApi *ui) {
	g_bridge = bridge;
	PluginUi::Bind(ui);
}

bool IsShown() { return g_show; }

void SetShown(bool shown) { g_show = shown; }

bool &FocusInput() { return g_focusInput; }

bool &AutoScroll() { return g_autoScroll; }

char *InputBuffer() { return g_input; }

size_t InputBufferSize() { return sizeof(g_input); }

void PushHistory(const std::string &line) {
	if (line.empty()) {
		return;
	}
	if (!g_history.empty() && g_history.back() == line) {
		return;
	}
	g_history.push_back(line);
	while (g_history.size() > kMaxHistory) {
		g_history.pop_front();
	}
}

void ResetHistoryNav() { g_historyNav = -1; }

void ClearHistoryDraft() { g_historyDraft.clear(); }

std::deque<std::string> &History() { return g_history; }

int &HistoryNav() { return g_historyNav; }

std::string &HistoryDraft() { return g_historyDraft; }

void Print(const char *text) {
	const auto &bridge = Bridge();
	if (text && text[0] && bridge.writeLog) {
		bridge.writeLog(text);
	}
}

void PrintMultiline(const std::string &text) {
	size_t start = 0;
	while (start < text.size()) {
		const size_t end = text.find('\n', start);
		if (end == std::string::npos) {
			Print(text.substr(start).c_str());
			break;
		}
		if (end > start) {
			Print(text.substr(start, end - start).c_str());
		}
		start = end + 1;
	}
}

void GetLogLines(std::vector<std::string> &lines) {
	lines.clear();
	const auto &bridge = Bridge();
	if (!bridge.forEachLogLine) {
		return;
	}
	CollectLinesCtx ctx = {&lines};
	bridge.forEachLogLine(CollectLine, &ctx);
}

void TrimInPlace(std::string &text) {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
		text.erase(text.begin());
	}
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
		text.pop_back();
	}
}

std::vector<std::string> Tokenize(const char *line) {
	std::vector<std::string> tokens;
	std::string current;
	for (const char *p = line; *p; ++p) {
		if (std::isspace(static_cast<unsigned char>(*p))) {
			if (!current.empty()) {
				tokens.push_back(current);
				current.clear();
			}
			continue;
		}
		current.push_back(*p);
	}
	if (!current.empty()) {
		tokens.push_back(current);
	}
	return tokens;
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

} // namespace ConsoleShared
