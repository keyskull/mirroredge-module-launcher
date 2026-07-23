#pragma once

#define MMOD_RUNTIME_VERSION_EXPORT "MMOD_GetRuntimeVersion"

#define MMOD_RUNTIME_VERSION_PACK(major, minor, patch) \
	(((major) << 16) | ((minor) << 8) | (patch))

#define MMOD_RUNTIME_VERSION_STRINGIFY2(x) #x
#define MMOD_RUNTIME_VERSION_STRINGIFY3(maj, min, pat) \
	MMOD_RUNTIME_VERSION_STRINGIFY2(maj) "." MMOD_RUNTIME_VERSION_STRINGIFY2(min) "." \
	MMOD_RUNTIME_VERSION_STRINGIFY2(pat)

struct MmodRuntimeVersion {
	unsigned major;
	unsigned minor;
	unsigned patch;
	const char *string;
	const char *component;
};

typedef const MmodRuntimeVersion *(__cdecl *MMOD_GetRuntimeVersionFn)();
