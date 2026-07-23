#pragma once

#include <Windows.h>

// In-memory bypass for Mirror's Edge Default*.ini integrity checks (btbd/memla
// signatures). BeginWatching() polls MirrorsEdge.exe and patches the main module
// shortly after start.
namespace ConfigIntegrityBypass {

bool IsBypassDisabled();
// True when launcher-managed game start should patch integrity checks (default).
// Set MMOD_DISABLE_CONFIG_BYPASS=1 to skip; MMOD_FORCE_CONFIG_BYPASS=1 is redundant.
bool NeedsConfigIntegrityBypass();

void BeginWatching();
void StopWatching();
// Patches the main module before the game thread runs (CREATE_SUSPENDED launch).
// Returns false if signatures were not found; watch thread may retry later.
bool TryApplyBeforeFirstRun(DWORD processId);
bool IsProcessPatched(DWORD processId);
void NotifyPatchFailed(DWORD processId);

} // namespace ConfigIntegrityBypass
