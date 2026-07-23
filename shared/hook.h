#pragma once

#include <Windows.h>

#define JMP_SIZE (5)
#define RELATIVE_ADDR(addr, size)                                              \
    ((void *)((byte *)addr +                                                   \
              *(int *)((byte *)addr + (size - (int)sizeof(int))) + size))

namespace Hook {

byte GetInstructionLength(byte table[], byte *instruction);
bool SetJMP(void *dest, void *src, int nops);
bool TrampolineHook(void *dest, void *src, void **original = nullptr);
bool TrampolineHookNoSuspend(void *dest, void *src, void **original = nullptr);
bool ImportHook(HMODULE module, const char *importDll, const char *importName,
                void *hook, void **original);
bool UnTrampolineHook(void *src, void *original);
typedef bool (*SuspendedCallback)();
bool WithSuspendedThreads(SuspendedCallback callback);
// Emergency: drain thread suspend counts (hook install or external debugger).
void ReleaseProcessThreadSuspensions();

}; // namespace Hook