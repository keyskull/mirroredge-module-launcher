#pragma once

#include <string>
#include <vector>

namespace ModLog {

void Initialize();
void Write(const char *message);
void Writef(const char *format, ...);
void WriteFromModule(const wchar_t *moduleId, const char *message);
void Clear();
void GetLines(std::vector<std::string> &lines);

} // namespace ModLog
