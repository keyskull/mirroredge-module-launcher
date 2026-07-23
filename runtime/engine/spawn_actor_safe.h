#pragma once

// Spawns an ASkeletalMeshActorSpawnable using `owner->Spawn(...)`
// guarded by SEH + C++ try/catch.  Returns nullptr on any crash.
// Must be in its own TU to avoid C2712 (__try vs C++ unwinding).

#ifdef __cplusplus
extern "C" {
#endif

void *__cdecl MMOD_SpawnActorSafe(void *owner, const char *context);

#ifdef __cplusplus
}
#endif
