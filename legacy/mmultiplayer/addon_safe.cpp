#include "addon_safe.h"
#include "modmanager.h"

namespace AddonSafe {

bool TryEnable(Addon *mod, bool *enabled, DWORD *exceptionCode) {
    if (exceptionCode) {
        *exceptionCode = 0;
    }
    if (!mod || !enabled) {
        return false;
    }

    bool result = false;
    __try {
        result = mod->Enable();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (exceptionCode) {
            *exceptionCode = GetExceptionCode();
        }
        return false;
    }

    *enabled = result;
    return true;
}

bool TryDisable(Addon *mod, DWORD *exceptionCode) {
    if (exceptionCode) {
        *exceptionCode = 0;
    }
    if (!mod) {
        return false;
    }

    __try {
        mod->Disable();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (exceptionCode) {
            *exceptionCode = GetExceptionCode();
        }
        return false;
    }

    return true;
}

} // namespace AddonSafe
