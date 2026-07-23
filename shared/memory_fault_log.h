#pragma once

#include "memory_fault_log_c.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace MemoryFaultLog {

constexpr size_t kMaxEntries = 64;

struct Entry {
	ULONGLONG tick64 = 0;
	DWORD threadId = 0;
	DWORD exceptionCode = 0;
	uintptr_t faultAddress = 0;
	uintptr_t instructionPointer = 0;
	char context[48] = {};
	char location[96] = {};
};

size_t Count();
void AppendJson(std::string &out, size_t maxEntries = 32);
const char *ExceptionCodeName(DWORD code);

} // namespace MemoryFaultLog
