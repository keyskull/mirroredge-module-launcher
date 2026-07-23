#include "console_shared.h"

#include <Windows.h>

#include <cstring>
#include <string>

namespace {

const char *kCommands[] = {
    "clear", "echo", "help", "inject", "menu", "modules", "status", "unload",
};

void PrintHelp() {
	ConsoleShared::Print("Available commands:");
	ConsoleShared::Print("  help              - list commands");
	ConsoleShared::Print("  clear             - clear console output");
	ConsoleShared::Print("  echo <text>       - print text");
	ConsoleShared::Print("  status            - module_manager + engine status");
	ConsoleShared::Print("  modules           - list injectable modules");
	ConsoleShared::Print("  inject <id>       - queue module inject");
	ConsoleShared::Print("  unload <id>       - queue module unload");
	ConsoleShared::Print("  menu              - open Module Manager (Insert/F10)");
	ConsoleShared::Print("Toggle console with ` (grave). Escape closes console.");
}

std::string CompletePrefix(const std::string &prefix) {
	if (prefix.empty()) {
		return {};
	}

	std::string match;
	for (const char *cmd : kCommands) {
		if (_strnicmp(cmd, prefix.c_str(), prefix.size()) != 0) {
			continue;
		}
		if (!match.empty() && match != cmd) {
			return prefix;
		}
		match = cmd;
	}
	return match;
}

} // namespace

extern "C" __declspec(dllexport) void __cdecl MMOD_ConsoleInitialize() {
	ConsoleShared::Print(
	    "Type 'help' for console commands. Press ` to toggle console.");
}

void ConsoleShared::ExecuteCommand(const char *line) {
	std::string text = line ? line : "";
	ConsoleShared::TrimInPlace(text);
	if (text.empty()) {
		return;
	}

	ConsoleShared::PushHistory(text);
	ConsoleShared::ResetHistoryNav();
	ConsoleShared::ClearHistoryDraft();

	ConsoleShared::Print(("> " + text).c_str());

	const auto tokens = ConsoleShared::Tokenize(text.c_str());
	if (tokens.empty()) {
		return;
	}

	const auto &bridge = ConsoleShared::Bridge();
	const std::string &cmd = tokens[0];
	if (_stricmp(cmd.c_str(), "help") == 0) {
		PrintHelp();
		return;
	}
	if (_stricmp(cmd.c_str(), "clear") == 0) {
		if (bridge.clearLog) {
			bridge.clearLog();
		}
		return;
	}
	if (_stricmp(cmd.c_str(), "echo") == 0) {
		const auto space = text.find(' ');
		if (space == std::string::npos) {
			ConsoleShared::Print("");
			return;
		}
		ConsoleShared::Print(text.substr(space + 1).c_str());
		return;
	}
	if (_stricmp(cmd.c_str(), "status") == 0) {
		if (bridge.formatStatus) {
			char buffer[8192] = {};
			bridge.formatStatus(buffer, sizeof(buffer));
			ConsoleShared::PrintMultiline(buffer);
		}
		return;
	}
	if (_stricmp(cmd.c_str(), "modules") == 0) {
		if (bridge.formatModules) {
			char buffer[8192] = {};
			bridge.formatModules(buffer, sizeof(buffer));
			ConsoleShared::PrintMultiline(buffer);
		}
		return;
	}
	if (_stricmp(cmd.c_str(), "inject") == 0) {
		if (tokens.size() < 2) {
			ConsoleShared::Print("usage: inject <module_id>");
			return;
		}
		const auto moduleId = ConsoleShared::Utf8ToWide(tokens[1]);
		if (moduleId.empty()) {
			ConsoleShared::Print("inject: invalid module id");
			return;
		}
		if (bridge.requestLoad && bridge.requestLoad(moduleId.c_str(), "console")) {
			ConsoleShared::Print("queued inject");
		} else {
			ConsoleShared::Print("inject: module not found or busy");
		}
		return;
	}
	if (_stricmp(cmd.c_str(), "unload") == 0) {
		if (tokens.size() < 2) {
			ConsoleShared::Print("usage: unload <module_id>");
			return;
		}
		const auto moduleId = ConsoleShared::Utf8ToWide(tokens[1]);
		if (moduleId.empty()) {
			ConsoleShared::Print("unload: invalid module id");
			return;
		}
		if (bridge.requestUnload && bridge.requestUnload(moduleId.c_str())) {
			ConsoleShared::Print("queued unload");
		} else {
			ConsoleShared::Print("unload: module not found or not loaded");
		}
		return;
	}
	if (_stricmp(cmd.c_str(), "menu") == 0) {
		if (bridge.showMenu) {
			bridge.showMenu();
		}
		return;
	}

	ConsoleShared::Print(("Unknown command: " + cmd + ". Type 'help'.").c_str());
}

extern "C" __declspec(dllexport) void __cdecl MMOD_ConsoleExecuteCommand(const char *line) {
	ConsoleShared::ExecuteCommand(line);
}

int ConsoleInputCallback(PluginUiInputTextCallbackData *data) {
	if (data->eventFlag == ImGuiInputTextFlags_CallbackHistory) {
		auto &history = ConsoleShared::History();
		if (history.empty()) {
			return 0;
		}

		int &historyNav = ConsoleShared::HistoryNav();
		std::string &historyDraft = ConsoleShared::HistoryDraft();

		if (data->eventKey == ImGuiKey_UpArrow) {
			if (historyNav < 0) {
				historyDraft.assign(data->buf, data->bufTextLen);
				historyNav = static_cast<int>(history.size()) - 1;
			} else if (historyNav > 0) {
				--historyNav;
			}
		} else if (data->eventKey == ImGuiKey_DownArrow) {
			if (historyNav < 0) {
				return 0;
			}
			if (historyNav + 1 >= static_cast<int>(history.size())) {
				historyNav = -1;
				data->deleteChars(data, 0, data->bufTextLen);
				data->insertChars(data, 0, historyDraft.c_str());
				return 0;
			}
			++historyNav;
		} else {
			return 0;
		}

		const std::string &entry =
		    historyNav >= 0 ? history[static_cast<size_t>(historyNav)] : historyDraft;
		data->deleteChars(data, 0, data->bufTextLen);
		data->insertChars(data, 0, entry.c_str());
		return 0;
	}

	if (data->eventFlag == ImGuiInputTextFlags_CallbackCompletion) {
		const std::string current(data->buf, data->bufTextLen);
		const auto tokens = ConsoleShared::Tokenize(current.c_str());
		if (tokens.size() != 1) {
			return 0;
		}

		const auto completed = CompletePrefix(tokens[0]);
		if (completed.size() <= tokens[0].size()) {
			return 0;
		}

		data->deleteChars(data, 0, data->bufTextLen);
		data->insertChars(data, 0, completed.c_str());
		data->insertChars(data, static_cast<int>(completed.size()), " ");
		return 0;
	}

	return 0;
}
