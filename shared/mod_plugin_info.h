#pragma once

#define MMOD_PLUGIN_INFO_API_VERSION 1U
#define MMOD_PLUGIN_INFO_EXPORT "MMOD_GetPluginInfo"

#define MMOD_PACK_VERSION(major, minor, patch) \
	(((major) << 16) | ((minor) << 8) | (patch))

#define MMOD_UNPACK_VERSION_MAJOR(packed) (((packed) >> 16) & 0xFFFFu)
#define MMOD_UNPACK_VERSION_MINOR(packed) (((packed) >> 8) & 0xFFu)
#define MMOD_UNPACK_VERSION_PATCH(packed) ((packed) & 0xFFu)

struct ModPluginInfo {
	unsigned apiVersion;
	unsigned major;
	unsigned minor;
	unsigned patch;
	const char *id;
	const char *displayName;
	const char *requiresId;
	unsigned requiresMinVersion;
};

typedef const ModPluginInfo *(__cdecl *MMOD_GetPluginInfoFn)();
