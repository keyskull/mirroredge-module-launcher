#include "runtime_version.h"
#include "version.h"

extern "C" __declspec(dllexport) const MmodRuntimeVersion *MMOD_GetRuntimeVersion() {
	static const MmodRuntimeVersion version = {
	    MMOD_MOD_VERSION_MAJOR,
	    MMOD_MOD_VERSION_MINOR,
	    MMOD_MOD_VERSION_PATCH,
	    MMOD_MOD_VERSION_STRING,
	    MMOD_CORE_COMPONENT,
	};
	return &version;
}
