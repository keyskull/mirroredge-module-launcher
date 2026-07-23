#include "runtime_version.h"
#include "version.h"

extern "C" __declspec(dllexport) const MmodRuntimeVersion *MMOD_GetRuntimeVersion() {
	static const MmodRuntimeVersion version = {
	    MMOD_MANAGER_VERSION_MAJOR,
	    MMOD_MANAGER_VERSION_MINOR,
	    MMOD_MANAGER_VERSION_PATCH,
	    MMOD_MANAGER_VERSION_STRING,
	    MMOD_MANAGER_COMPONENT,
	};
	return &version;
}
