#pragma once
// me_version.h — Game version detection for multi-version support.
//
// MirrorsEdgeTweaks (MET) uses JSON-based offset tables keyed by version.
// This module provides the runtime equivalent: detect the game build at
// startup so patches and SDK patterns can select version-specific data.

#include <cstdint>
#include <string>

namespace MeVersion {

// Known Mirror's Edge builds.
enum class Build {
	Unknown,
	ME_1_0_Retail,       // Standard retail 1.0
	ME_1_0_Steam,        // Steam release
	ME_1_0_EA_App,       // EA App / Origin
	ME_1_0_GOG,          // GOG release
};

// Result of version detection.
struct VersionInfo {
	Build       build;
	uint32_t    exeFileSize;     // For additional discrimination
	uint32_t    exeTimestamp;    // PE timestamp
	uint16_t    fileVersion[4];  // Major.Minor.Build.Revision
	std::string buildString;     // Human-readable
};

// Detect the game version by inspecting the loaded EXE image.
// Returns true if a known build was identified.
bool Detect(VersionInfo &out);

// Get the current version (must call Detect first).
Build Current();

// Get a human-readable build label.
const char *Label(Build build);

} // namespace MeVersion
