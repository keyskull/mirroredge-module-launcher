#include "debug_trace.h"

#include "debug_log.h"
#include "engine_core_bridge.h"

#include <cstdarg>
#include <cstdio>

namespace EngineDebugTrace {

void Event(const char *location, const char *message, const char *hypothesisId,
           uintptr_t a, uintptr_t b, uintptr_t c, int d) {
	AgentDebugLog("engine", location, message, hypothesisId, a, b, c, d);
}

void Log(const char *message) {
	if (!message || !message[0]) {
		return;
	}
	EngineCoreBridge::Log(message);
	if (AgentDebugSessionActive()) {
		Event("engine", message, "LOG", 0);
	}
}

void Logf(const char *fmt, ...) {
	if (!fmt) {
		return;
	}

	char buffer[512] = {};
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	Log(buffer);
}

} // namespace EngineDebugTrace
