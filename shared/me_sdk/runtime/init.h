#pragma once

#include "sdk_errors.h"

namespace MeSdk {

// Cached GNames/GObjects pointers are assigned and pass table validation.
bool AreGlobalsReady();

// Pattern-scan without assigning globals; uses cached pattern hits when available.
bool ProbeGlobals();

bool InitializeGlobals();

// Game binary gate + globals init + AreGlobalsReady (for harness / diagnostics).
bool ValidateRuntime(bool logFailures = true);

// After globals are ready, find known classes via GObjects and verify their
// runtime sizes match the compiled SDK expectations. Returns true if all
// critical classes validate; populates g_lastStatus with first mismatch.
bool ValidateClassLayouts(bool logFailures = true);

} // namespace MeSdk
