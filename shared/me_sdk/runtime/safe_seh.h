#pragma once

#include "memory_fault_log_c.h"

// SEH filter for MeSdk::Safe::* — records fault then swallows (same as EXCEPTION_EXECUTE_HANDLER).
#define ME_SDK_SAFE_EXCEPT_FILTER(context)                                       \
	MemoryFaultLog_Filter(GetExceptionInformation(), (context), __FUNCTION__)
