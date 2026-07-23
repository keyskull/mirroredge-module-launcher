#include "runtime_version.h"
#include "version.h"

extern "C" __declspec(dllexport) const MmodRuntimeVersion *MMOD_GetRuntimeVersion() {
	static const MmodRuntimeVersion version = {
	    MMOD_CONSOLE_VERSION_MAJOR,
	    MMOD_CONSOLE_VERSION_MINOR,
	    MMOD_CONSOLE_VERSION_PATCH,
	    MMOD_CONSOLE_VERSION_STRING,
	    MMOD_CONSOLE_COMPONENT,
	};
	return &version;
}
