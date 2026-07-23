#pragma once

#include <cstdint>

#include "debug_log.h"

// #region agent log
inline void MpDebugLog(const char *location, const char *message,
                       const char *hypothesisId, uintptr_t a = 0,
                       uintptr_t b = 0, int d = 0) {
    AgentDebugLog("core", location, message, hypothesisId, a, b, 0, d);
}
// #endregion
