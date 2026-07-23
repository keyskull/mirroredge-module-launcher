#pragma once

#include "me_sdk/me_sdk.h"
#include "mp_engine_shim.h"
#include "mp_engine_types.h"

typedef bool (*ProcessEventCallback)(Classes::UObject *, Classes::UFunction *,
                                     void *, void *);
typedef void (*ActorTickCallback)(Classes::AActor *actor);
typedef void (*BonesTickCallback)(Classes::TArray<Classes::FBoneAtom> *atoms);

namespace Engine {

inline void QueueMainThreadTask(MmMainThreadTaskFn task) {
	MpEngineShim::Get()->QueueMainThreadTask(task);
}

inline void OnTick(MmTickCallbackFn callback) { MpEngineShim::Get()->OnTick(callback); }

inline void OnRenderScene(MmRenderSceneCallbackFn callback) {
	MpEngineShim::Get()->OnRenderScene(callback);
}

inline void OnProcessEvent(MmProcessEventCallbackFn callback) {
	MpEngineShim::Get()->OnProcessEvent(callback);
}

inline void OnProcessEvent(ProcessEventCallback callback) {
	MpEngineShim::Get()->OnProcessEvent(
	    reinterpret_cast<MmProcessEventCallbackFn>(callback));
}

inline void OnPreLevelLoad(MmLevelLoadCallbackFn callback) {
	MpEngineShim::Get()->OnPreLevelLoad(callback);
}

inline void OnPostLevelLoad(MmLevelLoadCallbackFn callback) {
	MpEngineShim::Get()->OnPostLevelLoad(callback);
}

inline void OnPreDeath(MmDeathCallbackFn callback) {
	MpEngineShim::Get()->OnPreDeath(callback);
}

inline void OnPostDeath(MmDeathCallbackFn callback) {
	MpEngineShim::Get()->OnPostDeath(callback);
}

inline void OnActorTick(MmActorTickCallbackFn callback) {
	MpEngineShim::Get()->OnActorTick(callback);
}

inline void OnActorTick(ActorTickCallback callback) {
	MpEngineShim::Get()->OnActorTick(reinterpret_cast<MmActorTickCallbackFn>(callback));
}

inline void OnBonesTick(MmBonesTickCallbackFn callback) {
	MpEngineShim::Get()->OnBonesTick(callback);
}

inline void OnBonesTick(BonesTickCallback callback) {
	MpEngineShim::Get()->OnBonesTick(reinterpret_cast<MmBonesTickCallbackFn>(callback));
}

inline void OnInput(MmInputCallbackFn callback) {
	MpEngineShim::Get()->OnInput(callback);
}

inline void OnSuperInput(MmInputCallbackFn callback) {
	MpEngineShim::Get()->OnSuperInput(callback);
}

inline void BlockInput(bool block) { MpEngineShim::Get()->BlockInput(block); }

inline bool EnsureGameplayHooks() { return MpEngineShim::Get()->EnsureGameplayHooks(); }

inline bool AreGameplayHooksInstalled() {
	const auto *api = MpEngineShim::Get();
	if (api && api->version >= 4 && api->AreGameplayHooksInstalled) {
		return api->AreGameplayHooksInstalled();
	}
	return false;
}

inline bool CanSafelyUsePlayerPawn() {
	return MpEngineShim::Get()->CanSafelyUsePlayerPawn();
}

inline void SetHostedGameplayLive(bool live) {
	MpEngineShim::Get()->SetHostedGameplayLive(live);
}

inline bool IsHostedMode() { return MpEngineShim::Get()->IsHostedMode(); }

inline bool IsHostedGameplayLive() {
	return MpEngineShim::Get()->IsHostedGameplayLive();
}

inline bool IsKeyDown(int keycode) { return MpEngineShim::Get()->IsKeyDown(keycode); }

inline void ClearFeaturePluginCallbacks() {
	const auto *api = MpEngineShim::Get();
	if (api && api->version >= 2 && api->ClearFeaturePluginCallbacks) {
		api->ClearFeaturePluginCallbacks();
	}
}

inline Classes::ATdPlayerPawn *GetPlayerPawn(bool update = false) {
	return static_cast<Classes::ATdPlayerPawn *>(MpEngineShim::Get()->GetPlayerPawn(update));
}

inline Classes::ATdPlayerController *GetPlayerController(bool update = false) {
	return static_cast<Classes::ATdPlayerController *>(
	    MpEngineShim::Get()->GetPlayerController(update));
}

inline Classes::AWorldInfo *GetWorld(bool update = false) {
	return static_cast<Classes::AWorldInfo *>(MpEngineShim::Get()->GetWorld(update));
}

inline Classes::UTdGameEngine *GetEngine(bool update = false) {
	return static_cast<Classes::UTdGameEngine *>(MpEngineShim::Get()->GetEngine(update));
}

inline HWND GetWindow() { return MpEngineShim::Get()->GetWindow(); }

inline bool WorldToScreen(IDirect3DDevice9 *device, Classes::FVector &inOutLocation) {
	return MpEngineShim::Get()->WorldToScreen(device, &inOutLocation.X, &inOutLocation.Y,
	                                          &inOutLocation.Z);
}

inline void SpawnCharacter(Character character,
                           Classes::ASkeletalMeshActorSpawnable *&spawned) {
	MpEngineShim::Get()->SpawnCharacter(
	    static_cast<int>(character),
	    reinterpret_cast<void **>(&spawned));
}

inline void Despawn(Classes::ASkeletalMeshActorSpawnable *actor) {
	MpEngineShim::Get()->Despawn(actor);
}

inline void CancelGameplayActivation() {
	const auto *api = MpEngineShim::Get();
	if (api && api->version >= 3 && api->CancelGameplayActivation) {
		api->CancelGameplayActivation();
		return;
	}
	SetHostedGameplayLive(false);
}

inline bool IsGameplayReadySafe() {
	const auto *api = MpEngineShim::Get();
	if (api && api->version >= 3 && api->IsGameplayReadySafe) {
		return api->IsGameplayReadySafe();
	}
	return CanSafelyUsePlayerPawn();
}

inline bool TryGetGameplayMapName(char *out, size_t outSize) {
	const auto *api = MpEngineShim::Get();
	if (api && api->version >= 3 && api->TryGetGameplayMapName) {
		return api->TryGetGameplayMapName(out, outSize);
	}
	if (out && outSize > 0) {
		out[0] = '\0';
	}
	return false;
}

inline void RequestGameplayActivation(MmMainThreadTaskFn onActivated = nullptr) {
	const auto *api = MpEngineShim::Get();
	if (api && api->version >= 3 && api->RequestGameplayActivation) {
		api->RequestGameplayActivation(onActivated);
		return;
	}
	SetHostedGameplayLive(true);
	if (onActivated) {
		onActivated();
	}
}

inline void TransformBones(Character character,
                           Classes::TArray<Classes::FBoneAtom> *dest,
                           Classes::FBoneAtom *src) {
	MpEngineShim::Get()->TransformBones(static_cast<int>(character), dest, src);
}

inline bool TryGetSeedHostPose(float *x, float *y, float *z,
                               unsigned short *yaw) {
	const auto *api = MpEngineShim::Get();
	if (!api || api->version < 6 || !api->TryGetSeedHostPose) {
		return false;
	}
	return api->TryGetSeedHostPose(x, y, z, yaw);
}

inline unsigned long long GetIdlePcSeedAgeMs() {
	const auto *api = MpEngineShim::Get();
	if (!api || api->version < 6 || !api->GetIdlePcSeedAgeMs) {
		return 0;
	}
	return api->GetIdlePcSeedAgeMs();
}

} // namespace Engine
