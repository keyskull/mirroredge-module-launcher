#pragma once

// Compile-time SDK layout verification.
// Include after generated class headers to enforce struct sizes and member offsets.
//
// Usage:
//   #include "me_sdk/sdk_verify.h"
//   MMOD_SDK_VERIFY_CLASS_SIZE(UObject, 0x003C);
//   MMOD_SDK_VERIFY_MEMBER_OFFSET(UObject, Name, 0x002C);
//
// When verification fails, static_assert fires at compile time, preventing
// a build from shipping with wrong offsets after a generated-header regen.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace MeSdk {
namespace Verify {

template <typename T, size_t ExpectedSize>
constexpr bool CheckClassSize() {
	return sizeof(T) == ExpectedSize;
}

template <typename T, typename MemberType>
constexpr size_t OffsetOfMember(MemberType T::*member) {
	return reinterpret_cast<size_t>(
	    &(reinterpret_cast<T const volatile *>(0)->*member));
}

} // namespace Verify
} // namespace MeSdk

#define MMOD_SDK_VERIFY_CLASS_SIZE(ClassName, ExpectedSize)                   \
	static_assert(sizeof(Classes::ClassName) == (ExpectedSize),              \
	              #ClassName " size mismatch: expected " #ExpectedSize       \
	                         " but compiled to different size — generated "  \
	                         "header is stale for this binary")

#define MMOD_SDK_VERIFY_MEMBER_OFFSET(ClassName, MemberName, ExpectedOffset)   \
	static_assert(                                                            \
	    offsetof(Classes::ClassName, MemberName) == (ExpectedOffset),        \
	    #ClassName "::" #MemberName                                          \
	    " offset mismatch: expected " #ExpectedOffset                        \
	    " — generated header is stale for this binary")

#define MMOD_SDK_VERIFY_VFUNC_INDEX(ClassName, Index, ExpectedIndex)          \
	static_assert((Index) == (ExpectedIndex),                                \
	              #ClassName " vfunc index " #Index                          \
	                         " mismatch — generated header is stale")

// Helper: assert two constant offsets are equal (for cross-class inheritance checks).
#define MMOD_SDK_VERIFY_OFFSET_EQ(name, a, b)                                  \
	static_assert((a) == (b), name " offset mismatch: " #a " != " #b)
