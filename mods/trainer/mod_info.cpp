#include "mod_plugin_info.h"
#include "version.h"

extern "C" __declspec(dllexport) const ModPluginInfo *MMOD_GetPluginInfo() {
	static const ModPluginInfo info = {
	    MMOD_PLUGIN_INFO_API_VERSION,
	    MMOD_MOD_VERSION_MAJOR,
	    MMOD_MOD_VERSION_MINOR,
	    MMOD_MOD_VERSION_PATCH,
	    MMOD_MOD_ID,
	    MMOD_MOD_DISPLAY_NAME,
	    MMOD_MOD_REQUIRES_ID,
	    MMOD_MOD_REQUIRES_MIN_VERSION,
	};
	return &info;
}
