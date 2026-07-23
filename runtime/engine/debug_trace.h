#pragma once

#include <cstdint>

namespace EngineDebugTrace {

void Event(const char *location, const char *message, const char *hypothesisId,
           uintptr_t a = 0, uintptr_t b = 0, uintptr_t c = 0, int d = 0);

void Log(const char *message);
void Logf(const char *fmt, ...);

} // namespace EngineDebugTrace
