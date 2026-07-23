#pragma once
// patcher_types.h — Runtime binary patch descriptor types.
//
// These describe self-contained patches that can be applied to:
//   a) the game executable PE image (exe_patcher)
//   b) future: UE3 UFunction bytecode (ue_bytecode_patcher)
//
// Each patch is version-aware: it declares which game build(s) it supports
// and provides fallback search patterns when offsets are unknown.

#include <cstdint>
#include <string>
#include <vector>

namespace UePatcher {

// ---- Patch target: where in the process memory to patch ----

enum class PatchTarget {
	Exe,     // MirrorsEdge.exe PE image
	Engine,  // Engine.u (future: in-memory UFunction bytecode)
	Game,    // TdGame.u (future)
	Core,    // Core.u (future)
};

// ---- Patch result ----

enum class PatchResult {
	Ok,              // Applied successfully
	NotFound,        // Target not found (pattern / offset didn't match)
	AlreadyPatched,  // Patch bytes already present
	VersionMismatch, // Game version not in supported list
	Protected,       // Memory could not be made writable
	Error,           // General error
};

// ---- Single binary patch descriptor ----
//
// Describes one discrete binary modification:
//   - Find `findBytes` (with mask) at offset `findOffset` from module base
//   - Replace with `replaceBytes`
//   - Optionally verify `expectOriginal` before patching (safety check)

struct BinaryPatch {
	const char      *name;           // Human-readable patch name
	const char      *moduleName;     // Module to patch (nullptr = game EXE)
	int              findOffset;     // Offset from module base (0 = use pattern)
	const uint8_t   *findBytes;      // Bytes to search for (nullptr = use offset)
	const char      *findMask;       // Mask (e.g. "xxxx????", nullptr = exact)
	const uint8_t   *expectOriginal; // Safety: verify these bytes exist before patching
	size_t           patchLen;       // Length of findBytes / replaceBytes
	const uint8_t   *replaceBytes;   // Replacement bytes
	const char      *description;    // What this patch does
};

// ---- Versioned patch entry ----
//
// Maps a game build identifier to one or more binary patches.
// When the game version is unknown, patches with `searchFallback=true`
// can still be attempted via pattern scanning.

struct VersionedPatch {
	const char              *name;          // Patch name
	const char              *description;   // What the patch set does
	PatchTarget              target;        // Where patches apply
	bool                     searchFallback; // Try pattern scan if version unknown
	std::vector<const char*> supportedBuilds; // Build IDs (e.g. "ME_1.0_Retail")
	std::vector<BinaryPatch> patches;
};

// ---- Patch session ----
//
// Holds results of applying a set of patches.

struct PatchSession {
	int  total;
	int  applied;
	int  skipped;
	int  failed;
};

} // namespace UePatcher
