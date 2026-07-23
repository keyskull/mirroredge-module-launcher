#include "debug_trace.h"

#include "debug_log.h"

namespace DebugTrace {

void Event(const char *location, const char *message, const char *hypothesisId,
           uintptr_t a, uintptr_t b, uintptr_t c, int d) {
    AgentDebugLog("module_manager", location, message, hypothesisId, a, b, c, d);
}

void SessionEvent(const char *location, const char *message,
                  const char *hypothesisId, uintptr_t a, uintptr_t b, int d) {
    AgentDebugLog("module_manager", location, message, hypothesisId, a, b, 0, d);
}

} // namespace DebugTrace
