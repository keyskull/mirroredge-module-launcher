#pragma once

#include "me_sdk/me_sdk.h"

#include <cmath>

namespace MeSdk {

inline float Distance(const Classes::FVector &from, const Classes::FVector &to) {
	return sqrtf(((from.X - to.X) * (from.X - to.X)) + ((from.Y - to.Y) * (from.Y - to.Y)) +
	             ((from.Z - to.Z) * (from.Z - to.Z))) /
	       100.0f;
}

inline Classes::FRotator VectorToRotator(const Classes::FVector &vector) {
	auto convert = [](float r) {
		return static_cast<unsigned int>((fmodf(r, 360.0f) / 360.0f) * 0x10000);
	};

	return Classes::FRotator{convert(vector.X), convert(vector.Y), convert(vector.Z)};
}

inline Classes::FVector RotatorToVector(const Classes::FRotator &rotator) {
	auto convert = [](unsigned int r) {
		return (static_cast<float>(r % 0x10000) / static_cast<float>(0x10000)) * 360.0f;
	};

	return Classes::FVector{convert(rotator.Pitch), convert(rotator.Yaw), convert(rotator.Roll)};
}

} // namespace MeSdk
