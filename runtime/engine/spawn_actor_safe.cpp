// spawn_actor_safe.cpp — freestanding SEH+CXX-guarded Spawn wrapper.
// Must live in its own translation unit because __try is incompatible
// with C++ object unwinding in the same TU as engine_hooks_gameplay.cpp.

#include <Windows.h>
#include <cstdio>

#include "me_sdk/me_sdk.h"
#include "me_sdk/runtime/safe_access.h"

// Inner helper: C++ exception guard only.
// Must be a separate function from MMOD_SpawnActorSafe because MSVC
// cannot mix __try and try in the same function.
static void *SpawnActorInner(Classes::AActor *owner) {
    try {
        auto *spawnClass = Classes::ASkeletalMeshActorSpawnable::StaticClass();
        if (!spawnClass) {
            OutputDebugStringA("MMOD_SpawnActorSafe: StaticClass null\n");
            return nullptr;
        }

        // Spawn at the owner location when possible — zero vectors often fail
        // in menu / unloaded worlds even with bNoCollisionFail.
        Classes::FVector loc = {};
        Classes::FRotator rot = {};
        MeSdk::Safe::TryReadField(&owner->Location, loc);
        MeSdk::Safe::TryReadField(&owner->Rotation, rot);

        return static_cast<Classes::ASkeletalMeshActorSpawnable *>(
            owner->Spawn(spawnClass, nullptr, Classes::FName(), loc, rot,
                         nullptr, true));
    } catch (...) {
        return nullptr;
    }
}

// Outer wrapper: SEH guard.
extern "C" {

// Internal helper used only within engine.dll via extern "C" forward
// declaration in engine_hooks_gameplay.cpp — intentionally NOT in engine.def.
void *__cdecl
MMOD_SpawnActorSafe(void *ownerPtr, const char *context) {
    void *result = nullptr;
    __try {
        if (!ownerPtr) {
            return nullptr;
        }
        result = SpawnActorInner(static_cast<Classes::AActor *>(ownerPtr));
        if (!result && context) {
            char buf[160] = {};
            snprintf(buf, sizeof(buf),
                     "MMOD_SpawnActorSafe: null owner=%p ctx=%s\n", ownerPtr,
                     context);
            OutputDebugStringA(buf);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char buf[128] = {};
        snprintf(buf, sizeof(buf),
                 "MMOD_SpawnActorSafe: SEH_CRASH code=0x%08lx\n",
                 GetExceptionCode());
        OutputDebugStringA(buf);
        result = nullptr;
    }
    return result;
}

} // extern "C"
