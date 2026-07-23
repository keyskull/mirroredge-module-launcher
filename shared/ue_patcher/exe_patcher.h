#pragma once
// exe_patcher.h — PE binary patching for MirrorsEdge.exe.
//
// Uses the existing Pattern::FindPattern and VirtualProtect to locate and
// modify bytes in the game executable's loaded image.  Designed as a
// lightweight substitute for MirrorsEdgeTweaks' ExePatcher.cs (which
// operates on the on-disk file).

#include "patcher_types.h"
#include <cstdint>

namespace UePatcher {

// ---- Low-level primitives ----

// Make `len` bytes at `address` writable.  Returns true on success.
bool MakeWritable(void *address, size_t len);

// Restore original page protection (call after patching).
void RestoreProtection(void *address, size_t len);


// ---- Patch application ----

// Apply a single binary patch.  Returns the result.
PatchResult ApplyBinaryPatch(const BinaryPatch &patch);

// Apply all patches in a VersionedPatch entry.
// Populates `session` with outcome counts.
void ApplyVersionedPatch(const VersionedPatch &vp, PatchSession &session);


// ---- Utility ----

// Get the base address of a loaded module (nullptr = game EXE).
uintptr_t GetModuleBase(const char *moduleName);

// Check whether bytes at `address` match `expected`.
bool BytesMatch(const void *address, const uint8_t *expected, size_t len);

// Search for pattern within module.  Returns address or nullptr.
void *FindInModule(const char *moduleName,
                   const char *pattern, const char *mask);

} // namespace UePatcher
