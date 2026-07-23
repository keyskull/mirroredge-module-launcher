#pragma once

#include "manager_console_bridge.h"
#include "plugin_ui.h"

#include <deque>
#include <string>
#include <vector>

namespace ConsoleShared {

const ManagerConsoleBridge &Bridge();
void SetBridge(const ManagerConsoleBridge *bridge, const PluginUiApi *ui);

bool IsShown();
void SetShown(bool shown);
bool &FocusInput();
bool &AutoScroll();
char *InputBuffer();
size_t InputBufferSize();

void PushHistory(const std::string &line);
void ResetHistoryNav();
void ClearHistoryDraft();
std::deque<std::string> &History();
int &HistoryNav();
std::string &HistoryDraft();

void Print(const char *text);
void PrintMultiline(const std::string &text);
void GetLogLines(std::vector<std::string> &lines);

void ExecuteCommand(const char *line);

void TrimInPlace(std::string &text);
std::vector<std::string> Tokenize(const char *line);
std::wstring Utf8ToWide(const std::string &text);

} // namespace ConsoleShared

int ConsoleInputCallback(PluginUiInputTextCallbackData *data);
