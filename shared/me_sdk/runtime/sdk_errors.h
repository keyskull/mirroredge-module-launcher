#pragma once

#include <cstdint>

namespace MeSdk {

enum class SdkError : uint32_t {
	Ok = 0,
	GameModuleMissing = 1,
	GameImageSizeMismatch = 2,
	GameCodeProbeMismatch = 3,
	PatternGNamesNotFound = 4,
	PatternGObjectsNotFound = 5,
	GNamesPointerInvalid = 6,
	GObjectsPointerInvalid = 7,
	GNamesArrayInvalid = 8,
	GObjectsArrayInvalid = 9,
	FNameSampleInvalid = 10,
	ClassLayoutMismatch = 11,
	ClassNotFound = 12,
};

struct RuntimeStatus {
	SdkError error = SdkError::Ok;
	uint32_t imageSize = 0;
	uint32_t codeProbeFnv = 0;
	uint32_t gnamesCount = 0;
	uint32_t gobjectsCount = 0;
	uintptr_t gnamesPattern = 0;
	uintptr_t gobjectsPattern = 0;
	uintptr_t moduleBase = 0;
};

const char *SdkErrorName(SdkError error);
SdkError GetLastSdkError();
const RuntimeStatus &GetLastRuntimeStatus();

} // namespace MeSdk
