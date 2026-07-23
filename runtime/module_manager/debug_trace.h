#pragma once

#include <cstdint>

namespace DebugTrace {

void Event(const char *location, const char *message, const char *hypothesisId,
           uintptr_t a = 0, uintptr_t b = 0, uintptr_t c = 0, int d = 0);

void SessionEvent(const char *location, const char *message,
                  const char *hypothesisId, uintptr_t a = 0, uintptr_t b = 0,
                  int d = 0);

} // namespace DebugTrace
