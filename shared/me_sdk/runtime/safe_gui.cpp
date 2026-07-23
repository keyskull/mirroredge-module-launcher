#include "safe_gui.h"

#include "init.h"
#include "safe_seh.h"

#include <cmath>
#include <cwchar>

namespace MeSdk {
namespace Safe {
namespace Gui {

namespace {

constexpr float kRotatorUnits = static_cast<float>(0x10000);

bool TryReadRotatorDegrees(const Classes::FRotator &rotation, float &pitchDeg,
                           float &yawDeg) {
	unsigned int pitch = 0;
	unsigned int yaw = 0;
	if (!TryReadField(&rotation.Pitch, pitch) || !TryReadField(&rotation.Yaw, yaw)) {
		return false;
	}

	pitchDeg = (static_cast<float>(pitch % 0x10000u) / kRotatorUnits) * 360.f;
	yawDeg = (static_cast<float>(yaw % 0x10000u) / kRotatorUnits) * 360.f;
	return true;
}

struct FindEngineContext {
	Classes::UTdGameEngine *engine = nullptr;
};

Classes::UTdGameEngine *g_tdEngineCache = nullptr;
bool g_tdEnginePcSeedOk = false;
int g_tdEngineWarmIndex = 0;
Classes::UClass *g_tdEngineClass = nullptr;
int g_tdEngineNameIndex = -1;
int g_transientNameIndex = -1;

// UEngine::GamePlayers @ 0x02BC — SEH layout check, no VirtualQuery.
// False Class hits with huge/garbage Count hang Tick TryGetTArrayElement
// right after drain.warm.engine.idle.done (rem=2 sp=0; 2026-07-21).
constexpr uintptr_t kTdEngineGamePlayersOffset = 0x02BCu;

bool TdEngineGamePlayersLayoutOk(Classes::UTdGameEngine *engine) {
	if (!engine) {
		return false;
	}
	void *data = nullptr;
	int32_t count = -1;
	int32_t maxCount = -1;
	__try {
		auto *base = reinterpret_cast<unsigned char *>(engine) +
		             kTdEngineGamePlayersOffset;
		data = *reinterpret_cast<void **>(base);
		count = *reinterpret_cast<int32_t *>(base + 4);
		maxCount = *reinterpret_cast<int32_t *>(base + 8);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	if (count < 1 || count > 8) {
		return false;
	}
	if (maxCount < 1 || maxCount > 64) {
		return false;
	}
	if (count > maxCount) {
		return false;
	}
	if (reinterpret_cast<uintptr_t>(data) < 0x10000u) {
		return false;
	}
	return true;
}

Classes::AWorldInfo *g_worldInfoCache = nullptr;
int g_worldInfoWarmIndex = 0;
Classes::UClass *g_worldInfoClass = nullptr;

struct FindControllerContext {
	Classes::ATdPlayerController *controller = nullptr;
};

struct FindWorldContext {
	Classes::AWorldInfo *world = nullptr;
};

bool WorldHasTdPlayerController(Classes::AWorldInfo *world) {
	Classes::AController *controller = nullptr;
	if (!TryReadField(&world->ControllerList, controller)) {
		return false;
	}

	for (int guard = 0; controller && guard < 256; ++guard) {
		if (TryIsA(controller, Classes::ATdPlayerController::StaticClass())) {
			return true;
		}

		Classes::AController *next = nullptr;
		if (!TryReadField(&controller->NextController, next)) {
			break;
		}
		controller = next;
	}

	return false;
}

bool VisitTdController(Classes::UObject *object, int, void *context) {
	auto *ctx = static_cast<FindControllerContext *>(context);
	if (!TryIsA(object, Classes::ATdPlayerController::StaticClass())) {
		return true;
	}

	ctx->controller = static_cast<Classes::ATdPlayerController *>(object);
	return false;
}

bool VisitActiveWorld(Classes::UObject *object, int, void *context) {
	auto *ctx = static_cast<FindWorldContext *>(context);
	if (!TryIsA(object, Classes::AWorldInfo::StaticClass())) {
		return true;
	}

	auto *world = static_cast<Classes::AWorldInfo *>(object);
	if (!WorldHasTdPlayerController(world)) {
		return true;
	}

	ctx->world = world;
	return false;
}

bool OuterNameIsTransient(Classes::UObject *object) {
	Classes::UObject *outer = nullptr;
	if (!TryReadField(&object->Outer, outer) || !outer) {
		return false;
	}

	Classes::FName outerName = {};
	if (!TryReadField(&outer->Name, outerName)) {
		return false;
	}

	std::string name;
	if (!TryGetFNameString(outerName, name)) {
		return false;
	}

	return name == "Transient";
}

bool VisitTdEngine(Classes::UObject *object, int, void *context) {
	auto *ctx = static_cast<FindEngineContext *>(context);
	if (!TryIsA(object, Classes::UTdGameEngine::StaticClass())) {
		return true;
	}

	if (!OuterNameIsTransient(object)) {
		return true;
	}

	ctx->engine = static_cast<Classes::UTdGameEngine *>(object);
	return false;
}

bool RawIsInMainMenu(Classes::ATdPlayerController *controller, bool &inMainMenu) {
	static Classes::UFunction *fn = nullptr;
	if (!fn) {
		fn = Classes::UObject::FindObject<Classes::UFunction>(
		    "Function TdGame.TdPlayerController.IsInMainMenu");
	}
	if (!fn) {
		return false;
	}

	Classes::ATdPlayerController_IsInMainMenu_Params params = {};
	if (!TryProcessEvent(controller, fn, &params)) {
		return false;
	}

	inMainMenu = params.ReturnValue;
	return true;
}

bool RawGetCameraFov(Classes::ATdPlayerController *controller, float &fov) {
	Classes::ACamera *camera = nullptr;
	if (!TryReadField(&controller->PlayerCamera, camera) || !camera ||
	    !IsPlausibleUObject(camera)) {
		return false;
	}

	static Classes::UFunction *fn = nullptr;
	if (!fn) {
		fn = Classes::UObject::FindObject<Classes::UFunction>(
		    "Function Engine.Camera.GetFOVAngle");
	}
	if (!fn) {
		return false;
	}

	Classes::ACamera_GetFOVAngle_Params params = {};
	if (!TryProcessEvent(camera, fn, &params)) {
		return false;
	}

	fov = params.ReturnValue;
	return true;
}

bool RawSetCameraFov(Classes::ATdPlayerController *controller, float fov) {
	Classes::ACamera *camera = nullptr;
	if (!TryReadField(&controller->PlayerCamera, camera) || !camera ||
	    !IsPlausibleUObject(camera)) {
		return false;
	}

	static Classes::UFunction *fn = nullptr;
	if (!fn) {
		fn = Classes::UObject::FindObject<Classes::UFunction>(
		    "Function Engine.Camera.SetFOV");
	}
	if (!fn) {
		return false;
	}

	Classes::ACamera_SetFOV_Params params = {};
	params.NewFOV = fov;
	return TryProcessEvent(camera, fn, &params);
}

bool RawGetMovementState(Classes::ATdPlayerPawn *pawn, int &movementState) {
	__try {
		movementState = static_cast<int>(pawn->MovementState.GetValue());
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gui")) {
		return false;
	}
}

bool RawReadSmoothFrameRate(Classes::UTdGameEngine *engine, bool &enabled) {
	__try {
		enabled = engine->bSmoothFrameRate != 0;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gui")) {
		return false;
	}
}

bool RawWriteSmoothFrameRate(Classes::UTdGameEngine *engine, bool enabled) {
	__try {
		engine->bSmoothFrameRate = enabled;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gui")) {
		return false;
	}
}

bool RawReadShouldBeLoaded(Classes::ULevelStreaming *level, bool &loaded) {
	__try {
		loaded = level->bShouldBeLoaded != 0;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gui")) {
		return false;
	}
}

bool RawWriteStreamingLevelLoaded(Classes::ULevelStreaming *level, bool loaded) {
	__try {
		level->bShouldBeLoaded = loaded;
		if (loaded) {
			level->bShouldBeVisible = true;
		}
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gui")) {
		return false;
	}
}

} // namespace

bool TrySeedPlayerControllerFromTdEngineGamePlayers(
    Classes::UTdGameEngine *engine, Classes::ATdPlayerController *&outPc) {
	outPc = nullptr;
	if (!engine || !TdEngineGamePlayersLayoutOk(engine)) {
		return false;
	}

	void *data = nullptr;
	int32_t count = 0;
	__try {
		auto *base = reinterpret_cast<unsigned char *>(engine) +
		             kTdEngineGamePlayersOffset;
		data = *reinterpret_cast<void **>(base);
		count = *reinterpret_cast<int32_t *>(base + 4);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	if (count < 1 || !data) {
		return false;
	}

	Classes::ULocalPlayer *localPlayer = nullptr;
	__try {
		localPlayer = reinterpret_cast<Classes::ULocalPlayer **>(data)[0];
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	if (!localPlayer || reinterpret_cast<uintptr_t>(localPlayer) < 0x10000u) {
		return false;
	}

	// UPlayer::Actor @ 0x0040
	Classes::APlayerController *actor = nullptr;
	__try {
		actor = *reinterpret_cast<Classes::APlayerController **>(
		    reinterpret_cast<unsigned char *>(localPlayer) + 0x40u);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	if (!actor || reinterpret_cast<uintptr_t>(actor) < 0x10000u) {
		return false;
	}

	outPc = static_cast<Classes::ATdPlayerController *>(actor);
	return true;
}

bool RawReadObjectName(Classes::UObject *object, Classes::FName *out) {
	if (!object || !out) {
		return false;
	}
	__try {
		*out = object->Name;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool RawReadObjectPtr(Classes::UObject **buffer, int index,
                      Classes::UObject **out) {
	if (!buffer || !out || index < 0) {
		return false;
	}
	__try {
		*out = buffer[index];
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool RawReadObjectClass(Classes::UObject *object, Classes::UClass **out) {
	if (!object || !out) {
		return false;
	}
	__try {
		*out = object->Class;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// SEH-only IsA (SuperField walk). No VirtualQuery / IsPlausibleUObject.
bool RawObjectClassIsA(Classes::UObject *object, Classes::UClass *target) {
	if (!object || !target) {
		return false;
	}
	Classes::UClass *cls = nullptr;
	if (!RawReadObjectClass(object, &cls) || !cls) {
		return false;
	}
	for (int depth = 0; depth < 64 && cls; ++depth) {
		if (cls == target) {
			return true;
		}
		Classes::UField *superField = nullptr;
		__try {
			superField = cls->SuperField;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
		cls = static_cast<Classes::UClass *>(superField);
	}
	return false;
}

// SEH-only Transient outer check via prewarmed FName index.
bool RawOuterNameIndexEquals(Classes::UObject *object, int nameIndex) {
	if (!object || nameIndex <= 0) {
		return false;
	}
	Classes::UObject *outer = nullptr;
	__try {
		outer = object->Outer;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	if (!outer) {
		return false;
	}
	int outerNameIndex = 0;
	__try {
		outerNameIndex = outer->Name.Index;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	return outerNameIndex == nameIndex;
}

void PrewarmTdGameEngineClass() {
	if (g_tdEngineClass) {
		return;
	}
	g_tdEngineClass = Classes::UTdGameEngine::StaticClass();
	if (g_tdEngineClass) {
		g_tdEngineNameIndex = g_tdEngineClass->Name.Index;
	}
	// g_transientNameIndex stays -1; warm uses OuterNameIsTransient only on
	// rare Class hits (not per GObjects slot).
}

void PrewarmWorldInfoClass() {
	if (g_worldInfoClass) {
		return;
	}
	g_worldInfoClass = Classes::AWorldInfo::StaticClass();
}

Classes::AWorldInfo *TryFindActiveWorldInfo(bool refresh) {
	if (!refresh) {
		if (g_worldInfoCache && IsPlausibleUObject(g_worldInfoCache)) {
			return g_worldInfoCache;
		}
		return nullptr;
	}

	g_worldInfoCache = nullptr;
	g_worldInfoWarmIndex = 0;
	if (!AreGlobalsReady()) {
		return nullptr;
	}

	FindWorldContext ctx;
	ForEachGlobalObject(VisitActiveWorld, &ctx);
	g_worldInfoCache = ctx.world;
	return g_worldInfoCache;
}

bool TryWarmActiveWorldInfoIncremental(int maxVisit) {
	if (g_worldInfoCache && IsPlausibleUObject(g_worldInfoCache)) {
		return true;
	}
	if (!AreGlobalsReady() || maxVisit <= 0) {
		return false;
	}
	if (!g_worldInfoClass) {
		// Prefer PrewarmWorldInfoClass() at SDK init.
		return false;
	}

	auto &objects = Classes::UObject::GetGlobalObjects();
	int bounded = objects.Num();
	if (bounded <= 0) {
		return false;
	}
	if (bounded > 500000) {
		bounded = 500000;
	}
	Classes::UObject **buffer = objects.Buffer();
	if (!buffer) {
		return false;
	}

	if (g_worldInfoWarmIndex < 0 || g_worldInfoWarmIndex >= bounded) {
		g_worldInfoWarmIndex = 0;
	}

	int visited = 0;
	while (g_worldInfoWarmIndex < bounded && visited < maxVisit) {
		const int index = g_worldInfoWarmIndex++;
		++visited;

		Classes::UObject *object = nullptr;
		if (!RawReadObjectPtr(buffer, index, &object) || !object) {
			continue;
		}
		const auto addr = reinterpret_cast<uintptr_t>(object);
		if (addr < 0x10000u) {
			continue;
		}
		if (!RawObjectClassIsA(object, g_worldInfoClass)) {
			continue;
		}

		auto *world = static_cast<Classes::AWorldInfo *>(object);
		// Class hits are rare — ControllerList probe is OK here.
		if (!WorldHasTdPlayerController(world)) {
			continue;
		}
		g_worldInfoCache = world;
		return true;
	}
	return g_worldInfoCache != nullptr;
}

bool TryReadPlayerOverlay(Classes::ATdPlayerPawn *pawn,
                          Classes::ATdPlayerController *controller,
                          PlayerOverlayInfo &out) {
	out = {};

	if (!IsPlausibleUObject(pawn) || !IsPlausibleUObject(controller)) {
		return false;
	}

	if (!TryIsA(pawn, Classes::ATdPlayerPawn::StaticClass()) ||
	    !TryIsA(controller, Classes::ATdPlayerController::StaticClass())) {
		return false;
	}

	Classes::FVector location = {};
	Classes::FVector velocity = {};
	int movementState = 0;
	if (!TryReadField(&pawn->Location, location) ||
	    !TryReadField(&pawn->Velocity, velocity) ||
	    !RawGetMovementState(pawn, movementState)) {
		return false;
	}

	float pitchDeg = 0.f;
	float yawDeg = 0.f;
	if (!TryReadRotatorDegrees(controller->Rotation, pitchDeg, yawDeg)) {
		return false;
	}

	out.valid = true;
	out.posX = location.X / 100.f;
	out.posY = location.Y / 100.f;
	out.posZ = location.Z / 100.f;
	out.velocity2D =
	    sqrtf(powf(velocity.X, 2.f) + powf(velocity.Y, 2.f)) * 0.036f;
	out.rotPitchDeg = pitchDeg;
	out.rotYawDeg = yawDeg;
	out.movementState = movementState;
	return true;
}

bool TryReadEngineMenuState(Classes::UTdGameEngine *engine, EngineMenuState &out) {
	out = {};

	if (!IsPlausibleUObject(engine) ||
	    !TryIsA(engine, Classes::UTdGameEngine::StaticClass())) {
		return false;
	}

	if (!RawReadSmoothFrameRate(engine, out.smoothFrameRate) ||
	    !TryReadField(&engine->MinSmoothedFrameRate, out.minSmoothedFrameRate) ||
	    !TryReadField(&engine->MaxSmoothedFrameRate, out.maxSmoothedFrameRate)) {
		return false;
	}

	Classes::UClient *client = nullptr;
	if (TryReadField(&engine->Client, client) && client &&
	    IsPlausibleUObject(client)) {
		out.hasClient = true;
		TryReadField(&client->DisplayGamma, out.displayGamma);
	}

	out.valid = true;
	return true;
}

bool TryWriteEngineSmoothFrameRate(Classes::UTdGameEngine *engine, bool enabled) {
	if (!IsPlausibleUObject(engine)) {
		return false;
	}

	return RawWriteSmoothFrameRate(engine, enabled);
}

bool TryWriteEngineFrameRateLimits(Classes::UTdGameEngine *engine, float minFps,
                                   float maxFps) {
	if (!IsPlausibleUObject(engine)) {
		return false;
	}

	if (!TryWriteField(&engine->MinSmoothedFrameRate, minFps)) {
		return false;
	}

	return TryWriteField(&engine->MaxSmoothedFrameRate, maxFps);
}

bool TryWriteClientGamma(Classes::UTdGameEngine *engine, float gamma) {
	if (!IsPlausibleUObject(engine)) {
		return false;
	}

	Classes::UClient *client = nullptr;
	if (!TryReadField(&engine->Client, client) || !client ||
	    !IsPlausibleUObject(client)) {
		return false;
	}

	return TryWriteField(&client->DisplayGamma, gamma);
}

bool TryReadWorldMenuState(Classes::ATdPlayerController *controller,
                           Classes::AWorldInfo *world, WorldMenuState &out) {
	out = {};

	if (!IsPlausibleUObject(controller) ||
	    !TryIsA(controller, Classes::ATdPlayerController::StaticClass())) {
		controller = TryFindTdPlayerController(false);
	}

	if (!IsPlausibleUObject(world) ||
	    !TryIsA(world, Classes::AWorldInfo::StaticClass())) {
		world = TryFindActiveWorldInfo(false);
	}

	if (!IsPlausibleUObject(controller) ||
	    !TryIsA(controller, Classes::ATdPlayerController::StaticClass())) {
		out.inMainMenu = true;
		out.valid = true;
		return true;
	}

	if (!RawIsInMainMenu(controller, out.inMainMenu)) {
		return false;
	}

	if (out.inMainMenu || !IsPlausibleUObject(world) ||
	    !TryIsA(world, Classes::AWorldInfo::StaticClass())) {
		out.valid = true;
		return true;
	}

	if (!TryReadField(&world->TimeDilation, out.timeDilation) ||
	    !TryReadField(&world->WorldGravityZ, out.worldGravityZ)) {
		return false;
	}

	Classes::TArray<Classes::ULevelStreaming *> levels = {};
	if (!TryReadField(&world->StreamingLevels, levels)) {
		return false;
	}

	const auto count = BoundedTArrayCount(levels);
	out.streamingLevels.reserve(count);

	for (size_t i = 0; i < count; ++i) {
		Classes::ULevelStreaming *level = nullptr;
		if (!TryGetTArrayElement(levels, i, level) || !level ||
		    !IsPlausibleUObject(level)) {
			continue;
		}

		StreamingLevelEntry entry = {};
		entry.level = level;
		RawReadShouldBeLoaded(level, entry.shouldBeLoaded);
		Classes::FName packageName = {};
		if (TryReadField(&level->PackageName, packageName)) {
			TryGetFNameString(packageName, entry.packageName);
		}
		if (entry.packageName.empty()) {
			entry.packageName = "?";
		}
		out.streamingLevels.push_back(std::move(entry));
	}

	out.valid = true;
	return true;
}

bool TryWriteWorldScalars(Classes::AWorldInfo *world, float timeDilation,
                          float gravityZ) {
	if (!IsPlausibleUObject(world)) {
		return false;
	}

	if (!TryWriteField(&world->TimeDilation, timeDilation)) {
		return false;
	}

	return TryWriteField(&world->WorldGravityZ, gravityZ);
}

bool TrySetStreamingLevelLoaded(Classes::ULevelStreaming *level, bool loaded) {
	if (!IsPlausibleUObject(level)) {
		return false;
	}

	return RawWriteStreamingLevelLoaded(level, loaded);
}

Classes::ATdPlayerController *TryFindTdPlayerController(bool refresh) {
	static Classes::ATdPlayerController *cache = nullptr;
	if (!refresh) {
		if (cache &&
		    reinterpret_cast<uintptr_t>(cache) >= 0x10000u) {
			return cache;
		}
		return nullptr;
	}

	cache = nullptr;
	if (!AreGlobalsReady()) {
		return nullptr;
	}

	FindControllerContext ctx;
	ForEachGlobalObject(VisitTdController, &ctx);
	cache = ctx.controller;
	return cache;
}

bool TryReadDollyGuiContext(Classes::ATdPlayerPawn *pawn,
                            Classes::ATdPlayerController *controller,
                            DollyGuiContext &out) {
	out = {};

	if (!IsPlausibleUObject(pawn) || !IsPlausibleUObject(controller)) {
		return false;
	}

	if (!TryReadField(&pawn->Location, out.location)) {
		return false;
	}

	Classes::AController *pawnController = nullptr;
	if (TryReadField(&pawn->Controller, pawnController) && pawnController) {
		TryReadField(&pawnController->Rotation, out.rotation);
	} else {
		TryReadField(&controller->Rotation, out.rotation);
	}

	RawGetCameraFov(controller, out.fov);
	out.valid = true;
	return true;
}

bool TryWriteCameraFov(Classes::ATdPlayerController *controller, float fov) {
	if (!IsPlausibleUObject(controller)) {
		return false;
	}

	return RawSetCameraFov(controller, fov);
}

Classes::UTdGameEngine *TryFindTdGameEngine(bool refresh) {
	if (!refresh) {
		if (g_tdEngineCache && g_tdEnginePcSeedOk &&
		    TdEngineGamePlayersLayoutOk(g_tdEngineCache) &&
		    reinterpret_cast<uintptr_t>(g_tdEngineCache) >= 0x10000u) {
			return g_tdEngineCache;
		}
		if (g_tdEngineCache) {
			g_tdEngineCache = nullptr;
			g_tdEnginePcSeedOk = false;
		}
		return nullptr;
	}

	g_tdEngineCache = nullptr;
	g_tdEnginePcSeedOk = false;
	g_tdEngineWarmIndex = 0;
	if (!AreGlobalsReady()) {
		return nullptr;
	}

	FindEngineContext ctx;
	ForEachGlobalObject(VisitTdEngine, &ctx);
	if (ctx.engine && TdEngineGamePlayersLayoutOk(ctx.engine)) {
		Classes::ATdPlayerController *probePc = nullptr;
		if (TrySeedPlayerControllerFromTdEngineGamePlayers(ctx.engine,
		                                                   probePc) &&
		    probePc) {
			g_tdEngineCache = ctx.engine;
			g_tdEnginePcSeedOk = true;
		}
	}
	return g_tdEngineCache;
}

bool TrySeedTdGameEngineFromPlayerController(Classes::APlayerController *pc) {
	if (g_tdEngineCache && IsPlausibleUObject(g_tdEngineCache)) {
		return true;
	}
	if (!pc || !IsPlausibleUObject(pc) || !g_tdEngineClass) {
		return false;
	}

	Classes::UPlayer *player = nullptr;
	if (!TryReadField(&pc->Player, player) || !player ||
	    !IsPlausibleUObject(player)) {
		return false;
	}

	Classes::UObject *outer = nullptr;
	if (!TryReadField(&player->Outer, outer) || !outer) {
		return false;
	}
	if (!TryIsA(outer, g_tdEngineClass)) {
		return false;
	}
	if (!OuterNameIsTransient(outer)) {
		return false;
	}

	auto *candidate = static_cast<Classes::UTdGameEngine *>(outer);
	if (!TdEngineGamePlayersLayoutOk(candidate)) {
		return false;
	}
	g_tdEngineCache = candidate;
	return true;
}

bool TryWarmTdGameEngineIncremental(int maxVisit) {
	if (g_tdEngineCache && g_tdEnginePcSeedOk &&
	    TdEngineGamePlayersLayoutOk(g_tdEngineCache) &&
	    reinterpret_cast<uintptr_t>(g_tdEngineCache) >= 0x10000u) {
		return true;
	}
	if (g_tdEngineCache) {
		g_tdEngineCache = nullptr;
		g_tdEnginePcSeedOk = false;
	}
	if (!AreGlobalsReady() || maxVisit <= 0) {
		return false;
	}

	// NEVER call TryFindTdPlayerController here. refresh=false still does a
	// full ForEachGlobalObject when the PC cache is empty, which VirtualQuerys
	// every GObjects slot and freezes EndScene for minutes at drain.warm.slice.
	// Callers may seed via TrySeedTdGameEngineFromPlayerController with a
	// cache-only Engine::GetPlayerController(false) pointer.

	if (!g_tdEngineClass) {
		// Prefer PrewarmTdGameEngineClass() at SDK init. Calling StaticClass
		// here can FindClass-walk GObjects and freeze EndScene for minutes.
		return false;
	}

	auto &objects = Classes::UObject::GetGlobalObjects();
	// Avoid BoundedTArrayCount here — it VirtualQuerys the entire GObjects
	// allocation once per EndScene and can hitch. Trust Num() with a hard cap.
	int bounded = objects.Num();
	if (bounded <= 0) {
		return false;
	}
	if (bounded > 500000) {
		bounded = 500000;
	}
	Classes::UObject **buffer = objects.Buffer();
	if (!buffer) {
		return false;
	}

	if (g_tdEngineWarmIndex < 0 || g_tdEngineWarmIndex >= bounded) {
		g_tdEngineWarmIndex = 0;
	}

	int visited = 0;
	while (g_tdEngineWarmIndex < bounded && visited < maxVisit) {
		const int index = g_tdEngineWarmIndex++;
		++visited;

		Classes::UObject *object = nullptr;
		if (!RawReadObjectPtr(buffer, index, &object) || !object) {
			continue;
		}
		const auto addr = reinterpret_cast<uintptr_t>(object);
		if (addr < 0x10000u) {
			continue;
		}

		// SuperField IsA via SEH only — exact Class* misses subclasses;
		// TryIsA/IsPlausibleUObject VirtualQuery and must stay off this path.
		if (!RawObjectClassIsA(object, g_tdEngineClass)) {
			continue;
		}
		// Transient filter only on Class hits (rare). Cache the name index
		// after the first successful string resolve for later SEH-only checks.
		if (g_transientNameIndex > 0) {
			if (!RawOuterNameIndexEquals(object, g_transientNameIndex)) {
				continue;
			}
		} else {
			if (!OuterNameIsTransient(object)) {
				continue;
			}
			Classes::UObject *outer = nullptr;
			Classes::FName outerName = {};
			if (TryReadField(&object->Outer, outer) && outer &&
			    TryReadField(&outer->Name, outerName) && outerName.Index > 0) {
				g_transientNameIndex = outerName.Index;
			}
		}
		auto *candidate = static_cast<Classes::UTdGameEngine *>(object);
		if (!TdEngineGamePlayersLayoutOk(candidate)) {
			continue;
		}
		Classes::ATdPlayerController *probePc = nullptr;
		if (!TrySeedPlayerControllerFromTdEngineGamePlayers(candidate,
		                                                    probePc) ||
		    !probePc) {
			continue;
		}
		g_tdEngineCache = candidate;
		g_tdEnginePcSeedOk = true;
		return true;
	}

	return false;
}

Classes::UTdGameViewportClient *TryFindTdViewportClient(bool refresh) {
	static Classes::UTdGameViewportClient *cache = nullptr;
	if (!refresh && cache && IsPlausibleUObject(cache)) {
		return cache;
	}

	cache = nullptr;
	const auto engine = TryFindTdGameEngine(refresh);
	Classes::UGameViewportClient *viewport = nullptr;
	if (!engine || !TryReadField(&engine->GameViewport, viewport) ||
	    !IsPlausibleUObject(viewport)) {
		return nullptr;
	}

	cache = static_cast<Classes::UTdGameViewportClient *>(viewport);
	return cache;
}

Classes::UPlayerInput *TryFindLocalPlayerInput() {
	const auto engine = TryFindTdGameEngine(false);
	if (!engine) {
		return nullptr;
	}

	Classes::ULocalPlayer *localPlayer = nullptr;
	if (!TryGetTArrayElement(engine->GamePlayers, 0, localPlayer) || !localPlayer) {
		return nullptr;
	}

	Classes::APlayerController *actor = nullptr;
	if (!TryReadField(&localPlayer->Actor, actor) || !actor ||
	    !IsPlausibleUObject(actor)) {
		return nullptr;
	}

	Classes::UPlayerInput *input = nullptr;
	if (!TryReadField(&actor->PlayerInput, input) || !IsPlausibleUObject(input)) {
		return nullptr;
	}

	return input;
}

bool TryQueryViewportSize(Classes::UTdGameViewportClient *viewport, int &width,
                          int &height) {
	width = 0;
	height = 0;

	if (!IsPlausibleUObject(viewport)) {
		return false;
	}

	static Classes::UFunction *fn = nullptr;
	if (!fn) {
		fn = Classes::UObject::FindObject<Classes::UFunction>(
		    "Function Engine.GameViewportClient.GetViewportSize");
	}
	if (!fn) {
		return false;
	}

	Classes::UGameViewportClient_GetViewportSize_Params params = {};
	if (!TryProcessEvent(viewport, fn, &params)) {
		return false;
	}

	width = static_cast<int>(params.out_ViewportSize.X + 0.5f);
	height = static_cast<int>(params.out_ViewportSize.Y + 0.5f);
	return width > 0 && height > 0;
}

bool TryRunSetResCommand(Classes::UTdGameViewportClient *viewport, int width,
                         int height) {
	if (!IsPlausibleUObject(viewport) || width < 1 || height < 1) {
		return false;
	}

	static Classes::UFunction *fn = nullptr;
	if (!fn) {
		fn = Classes::UObject::FindObject<Classes::UFunction>(
		    "Function Engine.GameViewportClient.ConsoleCommand");
	}
	if (!fn) {
		return false;
	}

	wchar_t command[64] = {};
	swprintf(command, 64, L"setres %dx%dw", width, height);

	Classes::UGameViewportClient_ConsoleCommand_Params params = {};
	params.Command = command;
	return TryProcessEvent(viewport, fn, &params);
}

bool TryGetWorldMapName(Classes::AWorldInfo *world, std::string &out) {
	out.clear();
	if (!IsPlausibleUObject(world)) {
		return false;
	}

	// world->GetMapName(false) is a virtual call that can fault during
	// level transitions when the world's internal FString is released
	// or the vtable is partially invalidated.  SEH-guard the whole call.
	const wchar_t *wideName = nullptr;
	__try {
		wideName = world->GetMapName(false).c_str();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	if (!wideName || !IsReadableMemory(wideName, sizeof(wchar_t))) {
		return false;
	}

	const auto len = wcsnlen(wideName, 256);
	if (len == 0) {
		return false;
	}

	out.resize(len);
	for (size_t i = 0; i < len; ++i) {
		const auto ch = wideName[i];
		if (ch == 0) {
			out.resize(i);
			break;
		}
		out[i] = static_cast<char>(ch >= 0x80 ? '?' : static_cast<unsigned char>(ch));
	}
	return !out.empty();
}

} // namespace Gui
} // namespace Safe
} // namespace MeSdk
