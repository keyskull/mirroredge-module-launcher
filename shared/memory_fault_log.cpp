#include "memory_fault_log.h"

#include "debug_log.h"
#include "module_contract.h"
#include "session_file_log.h"

#include <mutex>
#include <vector>

namespace {

constexpr wchar_t kSharedMappingName[] = L"Local\\MMOD_MemoryFaultLog_v1";

struct SharedRing {
	LONG initState;
	CRITICAL_SECTION lock;
	ULONGLONG sessionStartTick64;
	LONG totalWritten;
	MemoryFaultLog::Entry entries[MemoryFaultLog::kMaxEntries];
};

enum : LONG { kInitNone = 0, kInitInProgress = 1, kInitDone = 2 };

SharedRing *g_shared = nullptr;
HANDLE g_mapping = nullptr;
std::mutex g_localFallbackMutex;
std::vector<MemoryFaultLog::Entry> g_localFallback;
ULONGLONG g_localSessionStartTick64 = 0;

bool EnsureSharedRing() {
	if (g_shared) {
		return true;
	}

	static std::once_flag once;
	std::call_once(once, []() {
		g_mapping =
		    CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
		                       static_cast<DWORD>(sizeof(SharedRing)),
		                       kSharedMappingName);
		if (!g_mapping) {
			return;
		}

		g_shared = static_cast<SharedRing *>(MapViewOfFile(
		    g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedRing)));
		if (!g_shared) {
			return;
		}

		if (InterlockedCompareExchange(&g_shared->initState, kInitInProgress,
		                               kInitNone) == kInitNone) {
			InitializeCriticalSection(&g_shared->lock);
			g_shared->sessionStartTick64 = GetTickCount64();
			g_shared->totalWritten = 0;
			InterlockedExchange(&g_shared->initState, kInitDone);
		} else {
			while (g_shared->initState != kInitDone) {
				Sleep(0);
			}
		}
	});

	return g_shared != nullptr;
}

ULONGLONG SessionStartTick64() {
	if (EnsureSharedRing()) {
		return g_shared->sessionStartTick64;
	}
	if (!g_localSessionStartTick64) {
		g_localSessionStartTick64 = GetTickCount64();
	}
	return g_localSessionStartTick64;
}

void JsonEscapeInto(const char *src, std::string &out) {
	if (!src) {
		return;
	}
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(src);
	     *p; ++p) {
		const unsigned char c = *p;
		if (c == '"' || c == '\\') {
			out.push_back('\\');
			out.push_back(static_cast<char>(c));
		} else if (c < 0x20) {
			out.push_back(' ');
		} else {
			out.push_back(static_cast<char>(c));
		}
	}
}

void CopyLabel(char *dst, size_t dstSize, const char *src) {
	if (!dst || dstSize == 0) {
		return;
	}
	if (!src || !src[0]) {
		dst[0] = '\0';
		return;
	}
	const size_t n = dstSize - 1;
	strncpy(dst, src, n);
	dst[n] = '\0';
}

void MirrorEntryToLogs(const MemoryFaultLog::Entry &entry) {
	const auto elapsedSec =
	    (entry.tick64 - SessionStartTick64()) / 1000ULL;
	const char *codeName = MemoryFaultLog::ExceptionCodeName(entry.exceptionCode);

	char line[512] = {};
	snprintf(line, sizeof(line),
	         "[%4llus][memory_fault] code=0x%08lX (%s) addr=0x%08X eip=0x%08X "
	         "thread=%lu ctx=%s at %s",
	         static_cast<unsigned long long>(elapsedSec),
	         static_cast<unsigned long>(entry.exceptionCode), codeName,
	         static_cast<unsigned>(entry.faultAddress),
	         static_cast<unsigned>(entry.instructionPointer),
	         static_cast<unsigned long>(entry.threadId), entry.context,
	         entry.location);
	SessionFileLogWrite(line);

	if (AgentDebugSessionActive()) {
		AgentDebugLog("memory_fault", entry.location, "memory_fault", "mem-fault",
		              entry.exceptionCode, entry.faultAddress,
		              entry.instructionPointer,
		              static_cast<int>(entry.threadId));
	}
}

void StoreEntry(const MemoryFaultLog::Entry &entry) {
	if (EnsureSharedRing()) {
		EnterCriticalSection(&g_shared->lock);
		const LONG slot =
		    g_shared->totalWritten % static_cast<LONG>(MemoryFaultLog::kMaxEntries);
		g_shared->entries[slot] = entry;
		++g_shared->totalWritten;
		LeaveCriticalSection(&g_shared->lock);
	} else {
		std::lock_guard<std::mutex> lock(g_localFallbackMutex);
		g_localFallback.push_back(entry);
		while (g_localFallback.size() > MemoryFaultLog::kMaxEntries) {
			g_localFallback.erase(g_localFallback.begin());
		}
	}

	MirrorEntryToLogs(entry);
}

void CollectSnapshot(std::vector<MemoryFaultLog::Entry> &out) {
	out.clear();

	if (EnsureSharedRing()) {
		EnterCriticalSection(&g_shared->lock);
		const LONG total = g_shared->totalWritten;
		const LONG count =
		    total > static_cast<LONG>(MemoryFaultLog::kMaxEntries)
		        ? static_cast<LONG>(MemoryFaultLog::kMaxEntries)
		        : total;
		const LONG start = total - count;
		out.reserve(static_cast<size_t>(count));
		for (LONG i = start; i < total; ++i) {
			const LONG slot =
			    i % static_cast<LONG>(MemoryFaultLog::kMaxEntries);
			out.push_back(g_shared->entries[slot]);
		}
		LeaveCriticalSection(&g_shared->lock);
		return;
	}

	std::lock_guard<std::mutex> lock(g_localFallbackMutex);
	out = g_localFallback;
}

} // namespace

extern "C" {

int MemoryFaultLog_Filter(LPEXCEPTION_POINTERS pointers, const char *context,
                          const char *location) {
	MemoryFaultLog_RecordException(pointers, context, location);
	return EXCEPTION_EXECUTE_HANDLER;
}

void MemoryFaultLog_Record(unsigned long exceptionCode, unsigned long faultAddress,
                           unsigned long instructionPointer, const char *context,
                           const char *location) {
	MemoryFaultLog::Entry entry = {};
	entry.tick64 = GetTickCount64();
	entry.threadId = GetCurrentThreadId();
	entry.exceptionCode = exceptionCode;
	entry.faultAddress = static_cast<uintptr_t>(faultAddress);
	entry.instructionPointer = static_cast<uintptr_t>(instructionPointer);
	CopyLabel(entry.context, sizeof(entry.context), context);
	CopyLabel(entry.location, sizeof(entry.location), location);
	StoreEntry(entry);
}

void MemoryFaultLog_RecordException(LPEXCEPTION_POINTERS pointers,
                                    const char *context, const char *location) {
	if (!pointers || !pointers->ExceptionRecord) {
		MemoryFaultLog_Record(0, 0, 0, context, location);
		return;
	}

	const auto *record = pointers->ExceptionRecord;
	uintptr_t faultAddress = 0;
	if (record->NumberParameters >= 2) {
		faultAddress = static_cast<uintptr_t>(record->ExceptionInformation[1]);
	}

	uintptr_t eip = 0;
	if (pointers->ContextRecord) {
		eip = static_cast<uintptr_t>(pointers->ContextRecord->Eip);
	}

	MemoryFaultLog_Record(record->ExceptionCode,
	                      static_cast<unsigned long>(faultAddress),
	                      static_cast<unsigned long>(eip), context, location);
}

} // extern "C"

namespace MemoryFaultLog {

const char *ExceptionCodeName(DWORD code) {
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:
		return "ACCESS_VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		return "ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_BREAKPOINT:
		return "BREAKPOINT";
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		return "DATATYPE_MISALIGNMENT";
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		return "FLT_DENORMAL_OPERAND";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		return "FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_FLT_INEXACT_RESULT:
		return "FLT_INEXACT_RESULT";
	case EXCEPTION_FLT_INVALID_OPERATION:
		return "FLT_INVALID_OPERATION";
	case EXCEPTION_FLT_OVERFLOW:
		return "FLT_OVERFLOW";
	case EXCEPTION_FLT_STACK_CHECK:
		return "FLT_STACK_CHECK";
	case EXCEPTION_FLT_UNDERFLOW:
		return "FLT_UNDERFLOW";
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		return "ILLEGAL_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR:
		return "IN_PAGE_ERROR";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		return "INT_DIVIDE_BY_ZERO";
	case EXCEPTION_INT_OVERFLOW:
		return "INT_OVERFLOW";
	case EXCEPTION_INVALID_DISPOSITION:
		return "INVALID_DISPOSITION";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		return "NONCONTINUABLE_EXCEPTION";
	case EXCEPTION_PRIV_INSTRUCTION:
		return "PRIV_INSTRUCTION";
	case EXCEPTION_SINGLE_STEP:
		return "SINGLE_STEP";
	case EXCEPTION_STACK_OVERFLOW:
		return "STACK_OVERFLOW";
	default:
		return "UNKNOWN";
	}
}

size_t Count() {
	std::vector<Entry> snapshot;
	CollectSnapshot(snapshot);
	return snapshot.size();
}

void AppendJson(std::string &out, size_t maxEntries) {
	std::vector<Entry> snapshot;
	CollectSnapshot(snapshot);

	out += ",\"memoryFaultCount\":";
	out += std::to_string(snapshot.size());
	out += ",\"memoryFaults\":[";

	const size_t start =
	    snapshot.size() > maxEntries ? snapshot.size() - maxEntries : 0;
	for (size_t i = start; i < snapshot.size(); ++i) {
		const auto &e = snapshot[i];
		if (i > start) {
			out.push_back(',');
		}
		const auto elapsedMs = e.tick64 - SessionStartTick64();
		out += "{\"elapsedMs\":";
		out += std::to_string(elapsedMs);
		out += ",\"threadId\":";
		out += std::to_string(e.threadId);
		out += ",\"exceptionCode\":";
		out += std::to_string(e.exceptionCode);
		out += ",\"exceptionName\":";
		out.push_back('"');
		JsonEscapeInto(ExceptionCodeName(e.exceptionCode), out);
		out.push_back('"');
		out += ",\"faultAddress\":";
		out += std::to_string(e.faultAddress);
		out += ",\"instructionPointer\":";
		out += std::to_string(e.instructionPointer);
		out += ",\"context\":";
		out.push_back('"');
		JsonEscapeInto(e.context, out);
		out.push_back('"');
		out += ",\"location\":";
		out.push_back('"');
		JsonEscapeInto(e.location, out);
		out.push_back('"');
		out += "}";
	}

	out += "]";
}

} // namespace MemoryFaultLog
