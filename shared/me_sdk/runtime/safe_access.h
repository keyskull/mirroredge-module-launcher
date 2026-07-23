#pragma once

#include <cstddef>
#include <cstdint>
#include <locale>
#include <set>
#include <string>

#include "../util/ME_Basic.hpp"

namespace Classes {
class UObject;
class UFunction;
class UClass;
} // namespace Classes

namespace MeSdk {
namespace Safe {

constexpr uintptr_t kMinValidAddress = 0x10000;
constexpr size_t kMaxTArrayCount = 5000000;

bool IsReadableMemory(const void *address, size_t size);
bool IsWritableMemory(void *address, size_t size);
bool IsPlausiblePointer(const void *ptr);
bool IsPlausibleUObject(Classes::UObject *object);
bool IsPlausibleUClass(Classes::UClass *cls);

bool TryMemcpy(const void *src, void *dst, size_t size);
bool TryProcessEvent(Classes::UObject *object, Classes::UFunction *function,
                      void *params);
bool TryIsA(Classes::UObject *object, Classes::UClass *classType);
bool TryGetFNameString(const Classes::FName &name, std::string &out);

template <typename T>
inline size_t BoundedTArrayCount(const Classes::TArray<T> &array) {
	if (array.Num() < 0) {
		return 0;
	}

	const auto count = static_cast<size_t>(array.Num());
	if (count > kMaxTArrayCount) {
		return 0;
	}

	if (count == 0) {
		return 0;
	}

	if (!IsPlausiblePointer(array.Buffer())) {
		return 0;
	}

	const auto bytes = count * sizeof(T);
	if (bytes / sizeof(T) != count) {
		return 0;
	}

	if (!IsReadableMemory(array.Buffer(), bytes)) {
		return 0;
	}

	return count;
}

template <typename T>
inline bool IsValidTArrayIndex(const Classes::TArray<T> &array, size_t index) {
	return index < BoundedTArrayCount(array);
}

template <typename T>
inline bool TryGetTArrayElement(const Classes::TArray<T> &array, size_t index,
                                T &out) {
	if (!IsValidTArrayIndex(array, index)) {
		return false;
	}

	return TryMemcpy(array.Buffer() + index, &out, sizeof(T));
}

template <typename T>
inline bool TrySetTArrayElement(Classes::TArray<T> &array, size_t index,
                                const T &value) {
	if (!IsValidTArrayIndex(array, index)) {
		return false;
	}

	return TryMemcpy(&value, array.Buffer() + index, sizeof(T));
}

template <typename T>
inline bool TryReadField(const T *ptr, T &out) {
	if (!ptr || !IsPlausiblePointer(ptr)) {
		return false;
	}

	return TryMemcpy(ptr, &out, sizeof(T));
}

template <typename T>
inline bool TryWriteField(T *ptr, const T &value) {
	if (!ptr || !IsWritableMemory(ptr, sizeof(T))) {
		return false;
	}

	return TryMemcpy(&value, ptr, sizeof(T));
}

using GlobalObjectVisitor =
    bool (*)(Classes::UObject *object, int index, void *context);

int ForEachGlobalObject(GlobalObjectVisitor visitor, void *context,
                        int maxCount = -1);

Classes::UObject *TryGetGlobalObject(int index);

} // namespace Safe
} // namespace MeSdk
