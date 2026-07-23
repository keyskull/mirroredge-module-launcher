#include "engine_core_bridge.h"

extern "C" __declspec(dllexport) void __cdecl MMOD_EngineSetCoreBridge(
    const MmCoreBridge *bridge) {
	EngineCoreBridge::Set(bridge);
}
