#pragma once

#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

int MemoryFaultLog_Filter(LPEXCEPTION_POINTERS pointers, const char *context,
                          const char *location);

void MemoryFaultLog_Record(unsigned long exceptionCode, unsigned long faultAddress,
                           unsigned long instructionPointer, const char *context,
                           const char *location);

void MemoryFaultLog_RecordException(LPEXCEPTION_POINTERS pointers,
                                    const char *context, const char *location);

#ifdef __cplusplus
}
#endif
