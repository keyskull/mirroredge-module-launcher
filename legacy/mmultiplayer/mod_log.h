#pragma once

namespace ModLog {

void Initialize();
void Write(const char *message);
void Writef(const char *format, ...);

} // namespace ModLog
