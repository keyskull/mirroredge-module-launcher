#pragma once

#include "safe_access.h"
#include "me_sdk/me_sdk.h"

namespace MeSdk {
namespace Safe {
namespace Gameplay {

struct PawnPoseSnapshot {
	bool valid = false;
	Classes::FVector location = {};
	Classes::FVector velocity = {};
	Classes::FRotator rotation = {};
	float targetMeshTranslationZ = 0.f;
	float health = 0.f;
	float enterFallingHeight = 0.f;
	int physics = 0;
	int movementState = 0;
};

struct FlyTickContext {
	bool valid = false;
	float rawJoyUp = 0.f;
	float rawJoyRight = 0.f;
	int controllerYaw = 0;
};

bool TryReadPawnPose(Classes::ATdPlayerPawn *pawn, PawnPoseSnapshot &out);
bool TryReadPawnHealth(Classes::ATdPlayerPawn *pawn, float &health);
bool TryReadFlyTickContext(Classes::ATdPlayerController *controller,
                           FlyTickContext &out);
bool TryApplyGodModeScalars(Classes::ATdPlayerPawn *pawn);
bool TryApplyFlyState(Classes::ATdPlayerPawn *pawn,
                      const Classes::FVector &location,
                      const Classes::FVector &velocity);
bool TryWriteControllerIgnoreInput(Classes::ATdPlayerController *controller,
                                   bool ignore);
bool TryGetMesh3pBoneBuffer(Classes::ATdPlayerPawn *pawn, void *&buffer);
// Same as above, also returns LocalAtoms.Num() (Faith Mesh3p is typically 108;
// shorter counts must not be read past when sampling CompressedBoneOffsets).
bool TryGetMesh3pBoneBuffer(Classes::ATdPlayerPawn *pawn, void *&buffer,
                            int &outAtomCount);
bool TryReadBufferFloat(const void *base, size_t byteOffset, float &out);

// Call once after InitializeGlobals (e.g. Engine::InitializeSDK). Tick must
// never call ATdPlayerPawn::StaticClass() — per-DLL cold statics hang.
void PrewarmClasses();

// Safe AActor transform access — for remote player actors (ASkeletalMeshActorSpawnable).
// These wrap direct pointer dereferences that can fault on garbage-collected actors.
bool TryReadActorLocation(Classes::AActor *actor, Classes::FVector &out);
bool TryWriteActorLocation(Classes::AActor *actor, const Classes::FVector &location);
bool TryReadActorRotation(Classes::AActor *actor, Classes::FRotator &out);
bool TryWriteActorRotation(Classes::AActor *actor, const Classes::FRotator &rotation);

// Safe skeletal mesh access for spawned remote player actors.
// Returns the USkeletalMeshComponent* without following the pointer chain unsafely.
bool TryReadSkeletalMeshComponent(Classes::ASkeletalMeshActorSpawnable *actor,
                                  Classes::USkeletalMeshComponent *&out);
// APawn::Mesh (local Faith Mesh3p sample / host bones).
bool TryReadPawnMeshComponent(Classes::APawn *pawn,
                              Classes::USkeletalMeshComponent *&out);

// Safe read of SkeletalMeshComponent::LocalAtoms.Buffer() — used by BonesTickHook.
bool TryReadMeshLocalAtomsBuffer(Classes::USkeletalMeshComponent *comp,
                                 void *&outBuffer, int &outCount);

// Pointer to LocalAtoms TArray (for Engine::TransformBones dest) — SEH-guarded.
bool TryGetMeshLocalAtomsArray(Classes::USkeletalMeshComponent *comp,
                               Classes::TArray<Classes::FBoneAtom> *&outAtoms);

// World query via Actor::FastTrace / Trace (ProcessEvent + SEH).
// FastTrace: ReturnValue true = clear path (no hit).
bool TryFastTraceClear(Classes::AActor *ctx, const Classes::FVector &start,
                       const Classes::FVector &end, bool &outClear);
// Trace world (bTraceActors=false). outHit true when a hit location is usable.
bool TryWorldTrace(Classes::AActor *ctx, const Classes::FVector &start,
                   const Classes::FVector &end, bool &outHit,
                   Classes::FVector &outHitLocation);
// Clamp desired pose: wall (horizontal FastTrace) then floor (down Trace).
// from = last written / current remote; desired = UDP target after soft-separate.
bool TryClampActorTarget(Classes::AActor *ctx, const Classes::FVector &from,
                         const Classes::FVector &desired,
                         Classes::FVector &outClamped, float upOffset,
                         float downDist, bool &outWallHit, bool &outFloorHit);

// ── Checkpoint / Stashpoint — safe spawn-point reads ────────────────────

struct CheckpointSnapshot {
	bool valid = false;
	Classes::FVector location = {};
	Classes::FRotator rotation = {};
	int checkpointId = 0;
};

bool TryReadTdCheckpoint(Classes::ATdCheckpoint *checkpoint, CheckpointSnapshot &out);

struct StashpointSnapshot {
	bool valid = false;
	Classes::FVector location = {};
	Classes::FRotator rotation = {};
	int stashPointId = 0;
	int territoryOfTeam = 0;
	float stashDuration = 0.f;
};

bool TryReadTdStashpoint(Classes::ATdStashpoint *stash, StashpointSnapshot &out);

// ── NavigationPoint — safe AI nav-point reads ───────────────────────────

struct NavigationPointSnapshot {
	bool valid = false;
	Classes::FVector location = {};
	bool bBlocked = false;
	bool bEndPoint = false;
	bool bDestinationOnly = false;
	bool bSourceOnly = false;
};

bool TryReadNavigationPoint(Classes::ANavigationPoint *nav,
                            NavigationPointSnapshot &out);

// ── MapInfo — safe minimap/map-metadata reads ───────────────────────────

struct TdMapInfoSnapshot {
	bool valid = false;
	Classes::FVector worldToMiniMapOrigo = {};
	float mapSpecificWidgetScale = 1.f;
};

bool TryReadTdMapInfo(Classes::UTdMapInfo *mapInfo, TdMapInfoSnapshot &out);

} // namespace Gameplay
} // namespace Safe
} // namespace MeSdk
