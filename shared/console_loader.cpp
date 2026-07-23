#include "console_loader.h"

#include "runtime_module_client.h"

namespace ConsoleLoader {

namespace {

HMODULE g_consoleModule = nullptr;

constexpr RuntimeModuleClient::Binding kConsoleBinding = {L"mm-console",
                                                          L"mm-console.dll"};

} // namespace

bool EnsureLoaded(HMODULE hostModule) {
	return RuntimeModuleClient::EnsureLoaded(kConsoleBinding, hostModule,
	                                         &g_consoleModule) != nullptr;
}

HMODULE GetModule() { return g_consoleModule; }

} // namespace ConsoleLoader
