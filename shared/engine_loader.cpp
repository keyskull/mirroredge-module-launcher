#include "engine_loader.h"

#include "runtime_module_client.h"

namespace EngineLoader {

namespace {

HMODULE g_engineModule = nullptr;

constexpr RuntimeModuleClient::Binding kEngineBinding = {L"engine", L"engine.dll"};

} // namespace

bool EnsureLoaded(HMODULE coreModule) {
	return RuntimeModuleClient::EnsureLoaded(kEngineBinding, coreModule,
	                                         &g_engineModule) != nullptr;
}

HMODULE GetModule() { return g_engineModule; }

} // namespace EngineLoader
