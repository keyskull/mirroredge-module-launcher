#include "engine_core_bridge.h"

namespace EngineCoreBridge {

namespace {

const MmCoreBridge *g_bridge = nullptr;

} // namespace

void Set(const MmCoreBridge *bridge) { g_bridge = bridge; }

const MmCoreBridge *Get() { return g_bridge; }

void Log(const char *message) {
	if (g_bridge && g_bridge->logWrite && message) {
		g_bridge->logWrite(message);
	}
}

void PumpIpc() {
	if (g_bridge && g_bridge->ipcPump) {
		g_bridge->ipcPump();
	}
}

void PollMenu() {
	if (g_bridge && g_bridge->menuPoll) {
		g_bridge->menuPoll();
	}
}

} // namespace EngineCoreBridge
