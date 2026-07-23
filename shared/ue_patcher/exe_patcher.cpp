// exe_patcher.cpp — PE binary patching implementation.
#include "exe_patcher.h"

#include <windows.h>
#include <psapi.h>
#include <cstring>
#include <vector>

#include "me_sdk/runtime/pattern.h"

namespace UePatcher {

// ---- Low-level primitives ----

static DWORD g_oldProtect; // cached by MakeWritable for RestoreProtection

bool MakeWritable(void *address, size_t len) {
	DWORD old;
	if (!VirtualProtect(address, len, PAGE_EXECUTE_READWRITE, &old))
		return false;
	g_oldProtect = old;
	return true;
}

void RestoreProtection(void *address, size_t len) {
	DWORD unused;
	VirtualProtect(address, len, g_oldProtect, &unused);
}

// ---- Utility ----

uintptr_t GetModuleBase(const char *moduleName) {
	HMODULE mod = moduleName
		? GetModuleHandleA(moduleName)
		: GetModuleHandleW(nullptr); // game EXE
	return reinterpret_cast<uintptr_t>(mod);
}

bool BytesMatch(const void *address, const uint8_t *expected, size_t len) {
	return std::memcmp(address, expected, len) == 0;
}

void *FindInModule(const char *moduleName,
                   const char *pattern, const char *mask) {
	return Pattern::FindPattern(moduleName, pattern, mask);
}

// ---- Patch application ----

// Generate an all-'x' mask for an exact byte sequence.
static std::vector<char> GenerateExactMask(size_t len) {
	std::vector<char> mask(len + 1, 'x');
	mask[len] = '\0';
	return mask;
}

static bool TryFindPatchTarget(const BinaryPatch &patch,
                               const uint8_t *moduleBase,
                               size_t moduleSize,
                               uint8_t **outAddress) {
	// Strategy 1: exact offset from module base
	if (patch.findOffset > 0 && !patch.findBytes && !patch.findMask) {
		*outAddress = const_cast<uint8_t *>(moduleBase) + patch.findOffset;
		return *outAddress < (moduleBase + moduleSize);
	}

	// Strategy 2: pattern with mask
	if (patch.findBytes && patch.findMask) {
		void *found = Pattern::FindPattern(
			const_cast<uint8_t *>(moduleBase),
			static_cast<int>(moduleSize),
			reinterpret_cast<const char *>(patch.findBytes),
			patch.findMask);
		if (found) {
			*outAddress = static_cast<uint8_t *>(found);
			return true;
		}
		return false;
	}

	// Strategy 3: exact byte sequence (no mask provided — generate one)
	if (patch.findBytes && !patch.findMask) {
		std::vector<char> mask = GenerateExactMask(patch.patchLen);
		void *found = Pattern::FindPattern(
			const_cast<uint8_t *>(moduleBase),
			static_cast<int>(moduleSize),
			reinterpret_cast<const char *>(patch.findBytes),
			mask.data());
		if (found) {
			*outAddress = static_cast<uint8_t *>(found);
			return true;
		}
		return false;
	}

	return false;
}

PatchResult ApplyBinaryPatch(const BinaryPatch &patch) {
	// Get module info
	const uintptr_t base = GetModuleBase(patch.moduleName);
	if (!base)
		return PatchResult::NotFound;

	MODULEINFO mi;
	if (!GetModuleInformation(GetCurrentProcess(),
	                          reinterpret_cast<HMODULE>(base),
	                          &mi, sizeof(mi)))
		return PatchResult::Error;

	const auto moduleBase = reinterpret_cast<const uint8_t *>(mi.lpBaseOfDll);
	const auto moduleSize = mi.SizeOfImage;

	// Find target
	uint8_t *target = nullptr;
	if (!TryFindPatchTarget(patch, moduleBase, moduleSize, &target))
		return PatchResult::NotFound;

	// Safety check: verify expected bytes
	if (patch.expectOriginal) {
		if (!BytesMatch(target, patch.expectOriginal, patch.patchLen))
			return PatchResult::AlreadyPatched; // or different version
	}

	// Check if already patched
	if (BytesMatch(target, patch.replaceBytes, patch.patchLen))
		return PatchResult::AlreadyPatched;

	// Apply
	if (!MakeWritable(target, patch.patchLen))
		return PatchResult::Protected;

	memcpy(target, patch.replaceBytes, patch.patchLen);
	FlushInstructionCache(GetCurrentProcess(), target, patch.patchLen);

	RestoreProtection(target, patch.patchLen);
	return PatchResult::Ok;
}

void ApplyVersionedPatch(const VersionedPatch &vp, PatchSession &session) {
	for (const auto &patch : vp.patches) {
		++session.total;
		const auto result = ApplyBinaryPatch(patch);
		switch (result) {
		case PatchResult::Ok:
			++session.applied;
			break;
		case PatchResult::AlreadyPatched:
			++session.skipped;
			break;
		default:
			++session.failed;
			break;
		}
	}
}

} // namespace UePatcher
