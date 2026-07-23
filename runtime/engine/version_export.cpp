#include "runtime_version.h"
#include "version.h"

extern "C" __declspec(dllexport) const MmodRuntimeVersion *MMOD_GetRuntimeVersion() {
	static const MmodRuntimeVersion version = {
	    MMOD_ENGINE_VERSION_MAJOR,
	    MMOD_ENGINE_VERSION_MINOR,
	    MMOD_ENGINE_VERSION_PATCH,
	    MMOD_ENGINE_VERSION_STRING,
	    MMOD_ENGINE_COMPONENT,
	};
	return &version;
}
