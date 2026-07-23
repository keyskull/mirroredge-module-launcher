#include "init.h"

#include "debug_log.h"
#include "game_signature.h"
#include "me_sdk/me_sdk.h"
#include "pattern.h"
#include "patterns_globals.h"

#include <Windows.h>
#include <psapi.h>
#include <cstring>

namespace MeSdk {

namespace {

constexpr uintptr_t kMinGlobalAddress = 0x10000;
constexpr size_t kMinGlobalArrayCount = 256;
constexpr size_t kMaxGlobalArrayCount = 5000000;

thread_local SdkError g_lastError = SdkError::Ok;
thread_local RuntimeStatus g_lastStatus = {};

void *g_cachedGNamesPattern = nullptr;
void *g_cachedGObjectsPattern = nullptr;
bool g_probeSucceeded = false;

void *FindGNamesPattern() {
	if (g_cachedGNamesPattern) {
		return g_cachedGNamesPattern;
	}
	g_cachedGNamesPattern =
	    Pattern::FindPattern(Patterns::GNames, Patterns::GNamesMask);
	return g_cachedGNamesPattern;
}

void *FindGObjectsPattern() {
	if (g_cachedGObjectsPattern) {
		return g_cachedGObjectsPattern;
	}
	g_cachedGObjectsPattern =
	    Pattern::FindPattern(Patterns::GObjects, Patterns::GObjectsMask);
	return g_cachedGObjectsPattern;
}

void SetLastError(SdkError error) { g_lastError = error; }

void LogProbe(const char *message, const char *hypothesisId, uintptr_t a = 0,
              uintptr_t b = 0, uintptr_t c = 0, int d = 0) {
	if (!AgentDebugSessionActive()) {
		return;
	}
	AgentDebugLog("me_sdk", "runtime/init.cpp", message, hypothesisId, a, b, c, d);
}

void LogFailure(SdkError error, const char *message, bool logFailures) {
	SetLastError(error);
	g_lastStatus.error = error;
	if (logFailures) {
		LogProbe(message, "sdk-validate", g_lastStatus.moduleBase,
		         g_lastStatus.imageSize, g_lastStatus.codeProbeFnv,
		         static_cast<int>(error));
	}
}

bool IsPlausibleGlobalPointer(const void *ptr) {
	return ptr && reinterpret_cast<uintptr_t>(ptr) >= kMinGlobalAddress;
}

uint32_t Fnv1a32(const void *data, size_t length) {
	auto *bytes = reinterpret_cast<const uint8_t *>(data);
	uint32_t hash = 2166136261u;
	for (size_t i = 0; i < length; ++i) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

bool IsKnownCodeProbe(uint32_t probeFnv) {
	if (GameSignature::kKnownCodeProbeFnvCount == 0) {
		return true;
	}

	for (size_t i = 0; i < GameSignature::kKnownCodeProbeFnvCount; ++i) {
		if (GameSignature::kKnownCodeProbeFnv[i] == probeFnv) {
			return true;
		}
	}
	return false;
}

bool ValidateGameBinary(bool logFailures) {
	const auto module =
	    GetModuleHandleA(GameSignature::kMainModuleName);
	if (!module) {
		LogFailure(SdkError::GameModuleMissing, "game_module_missing",
		           logFailures);
		return false;
	}

	MODULEINFO info = {};
	if (!GetModuleInformation(GetCurrentProcess(), module, &info,
	                          sizeof(info))) {
		LogFailure(SdkError::GameModuleMissing, "module_info_failed",
		           logFailures);
		return false;
	}

	g_lastStatus.moduleBase = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
	g_lastStatus.imageSize = info.SizeOfImage;

	const auto *base = reinterpret_cast<const uint8_t *>(info.lpBaseOfDll);
	const auto probeOffset = GameSignature::kCodeProbeOffset;
	const auto probeLength = GameSignature::kCodeProbeLength;
	if (info.SizeOfImage < probeOffset + probeLength) {
		LogFailure(SdkError::GameImageSizeMismatch, "probe_range_invalid",
		           logFailures);
		return false;
	}

	g_lastStatus.codeProbeFnv =
	    Fnv1a32(base + probeOffset, probeLength);

	if (info.SizeOfImage < GameSignature::kMinImageSize ||
	    info.SizeOfImage > GameSignature::kMaxImageSize) {
		LogFailure(SdkError::GameImageSizeMismatch, "image_size_out_of_range",
		           logFailures);
		return false;
	}

	if (!IsKnownCodeProbe(g_lastStatus.codeProbeFnv)) {
		LogFailure(SdkError::GameCodeProbeMismatch, "code_probe_unknown",
		           logFailures);
		return false;
	}

	if (logFailures && AgentDebugSessionActive()) {
		LogProbe("game_binary_ok", "sdk-binary", g_lastStatus.moduleBase,
		         g_lastStatus.imageSize, g_lastStatus.codeProbeFnv, 0);
	}

	return true;
}

bool ValidateGlobalsTables(Classes::TArray<Classes::FNameEntry *> *gnames,
                           Classes::TArray<Classes::UObject *> *gobjects,
                           bool logFailures) {
	if (!IsPlausibleGlobalPointer(gnames)) {
		LogFailure(SdkError::GNamesPointerInvalid, "gnames_ptr_invalid",
		           logFailures);
		return false;
	}
	if (!IsPlausibleGlobalPointer(gobjects)) {
		LogFailure(SdkError::GObjectsPointerInvalid, "gobjects_ptr_invalid",
		           logFailures);
		return false;
	}

	const auto gnamesCount = gnames->Num();
	const auto gobjectsCount = gobjects->Num();
	g_lastStatus.gnamesCount = static_cast<uint32_t>(gnamesCount);
	g_lastStatus.gobjectsCount = static_cast<uint32_t>(gobjectsCount);

	if (gnamesCount < kMinGlobalArrayCount ||
	    gnamesCount > kMaxGlobalArrayCount) {
		LogFailure(SdkError::GNamesArrayInvalid, "gnames_count_invalid",
		           logFailures);
		return false;
	}
	if (gobjectsCount < kMinGlobalArrayCount ||
	    gobjectsCount > kMaxGlobalArrayCount) {
		LogFailure(SdkError::GObjectsArrayInvalid, "gobjects_count_invalid",
		           logFailures);
		return false;
	}

	if (!gnames->IsValidIndex(0)) {
		LogFailure(SdkError::FNameSampleInvalid, "fname_index0_invalid",
		           logFailures);
		return false;
	}

	const auto *nameEntry = (*gnames)[0];
	if (!nameEntry || nameEntry->GetName() != "None") {
		LogFailure(SdkError::FNameSampleInvalid, "fname_none_missing",
		           logFailures);
		return false;
	}

	if (!gobjects->IsValidIndex(0) ||
	    !IsPlausibleGlobalPointer((*gobjects)[0])) {
		LogFailure(SdkError::GObjectsArrayInvalid, "gobjects_sample_invalid",
		           logFailures);
		return false;
	}

	return true;
}

bool ResolveGlobalsFromPatterns(bool assign, bool logFailures) {
	g_lastStatus.error = SdkError::Ok;

	void *gnamesPattern = FindGNamesPattern();
	if (!gnamesPattern) {
		LogFailure(SdkError::PatternGNamesNotFound, "pattern_gnames_miss",
		           logFailures);
		return false;
	}
	g_lastStatus.gnamesPattern = reinterpret_cast<uintptr_t>(gnamesPattern);

	const auto gnames =
	    *reinterpret_cast<Classes::TArray<Classes::FNameEntry *> **>(
	        reinterpret_cast<byte *>(gnamesPattern) + 2);
	if (!IsPlausibleGlobalPointer(gnames)) {
		LogFailure(SdkError::GNamesPointerInvalid, "gnames_deref_invalid",
		           logFailures);
		return false;
	}

	void *gobjectsPattern = FindGObjectsPattern();
	if (!gobjectsPattern) {
		LogFailure(SdkError::PatternGObjectsNotFound, "pattern_gobjects_miss",
		           logFailures);
		return false;
	}
	g_lastStatus.gobjectsPattern =
	    reinterpret_cast<uintptr_t>(gobjectsPattern);

	const auto gobjects =
	    *reinterpret_cast<Classes::TArray<Classes::UObject *> **>(
	        reinterpret_cast<byte *>(gobjectsPattern) + 2);
	if (!IsPlausibleGlobalPointer(gobjects)) {
		LogFailure(SdkError::GObjectsPointerInvalid, "gobjects_deref_invalid",
		           logFailures);
		return false;
	}

	if (!ValidateGlobalsTables(gnames, gobjects, logFailures)) {
		return false;
	}

	if (assign) {
		Classes::FName::GNames = gnames;
		Classes::UObject::GObjects = gobjects;
	}

	if (logFailures && AgentDebugSessionActive()) {
		LogProbe("globals_resolved", "sdk-globals", g_lastStatus.gnamesPattern,
		         g_lastStatus.gobjectsPattern, g_lastStatus.gnamesCount,
		         static_cast<int>(g_lastStatus.gobjectsCount));
	}

	SetLastError(SdkError::Ok);
	g_lastStatus.error = SdkError::Ok;
	return true;
}

} // namespace

const char *SdkErrorName(SdkError error) {
	switch (error) {
	case SdkError::Ok:
		return "Ok";
	case SdkError::GameModuleMissing:
		return "GameModuleMissing";
	case SdkError::GameImageSizeMismatch:
		return "GameImageSizeMismatch";
	case SdkError::GameCodeProbeMismatch:
		return "GameCodeProbeMismatch";
	case SdkError::PatternGNamesNotFound:
		return "PatternGNamesNotFound";
	case SdkError::PatternGObjectsNotFound:
		return "PatternGObjectsNotFound";
	case SdkError::GNamesPointerInvalid:
		return "GNamesPointerInvalid";
	case SdkError::GObjectsPointerInvalid:
		return "GObjectsPointerInvalid";
	case SdkError::GNamesArrayInvalid:
		return "GNamesArrayInvalid";
	case SdkError::GObjectsArrayInvalid:
		return "GObjectsArrayInvalid";
	case SdkError::FNameSampleInvalid:
		return "FNameSampleInvalid";
	case SdkError::ClassLayoutMismatch:
		return "ClassLayoutMismatch";
	case SdkError::ClassNotFound:
		return "ClassNotFound";
	default:
		return "Unknown";
	}
}

SdkError GetLastSdkError() { return g_lastError; }

const RuntimeStatus &GetLastRuntimeStatus() { return g_lastStatus; }

bool AreGlobalsReady() {
	if (!Classes::FName::GNames || !Classes::UObject::GObjects) {
		SetLastError(SdkError::GNamesPointerInvalid);
		return false;
	}

	if (!ValidateGlobalsTables(Classes::FName::GNames,
	                           Classes::UObject::GObjects, false)) {
		return false;
	}

	SetLastError(SdkError::Ok);
	return true;
}

bool ProbeGlobals() {
	if (AreGlobalsReady()) {
		return true;
	}

	// ResolveGlobalsFromPatterns(assign=false) can succeed without assigning
	// GNames/GObjects. Throttling the next call within 100ms then made
	// IsGameReadyForModInit() flicker false on the main thread after core
	// queued init — infinite "init: game not ready yet" on borderless labs.
	if (g_probeSucceeded) {
		return true;
	}

	static DWORD lastProbeTick = 0;
	const DWORD now = GetTickCount();
	constexpr DWORD kMinProbeIntervalMs = 100;
	if (lastProbeTick != 0 && now - lastProbeTick < kMinProbeIntervalMs) {
		return false;
	}
	lastProbeTick = now;

	g_lastStatus = {};
	SetLastError(SdkError::Ok);
	const bool ok = ResolveGlobalsFromPatterns(false, false);
	if (ok) {
		g_probeSucceeded = true;
	}
	return ok;
}

bool InitializeGlobals() {
	if (AreGlobalsReady()) {
		return true;
	}

	return ResolveGlobalsFromPatterns(true, true);
}

bool ValidateRuntime(bool logFailures) {
	g_lastStatus = {};
	SetLastError(SdkError::Ok);

	if (!ValidateGameBinary(logFailures)) {
		return false;
	}
	if (!InitializeGlobals()) {
		return false;
	}
	if (!AreGlobalsReady()) {
		return false;
	}
	if (!ValidateClassLayouts(logFailures)) {
		return false;
	}

	if (logFailures && AgentDebugSessionActive()) {
		LogProbe("validate_runtime_ok", "sdk-validate", g_lastStatus.moduleBase,
		         g_lastStatus.gnamesCount, g_lastStatus.gobjectsCount, 0);
	}

	return true;
}

// Known class full UE3 name → expected sizeof for runtime validation.
// Mirrors the compile-time static_assert list in sdk_verify_generated.h.
// When a class is not found or its runtime size differs, ValidateClassLayouts
// sets the first mismatch in g_lastStatus and returns false.
namespace {
struct KnownClassCheck {
	const char *fullName;   // e.g. "Class Engine.Actor"
	const char *parentName; // e.g. "Class Core.Object" (nullptr for UObject)
};

// Critical class hierarchy verification at runtime.
// Each entry checks: 1) class is findable, 2) IsA(parent) returns true.
const KnownClassCheck kCriticalClasses[] = {
	{"Class Core.Object",                 nullptr},
	{"Class Core.Field",                  "Class Core.Object"},
	{"Class Core.Struct",                 "Class Core.Field"},
	{"Class Core.Function",               "Class Core.Struct"},
	{"Class Core.Class",                  "Class Core.Struct"},
	{"Class Engine.Actor",                "Class Core.Object"},
	{"Class Engine.Info",                 "Class Engine.Actor"},
	{"Class Engine.ReplicationInfo",      "Class Engine.Info"},
	{"Class Engine.Pawn",                 "Class Engine.Actor"},
	{"Class Engine.Controller",           "Class Engine.Actor"},
	{"Class Engine.PlayerController",     "Class Engine.Controller"},
	{"Class Engine.NavigationPoint",      "Class Engine.Actor"},
	{"Class Engine.Checkpoint",           "Class Engine.NavigationPoint"},
	{"Class Engine.WorldInfo",            "Class Engine.Actor"},
	{"Class Engine.Canvas",               "Class Core.Object"},
	{"Class Engine.GameViewportClient",   "Class Core.Object"},
	{"Class Engine.Engine",               "Class Core.Object"},
	{"Class Engine.GameEngine",           "Class Engine.Engine"},
	{"Class Engine.MapInfo",              "Class Core.Object"},
	{"Class Engine.HUD",                  "Class Engine.Actor"},
	// ── Team / replication state ───────────────────────────────────────
	{"Class Engine.PlayerReplicationInfo","Class Engine.ReplicationInfo"},
	{"Class Engine.GameReplicationInfo",  "Class Engine.ReplicationInfo"},
	{"Class Engine.TeamInfo",             "Class Engine.ReplicationInfo"},
	// ── TdGame-specific ────────────────────────────────────────────────
	{"Class TdGame.TdGameEngine",         "Class Engine.GameEngine"},
	{"Class TdGame.TdGameViewportClient", "Class Engine.GameViewportClient"},
	{"Class TdGame.TdConsole",            "Class Engine.Console"},
	{"Class TdGame.TdPlayerController",   "Class Engine.PlayerController"},
	{"Class TdGame.TdPawn",               "Class Engine.Pawn"},
	{"Class TdGame.TdPlayerPawn",         "Class TdGame.TdPawn"},
	{"Class TdGame.TdPlayerReplicationInfo","Class Engine.PlayerReplicationInfo"},
	{"Class TdGame.TdGameReplicationInfo","Class Engine.GameReplicationInfo"},
	{"Class TdGame.TdTeamInfo",           "Class Engine.TeamInfo"},
	{"Class TdGame.TdCheckpoint",         "Class Engine.Checkpoint"},
	{"Class TdGame.TdStashpoint",         "Class Engine.Actor"},
	{"Class TdGame.TdMapInfo",            "Class Engine.MapInfo"},
};

constexpr size_t kCriticalClassCount =
    sizeof(kCriticalClasses) / sizeof(kCriticalClasses[0]);
} // namespace

bool ValidateClassLayouts(bool logFailures) {
	if (!Classes::UObject::GObjects) {
		LogFailure(SdkError::GObjectsPointerInvalid,
		           "layout_check_no_gobjects", logFailures);
		return false;
	}

	for (size_t i = 0; i < kCriticalClassCount; ++i) {
		const auto &check = kCriticalClasses[i];
		auto *cls = Classes::UObject::FindClass(check.fullName);
		if (!cls) {
			LogFailure(SdkError::ClassNotFound, "class_not_found", logFailures);
			if (logFailures) {
				LogProbe(check.fullName, "class-not-found", 0, 0, 0,
				         static_cast<int>(SdkError::ClassNotFound));
			}
			return false;
		}

		if (check.parentName) {
			auto *parent = Classes::UObject::FindClass(check.parentName);
			if (!parent) {
				LogFailure(SdkError::ClassNotFound, "parent_class_not_found",
				           logFailures);
				return false;
			}
			if (!cls->IsA(parent)) {
				LogFailure(SdkError::ClassLayoutMismatch,
				           "isa_check_failed", logFailures);
				if (logFailures) {
					LogProbe(check.fullName, "isa-fail",
					         reinterpret_cast<uintptr_t>(cls),
					         reinterpret_cast<uintptr_t>(parent), 0,
					         static_cast<int>(SdkError::ClassLayoutMismatch));
				}
				return false;
			}
		}
	}

	if (logFailures && AgentDebugSessionActive()) {
		LogProbe("class_layouts_ok", "sdk-layouts", kCriticalClassCount, 0, 0, 0);
	}

	SetLastError(SdkError::Ok);
	g_lastStatus.error = SdkError::Ok;
	return true;
}

} // namespace MeSdk
