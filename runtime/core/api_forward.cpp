#include "engine_loader.h"
#include "mmultiplayer_api.h"

#include <Windows.h>

extern "C" __declspec(dllexport) const MmultiplayerApi *MMOD_GetMmultiplayerApi() {
	static const MmultiplayerApi *cached = nullptr;
	if (cached) {
		return cached;
	}

	const HMODULE engine = EngineLoader::GetModule();
	if (!engine) {
		return nullptr;
	}

	const auto getApi = reinterpret_cast<MMOD_GetMmultiplayerApiFn>(
	    GetProcAddress(engine, MMULTIPLAYER_API_EXPORT));
	if (!getApi) {
		return nullptr;
	}

	cached = getApi();
	return cached;
}
