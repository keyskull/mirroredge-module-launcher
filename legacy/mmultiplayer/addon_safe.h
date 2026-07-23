#pragma once

#include <windows.h>

class Addon;

namespace AddonSafe {

// Returns true if Enable completed without SEH. *enabled is set from addon result.
bool TryEnable(Addon *mod, bool *enabled, DWORD *exceptionCode);
// Returns true if Disable completed without SEH.
bool TryDisable(Addon *mod, DWORD *exceptionCode);

} // namespace AddonSafe
