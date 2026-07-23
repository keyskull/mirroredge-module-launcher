#include "safe_gameplay.h"

#include <Windows.h>

#include "safe_seh.h"

namespace MeSdk {
namespace Safe {
namespace Gameplay {

namespace {

bool RawReadPawnPose(Classes::ATdPlayerPawn *pawn, PawnPoseSnapshot &out) {
	__try {
		out.location = pawn->Location;
		out.velocity = pawn->Velocity;
		out.rotation = pawn->Rotation;
		out.targetMeshTranslationZ = pawn->TargetMeshTranslationZ;
		out.health = pawn->Health;
		out.enterFallingHeight = pawn->EnterFallingHeight;
		out.physics = static_cast<int>(pawn->Physics.GetValue());
		out.movementState = static_cast<int>(pawn->MovementState.GetValue());
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay")) {
		return false;
	}
}

bool RawReadFlyInput(Classes::ATdPlayerController *controller,
                     FlyTickContext &out) {
	__try {
		if (!controller->PlayerInput) {
			return false;
		}

		out.rawJoyUp = controller->PlayerInput->RawJoyUp;
		out.rawJoyRight = controller->PlayerInput->RawJoyRight;
		out.controllerYaw = controller->Rotation.Yaw;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay")) {
		return false;
	}
}

bool RawApplyGodMode(Classes::ATdPlayerPawn *pawn) {
	__try {
		pawn->Health = pawn->MaxHealth;
		pawn->EnterFallingHeight = -1e30f;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay")) {
		return false;
	}
}

bool RawApplyFlyState(Classes::ATdPlayerPawn *pawn,
                      const Classes::FVector &location) {
	__try {
		pawn->Location = location;
		pawn->Velocity = {};
		pawn->bCollideWorld = false;
		pawn->Physics = Classes::EPhysics::PHYS_None;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay")) {
		return false;
	}
}

bool RawWriteControllerIgnoreInput(Classes::ATdPlayerController *controller,
                                   bool ignore) {
	__try {
		controller->bIgnoreMoveInput = ignore ? 1 : 0;
		controller->bIgnoreButtonInput = ignore ? 1 : 0;
		controller->bIgnoreMovementFocus = ignore;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay")) {
		return false;
	}
}

bool RawReadPawnHealth(Classes::ATdPlayerPawn *pawn, float &health) {
	__try {
		health = pawn->Health;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay")) {
		return false;
	}
}

bool RawGetBoneBuffer(Classes::ATdPlayerPawn *pawn, void *&buffer,
                      int &atomCount) {
	atomCount = 0;
	__try {
		if (!pawn->Mesh3p) {
			return false;
		}

		// First-person play often leaves Mesh3p off-screen; without this flag
		// LocalAtoms go stale/empty and MP bone samples never succeed.
		pawn->Mesh3p->bUpdateSkelWhenNotRendered = true;

		auto *atoms = pawn->Mesh3p->LocalAtoms.Buffer();
		const int num = pawn->Mesh3p->LocalAtoms.Num();
		if (!atoms || num <= 0) {
			return false;
		}

		buffer = atoms;
		atomCount = num;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay")) {
		return false;
	}
}

} // namespace

static Classes::UClass *s_tdPlayerPawnClass = nullptr;
static Classes::UClass *s_tdPlayerControllerClass = nullptr;

void PrewarmClasses() {
	if (!s_tdPlayerPawnClass) {
		s_tdPlayerPawnClass = Classes::ATdPlayerPawn::StaticClass();
	}
	if (!s_tdPlayerControllerClass) {
		s_tdPlayerControllerClass = Classes::ATdPlayerController::StaticClass();
	}
}

bool TryReadPawnPose(Classes::ATdPlayerPawn *pawn, PawnPoseSnapshot &out) {
	out = {};

	if (!pawn || reinterpret_cast<uintptr_t>(pawn) < 0x10000u) {
		return false;
	}
	// Hot Tick path: no IsPlausibleUObject / TryIsA — both VirtualQuery or
	// walk SuperField and have frozen tick.callbacks after idle.done (2026-07-21).
	// RawRead is SEH-guarded.
	if (!RawReadPawnPose(pawn, out)) {
		return false;
	}

	out.valid = true;
	return true;
}

bool TryReadPawnHealth(Classes::ATdPlayerPawn *pawn, float &health) {
	health = 0.f;
	if (!IsPlausibleUObject(pawn)) {
		return false;
	}

	return RawReadPawnHealth(pawn, health);
}

bool TryReadFlyTickContext(Classes::ATdPlayerController *controller,
                           FlyTickContext &out) {
	out = {};

	if (!IsPlausibleUObject(controller)) {
		return false;
	}
	if (s_tdPlayerControllerClass &&
	    !TryIsA(controller, s_tdPlayerControllerClass)) {
		return false;
	}

	if (!RawReadFlyInput(controller, out)) {
		return false;
	}

	out.valid = true;
	return true;
}

bool TryApplyGodModeScalars(Classes::ATdPlayerPawn *pawn) {
	if (!IsPlausibleUObject(pawn)) {
		return false;
	}

	return RawApplyGodMode(pawn);
}

bool TryApplyFlyState(Classes::ATdPlayerPawn *pawn,
                      const Classes::FVector &location,
                      const Classes::FVector & /*velocity*/) {
	if (!IsPlausibleUObject(pawn)) {
		return false;
	}

	return RawApplyFlyState(pawn, location);
}

bool TryWriteControllerIgnoreInput(Classes::ATdPlayerController *controller,
                                   bool ignore) {
	if (!IsPlausibleUObject(controller)) {
		return false;
	}

	return RawWriteControllerIgnoreInput(controller, ignore);
}

bool TryGetMesh3pBoneBuffer(Classes::ATdPlayerPawn *pawn, void *&buffer,
                            int &outAtomCount) {
	buffer = nullptr;
	outAtomCount = 0;

	if (!IsPlausibleUObject(pawn)) {
		return false;
	}

	return RawGetBoneBuffer(pawn, buffer, outAtomCount);
}

bool TryGetMesh3pBoneBuffer(Classes::ATdPlayerPawn *pawn, void *&buffer) {
	int unused = 0;
	return TryGetMesh3pBoneBuffer(pawn, buffer, unused);
}

bool TryReadBufferFloat(const void *base, size_t byteOffset, float &out) {
	out = 0.f;
	if (!base) {
		return false;
	}

	const auto *ptr =
	    reinterpret_cast<const unsigned char *>(base) + byteOffset;
	return TryMemcpy(ptr, &out, sizeof(out));
}

// ── Actor transform wrappers ────────────────────────────────────────────

namespace {

bool RawReadActorLocation(Classes::AActor *actor, Classes::FVector &out) {
	__try {
		out = actor->Location;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::ReadActorLocation")) {
		return false;
	}
}

bool RawWriteActorLocation(Classes::AActor *actor,
                           const Classes::FVector &location) {
	__try {
		actor->Location = location;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::WriteActorLocation")) {
		return false;
	}
}

bool RawReadActorRotation(Classes::AActor *actor, Classes::FRotator &out) {
	__try {
		out = actor->Rotation;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::ReadActorRotation")) {
		return false;
	}
}

bool RawWriteActorRotation(Classes::AActor *actor,
                           const Classes::FRotator &rotation) {
	__try {
		actor->Rotation = rotation;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::WriteActorRotation")) {
		return false;
	}
}

bool RawReadSkeletalMeshComponent(
    Classes::ASkeletalMeshActorSpawnable *actor,
    Classes::USkeletalMeshComponent *&out) {
	__try {
		out = actor->SkeletalMeshComponent;
		return out != nullptr;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::ReadSkeletalMeshComponent")) {
		out = nullptr;
		return false;
	}
}

bool RawReadPawnMeshComponent(Classes::APawn *pawn,
                              Classes::USkeletalMeshComponent *&out) {
	__try {
		out = pawn->Mesh;
		return out != nullptr;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::ReadPawnMeshComponent")) {
		out = nullptr;
		return false;
	}
}

bool RawReadMeshLocalAtomsBuffer(Classes::USkeletalMeshComponent *comp,
                                 void *&outBuffer, int &outCount) {
	__try {
		const auto num = comp->LocalAtoms.Num();
		if (num <= 0) {
			return false;
		}
		auto *buf = comp->LocalAtoms.Buffer();
		if (!buf) {
			return false;
		}
		outBuffer = buf;
		outCount = num;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::ReadMeshLocalAtomsBuffer")) {
		outBuffer = nullptr;
		outCount = 0;
		return false;
	}
}

bool RawGetMeshLocalAtomsArray(Classes::USkeletalMeshComponent *comp,
                               Classes::TArray<Classes::FBoneAtom> *&outAtoms) {
	__try {
		if (comp->LocalAtoms.Num() <= 0 || !comp->LocalAtoms.Buffer()) {
			return false;
		}
		outAtoms = &comp->LocalAtoms;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::GetMeshLocalAtomsArray")) {
		outAtoms = nullptr;
		return false;
	}
}

} // namespace

bool TryReadActorLocation(Classes::AActor *actor, Classes::FVector &out) {
	out = {};
	if (!IsPlausibleUObject(actor)) {
		return false;
	}
	return RawReadActorLocation(actor, out);
}

bool TryWriteActorLocation(Classes::AActor *actor,
                           const Classes::FVector &location) {
	if (!IsPlausibleUObject(actor)) {
		return false;
	}
	return RawWriteActorLocation(actor, location);
}

bool TryReadActorRotation(Classes::AActor *actor, Classes::FRotator &out) {
	out = {};
	if (!IsPlausibleUObject(actor)) {
		return false;
	}
	return RawReadActorRotation(actor, out);
}

bool TryWriteActorRotation(Classes::AActor *actor,
                           const Classes::FRotator &rotation) {
	if (!IsPlausibleUObject(actor)) {
		return false;
	}
	return RawWriteActorRotation(actor, rotation);
}

bool TryReadSkeletalMeshComponent(
    Classes::ASkeletalMeshActorSpawnable *actor,
    Classes::USkeletalMeshComponent *&out) {
	out = nullptr;
	if (!IsPlausibleUObject(actor)) {
		return false;
	}
	return RawReadSkeletalMeshComponent(actor, out);
}

bool TryReadPawnMeshComponent(Classes::APawn *pawn,
                              Classes::USkeletalMeshComponent *&out) {
	out = nullptr;
	if (!IsPlausibleUObject(pawn)) {
		return false;
	}
	return RawReadPawnMeshComponent(pawn, out);
}

bool TryReadMeshLocalAtomsBuffer(Classes::USkeletalMeshComponent *comp,
                                 void *&outBuffer, int &outCount) {
	outBuffer = nullptr;
	outCount = 0;
	if (!IsPlausibleUObject(comp)) {
		return false;
	}
	return RawReadMeshLocalAtomsBuffer(comp, outBuffer, outCount);
}

bool TryGetMeshLocalAtomsArray(Classes::USkeletalMeshComponent *comp,
                               Classes::TArray<Classes::FBoneAtom> *&outAtoms) {
	outAtoms = nullptr;
	if (!IsPlausibleUObject(comp)) {
		return false;
	}
	return RawGetMeshLocalAtomsArray(comp, outAtoms);
}

// ── Checkpoint / Stashpoint ──────────────────────────────────────────────

namespace {

bool RawReadCheckpoint(Classes::ATdCheckpoint *checkpoint,
                       CheckpointSnapshot &out) {
	__try {
		out.location = checkpoint->Location;
		out.rotation = checkpoint->Rotation;
		out.checkpointId = checkpoint->CheckpointWeight;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::ReadCheckpoint")) {
		return false;
	}
}

bool RawReadStashpoint(Classes::ATdStashpoint *stash,
                       StashpointSnapshot &out) {
	__try {
		out.location = stash->Location;
		out.rotation = stash->Rotation;
		out.stashPointId = stash->StashPointID;
		out.territoryOfTeam = stash->TerritoryOfTeam;
		out.stashDuration = stash->StashDuration;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::ReadStashpoint")) {
		return false;
	}
}

bool RawReadNavigationPoint(Classes::ANavigationPoint *nav,
                            NavigationPointSnapshot &out) {
	__try {
		out.location = nav->Location;
		out.bBlocked = nav->bBlocked;
		out.bEndPoint = nav->bEndPoint;
		out.bDestinationOnly = nav->bDestinationOnly;
		out.bSourceOnly = nav->bSourceOnly;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::ReadNavPoint")) {
		return false;
	}
}

bool RawReadTdMapInfo(Classes::UTdMapInfo *mapInfo, TdMapInfoSnapshot &out) {
	__try {
		out.worldToMiniMapOrigo = mapInfo->WorldToMiniMapOrigo;
		out.mapSpecificWidgetScale = mapInfo->MapSpecificWidgetScale;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gameplay::ReadMapInfo")) {
		return false;
	}
}

} // namespace

bool TryReadTdCheckpoint(Classes::ATdCheckpoint *checkpoint,
                         CheckpointSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(checkpoint) ||
	    !TryIsA(checkpoint, Classes::ATdCheckpoint::StaticClass())) {
		return false;
	}
	if (!RawReadCheckpoint(checkpoint, out)) {
		return false;
	}
	out.valid = true;
	return true;
}

bool TryReadTdStashpoint(Classes::ATdStashpoint *stash,
                         StashpointSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(stash) ||
	    !TryIsA(stash, Classes::ATdStashpoint::StaticClass())) {
		return false;
	}
	if (!RawReadStashpoint(stash, out)) {
		return false;
	}
	out.valid = true;
	return true;
}

bool TryReadNavigationPoint(Classes::ANavigationPoint *nav,
                            NavigationPointSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(nav) ||
	    !TryIsA(nav, Classes::ANavigationPoint::StaticClass())) {
		return false;
	}
	if (!RawReadNavigationPoint(nav, out)) {
		return false;
	}
	out.valid = true;
	return true;
}

bool TryReadTdMapInfo(Classes::UTdMapInfo *mapInfo, TdMapInfoSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(mapInfo) ||
	    !TryIsA(mapInfo, Classes::UTdMapInfo::StaticClass())) {
		return false;
	}
	if (!RawReadTdMapInfo(mapInfo, out)) {
		return false;
	}
	out.valid = true;
	return true;
}

bool TryFastTraceClear(Classes::AActor *ctx, const Classes::FVector &start,
                       const Classes::FVector &end, bool &outClear) {
	outClear = false;
	if (!IsPlausibleUObject(ctx)) {
		return false;
	}
	static Classes::UFunction *fn = nullptr;
	if (!fn) {
		fn = Classes::UObject::FindObject<Classes::UFunction>(
		    "Function Engine.Actor.FastTrace");
	}
	if (!fn) {
		return false;
	}
	Classes::AActor_FastTrace_Params params = {};
	params.TraceEnd = end;
	params.TraceStart = start;
	params.BoxExtent = {};
	params.bTraceBullet = false;
	params.ReturnValue = false;
	if (!TryProcessEvent(ctx, fn, &params)) {
		return false;
	}
	outClear = params.ReturnValue;
	return true;
}

bool TryWorldTrace(Classes::AActor *ctx, const Classes::FVector &start,
                   const Classes::FVector &end, bool &outHit,
                   Classes::FVector &outHitLocation) {
	outHit = false;
	outHitLocation = {};
	if (!IsPlausibleUObject(ctx)) {
		return false;
	}
	static Classes::UFunction *fn = nullptr;
	if (!fn) {
		fn = Classes::UObject::FindObject<Classes::UFunction>(
		    "Function Engine.Actor.Trace");
	}
	if (!fn) {
		return false;
	}
	Classes::AActor_Trace_Params params = {};
	params.HitLocation = {};
	params.HitNormal = {};
	params.TraceEnd = end;
	params.TraceStart = start;
	params.bTraceActors = false;
	params.Extent = {};
	params.HitInfo = {};
	params.ExtraTraceFlags = 0;
	params.ReturnValue = nullptr;
	if (!TryProcessEvent(ctx, fn, &params)) {
		return false;
	}
	// Hit when Trace returns an Actor, or HitLocation moved off the end point.
	const float dx = params.HitLocation.X - end.X;
	const float dy = params.HitLocation.Y - end.Y;
	const float dz = params.HitLocation.Z - end.Z;
	const bool locMoved = (dx * dx + dy * dy + dz * dz) > 1.0f;
	if (params.ReturnValue != nullptr || locMoved) {
		outHit = true;
		outHitLocation = params.HitLocation;
	}
	return true;
}

bool TryClampActorTarget(Classes::AActor *ctx, const Classes::FVector &from,
                         const Classes::FVector &desired,
                         Classes::FVector &outClamped, float upOffset,
                         float downDist, bool &outWallHit, bool &outFloorHit) {
	outClamped = desired;
	outWallHit = false;
	outFloorHit = false;
	if (!IsPlausibleUObject(ctx) || upOffset < 1.0f || downDist < 1.0f) {
		return false;
	}

	// Wall: horizontal FastTrace at from.Z — blocked => keep from.XY.
	Classes::FVector wallEnd = {desired.X, desired.Y, from.Z};
	Classes::FVector wallStart = {from.X, from.Y, from.Z};
	const float hdx = wallEnd.X - wallStart.X;
	const float hdy = wallEnd.Y - wallStart.Y;
	if ((hdx * hdx + hdy * hdy) > 4.0f) {
		bool clear = true;
		if (TryFastTraceClear(ctx, wallStart, wallEnd, clear) && !clear) {
			outWallHit = true;
			outClamped.X = from.X;
			outClamped.Y = from.Y;
		}
	}

	// Floor: down Trace from above desired XY (after wall clamp).
	Classes::FVector floorStart = {outClamped.X, outClamped.Y,
	                               outClamped.Z + upOffset};
	Classes::FVector floorEnd = {outClamped.X, outClamped.Y,
	                             outClamped.Z - downDist};
	bool hit = false;
	Classes::FVector hitLoc = {};
	if (TryWorldTrace(ctx, floorStart, floorEnd, hit, hitLoc) && hit) {
		outFloorHit = true;
		outClamped.Z = hitLoc.Z + 2.0f;
	}
	return true;
}

} // namespace Gameplay
} // namespace Safe
} // namespace MeSdk
