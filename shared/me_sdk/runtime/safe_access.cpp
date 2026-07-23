#include "safe_access.h"

#include "init.h"
#include "me_sdk/me_sdk.h"
#include "module_contract.h"
#include "safe_seh.h"

#include <Windows.h>
#include <Psapi.h>

namespace MeSdk {
namespace Safe {

namespace {

bool IsAddressInModule(uintptr_t address, HMODULE module) {
	if (!module) {
		return false;
	}

	MODULEINFO info = {};
	if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) {
		return false;
	}

	const auto base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
	return address >= base && address < base + info.SizeOfImage;
}

bool QueryRegion(const void *address, MEMORY_BASIC_INFORMATION &mbi) {
	return VirtualQuery(address, &mbi, sizeof(mbi)) != 0;
}

bool RegionAllowsRead(const MEMORY_BASIC_INFORMATION &mbi) {
	if (mbi.State != MEM_COMMIT) {
		return false;
	}

	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) {
		return false;
	}

	switch (mbi.Protect & 0xFF) {
	case PAGE_READONLY:
	case PAGE_READWRITE:
	case PAGE_WRITECOPY:
	case PAGE_EXECUTE_READ:
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY:
		return true;
	default:
		return false;
	}
}

bool RegionAllowsWrite(const MEMORY_BASIC_INFORMATION &mbi) {
	if (mbi.State != MEM_COMMIT) {
		return false;
	}

	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) {
		return false;
	}

	switch (mbi.Protect & 0xFF) {
	case PAGE_READWRITE:
	case PAGE_WRITECOPY:
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY:
		return true;
	default:
		return false;
	}
}

bool SpanSingleReadableRegion(const void *address, size_t size) {
	if (!address || size == 0) {
		return false;
	}

	const auto start = reinterpret_cast<uintptr_t>(address);
	const auto end = start + size;
	if (end < start) {
		return false;
	}

	auto cursor = start;
	while (cursor < end) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!QueryRegion(reinterpret_cast<const void *>(cursor), mbi)) {
			return false;
		}

		if (!RegionAllowsRead(mbi)) {
			return false;
		}

		const auto regionEnd =
		    reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (regionEnd <= cursor) {
			return false;
		}

		cursor = regionEnd;
	}

	return true;
}

bool SpanSingleWritableRegion(void *address, size_t size) {
	if (!address || size == 0) {
		return false;
	}

	const auto start = reinterpret_cast<uintptr_t>(address);
	const auto end = start + size;
	if (end < start) {
		return false;
	}

	auto cursor = start;
	while (cursor < end) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!QueryRegion(reinterpret_cast<const void *>(cursor), mbi)) {
			return false;
		}

		if (!RegionAllowsWrite(mbi)) {
			return false;
		}

		const auto regionEnd =
		    reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (regionEnd <= cursor) {
			return false;
		}

		cursor = regionEnd;
	}

	return true;
}

} // namespace

bool IsReadableMemory(const void *address, size_t size) {
	if (!IsPlausiblePointer(address)) {
		return false;
	}

	return SpanSingleReadableRegion(address, size);
}

bool IsWritableMemory(void *address, size_t size) {
	if (!IsPlausiblePointer(address)) {
		return false;
	}

	return SpanSingleWritableRegion(address, size);
}

bool IsPlausiblePointer(const void *ptr) {
	return ptr && reinterpret_cast<uintptr_t>(ptr) >= kMinValidAddress;
}

bool IsPlausibleUClass(Classes::UClass *cls) {
	if (!IsPlausiblePointer(cls)) {
		return false;
	}

	const auto address = reinterpret_cast<uintptr_t>(cls);
	if (IsAddressInModule(address, GetModuleHandle(nullptr)) ||
	    IsAddressInModule(address, GetModuleHandleW(MMOD_DLL_FILENAME))) {
		return false;
	}

	return IsReadableMemory(cls, sizeof(Classes::UClass *));
}

bool IsPlausibleUObject(Classes::UObject *object) {
	if (!IsPlausiblePointer(object)) {
		return false;
	}

	const auto address = reinterpret_cast<uintptr_t>(object);
	if (IsAddressInModule(address, GetModuleHandle(nullptr)) ||
	    IsAddressInModule(address, GetModuleHandleW(MMOD_DLL_FILENAME))) {
		return false;
	}

	if (!IsReadableMemory(object, sizeof(Classes::UObject *))) {
		return false;
	}

	Classes::UClass *cls = nullptr;
	if (!TryMemcpy(&object->Class, &cls, sizeof(cls)) || !IsPlausibleUClass(cls)) {
		return false;
	}

	return true;
}

bool TryMemcpy(const void *src, void *dst, size_t size) {
	if (!src || !dst || size == 0) {
		return false;
	}

	if (!IsReadableMemory(src, size) || !IsWritableMemory(dst, size)) {
		return false;
	}

	__try {
		memcpy(dst, src, size);
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_access")) {
		return false;
	}
}

bool TryProcessEvent(Classes::UObject *object, Classes::UFunction *function,
                     void *params) {
	if (!IsPlausibleUObject(object) || !IsPlausiblePointer(function)) {
		return false;
	}

	__try {
		object->ProcessEvent(function, params);
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_access")) {
		return false;
	}
}

bool TryIsA(Classes::UObject *object, Classes::UClass *classType) {
	if (!IsPlausibleUObject(object) || !IsPlausibleUClass(classType)) {
		return false;
	}

	__try {
		return object->IsA(classType);
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_access")) {
		return false;
	}
}

bool TryGetFNameString(const Classes::FName &name, std::string &out) {
	out.clear();

	if (!AreGlobalsReady() || !Classes::FName::GNames) {
		return false;
	}

	auto &names = Classes::FName::GetGlobalNames();
	const auto index = static_cast<size_t>(name.Index);
	if (!IsValidTArrayIndex(names, index)) {
		return false;
	}

	Classes::FNameEntry *entry = nullptr;
	if (!TryGetTArrayElement(names, index, entry) || !entry) {
		return false;
	}

	wchar_t wide[1024] = {};
	if (!TryMemcpy(entry->WideName, wide, sizeof(wide))) {
		return false;
	}

	size_t length = 0;
	while (length < sizeof(wide) / sizeof(wide[0]) && wide[length] != L'\0') {
		++length;
	}

	if (length == 0) {
		return name.Index == 0;
	}

	out.resize(length);
	for (size_t i = 0; i < length; ++i) {
		const auto ch = wide[i];
		out[i] = (ch >= 32 && ch <= 126) ? static_cast<char>(ch) : '?';
	}

	return true;
}

Classes::UObject *TryGetGlobalObject(int index) {
	if (!AreGlobalsReady() || index < 0) {
		return nullptr;
	}

	auto &objects = Classes::UObject::GetGlobalObjects();
	Classes::UObject *object = nullptr;
	if (!TryGetTArrayElement(objects, static_cast<size_t>(index), object)) {
		return nullptr;
	}

	if (!IsPlausibleUObject(object)) {
		return nullptr;
	}

	return object;
}

int ForEachGlobalObject(GlobalObjectVisitor visitor, void *context,
                        int maxCount) {
	if (!visitor || !AreGlobalsReady()) {
		return 0;
	}

	auto &objects = Classes::UObject::GetGlobalObjects();
	const auto bounded = BoundedTArrayCount(objects);
	if (bounded == 0) {
		return 0;
	}

	int limit = static_cast<int>(bounded);
	if (maxCount >= 0 && maxCount < limit) {
		limit = maxCount;
	}

	int visited = 0;
	for (int i = 0; i < limit; ++i) {
		auto *object = TryGetGlobalObject(i);
		if (!object) {
			continue;
		}

		if (!visitor(object, i, context)) {
			break;
		}

		++visited;
	}

	return visited;
}

} // namespace Safe
} // namespace MeSdk
