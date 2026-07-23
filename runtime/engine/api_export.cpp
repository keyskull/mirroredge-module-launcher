#include "engine.h"
#include "engine_core_bridge.h"
#include "mmultiplayer_api.h"

#include <cstring>
#include <string>

namespace {

static void QueueMainThreadTaskWrapper(MmMainThreadTaskFn task) {
	Engine::QueueMainThreadTask(task);
}

static void OnTickWrapper(MmTickCallbackFn callback) { Engine::OnTick(callback); }

static void OnRenderSceneWrapper(MmRenderSceneCallbackFn callback) {
	Engine::OnRenderScene(callback);
}

static void OnProcessEventWrapper(MmProcessEventCallbackFn callback) {
	Engine::OnProcessEvent(reinterpret_cast<ProcessEventCallback>(callback));
}

static void OnPreLevelLoadWrapper(MmLevelLoadCallbackFn callback) {
	Engine::OnPreLevelLoad(callback);
}

static void OnPostLevelLoadWrapper(MmLevelLoadCallbackFn callback) {
	Engine::OnPostLevelLoad(callback);
}

static void OnPreDeathWrapper(MmDeathCallbackFn callback) {
	Engine::OnPreDeath(callback);
}

static void OnPostDeathWrapper(MmDeathCallbackFn callback) {
	Engine::OnPostDeath(callback);
}

static void OnActorTickWrapper(MmActorTickCallbackFn callback) {
	Engine::OnActorTick(reinterpret_cast<ActorTickCallback>(callback));
}

static void OnBonesTickWrapper(MmBonesTickCallbackFn callback) {
	Engine::OnBonesTick(reinterpret_cast<BonesTickCallback>(callback));
}

static void OnInputWrapper(MmInputCallbackFn callback) { Engine::OnInput(callback); }

static void OnSuperInputWrapper(MmInputCallbackFn callback) {
	Engine::OnSuperInput(callback);
}

static void *GetPlayerPawnWrapper(bool update) { return Engine::GetPlayerPawn(update); }

static void *GetPlayerControllerWrapper(bool update) {
	return Engine::GetPlayerController(update);
}

static void *GetWorldWrapper(bool update) { return Engine::GetWorld(update); }

static void *GetEngineWrapper(bool update) { return Engine::GetEngine(update); }

static bool WorldToScreenWrapper(IDirect3DDevice9 *device, float *inOutX,
                                 float *inOutY, float *inOutZ) {
	if (!inOutX || !inOutY || !inOutZ) {
		return false;
	}

	Classes::FVector location{*inOutX, *inOutY, *inOutZ};
	const bool ok = Engine::WorldToScreen(device, location);
	*inOutX = location.X;
	*inOutY = location.Y;
	*inOutZ = location.Z;
	return ok;
}

static void SpawnCharacterWrapper(int character, void **spawned) {
	if (!spawned) {
		return;
	}

	Engine::SpawnCharacter(
	    static_cast<Engine::Character>(character),
	    reinterpret_cast<Classes::ASkeletalMeshActorSpawnable **>(spawned));
}

static void DespawnWrapper(void *actor) {
	Engine::Despawn(static_cast<Classes::ASkeletalMeshActorSpawnable *>(actor));
}

static void TransformBonesWrapper(int character, void *destBones, void *srcBones) {
	Engine::TransformBones(static_cast<Engine::Character>(character),
	                       static_cast<Classes::TArray<Classes::FBoneAtom> *>(destBones),
	                       static_cast<Classes::FBoneAtom *>(srcBones));
}

static const char *const *GetCharactersWrapper() { return Engine::GetCharacterNames(); }

static int GetCharacterCountWrapper() { return Engine::GetCharacterNameCount(); }

static bool GetSettingBoolWrapper(const char *section, const char *key,
                                  bool defaultValue) {
	const auto *bridge = EngineCoreBridge::Get();
	if (!bridge || !bridge->getSettingBool) {
		return defaultValue;
	}
	return bridge->getSettingBool(section, key, defaultValue);
}

static int GetSettingIntWrapper(const char *section, const char *key,
                                int defaultValue) {
	const auto *bridge = EngineCoreBridge::Get();
	if (!bridge || !bridge->getSettingInt) {
		return defaultValue;
	}
	return bridge->getSettingInt(section, key, defaultValue);
}

static void SetSettingBoolWrapper(const char *section, const char *key, bool value) {
	const auto *bridge = EngineCoreBridge::Get();
	if (bridge && bridge->setSettingBool) {
		bridge->setSettingBool(section, key, value);
	}
}

static void SetSettingIntWrapper(const char *section, const char *key, int value) {
	const auto *bridge = EngineCoreBridge::Get();
	if (bridge && bridge->setSettingInt) {
		bridge->setSettingInt(section, key, value);
	}
}

static bool GetSettingStringWrapper(const char *section, const char *key, char *out,
                                    size_t outSize, const char *defaultValue) {
	const auto *bridge = EngineCoreBridge::Get();
	if (!bridge || !bridge->getSettingString) {
		if (!out || outSize == 0) {
			return false;
		}
		const size_t n = outSize - 1;
		strncpy(out, defaultValue ? defaultValue : "", n);
		out[n] = '\0';
		return true;
	}
	return bridge->getSettingString(section, key, out, outSize, defaultValue);
}

static void SetSettingStringWrapper(const char *section, const char *key,
                                    const char *value) {
	const auto *bridge = EngineCoreBridge::Get();
	if (bridge && bridge->setSettingString) {
		bridge->setSettingString(section, key, value);
	}
}

static void ClearFeaturePluginCallbacksWrapper() {
	Engine::ClearFeaturePluginCallbacks();
}

static bool IsGameplayReadySafeWrapper() { return Engine::IsGameplayReadySafe(); }

static bool TryGetGameplayMapNameWrapper(char *out, size_t outSize) {
	return Engine::TryGetGameplayMapName(out, outSize);
}

static void RequestGameplayActivationWrapper(MmMainThreadTaskFn onActivated) {
	Engine::RequestGameplayActivation(onActivated);
}

static void CancelGameplayActivationWrapper() { Engine::CancelGameplayActivation(); }

static bool AreGameplayHooksInstalledWrapper() {
	return Engine::AreGameplayHooksInstalled();
}

static bool TryGetSeedHostPoseWrapper(float *x, float *y, float *z,
                                      unsigned short *yaw) {
	return Engine::TryGetSeedHostPose(x, y, z, yaw);
}

static unsigned long long GetIdlePcSeedAgeMsWrapper() {
	return Engine::GetIdlePcSeedAgeMs();
}

static const MmultiplayerApi g_api = {
    MMULTIPLAYER_API_VERSION,
    &QueueMainThreadTaskWrapper,
    &OnTickWrapper,
    &OnRenderSceneWrapper,
    &OnProcessEventWrapper,
    &OnPreLevelLoadWrapper,
    &OnPostLevelLoadWrapper,
    &OnPreDeathWrapper,
    &OnPostDeathWrapper,
    &OnActorTickWrapper,
    &OnBonesTickWrapper,
    &OnInputWrapper,
    &OnSuperInputWrapper,
    &Engine::BlockInput,
    &Engine::EnsureGameplayHooks,
    &Engine::CanSafelyUsePlayerPawn,
    &Engine::SetHostedGameplayLive,
    &GetPlayerPawnWrapper,
    &GetPlayerControllerWrapper,
    &GetWorldWrapper,
    &GetEngineWrapper,
    &Engine::GetWindow,
    &WorldToScreenWrapper,
    &SpawnCharacterWrapper,
    &DespawnWrapper,
    &TransformBonesWrapper,
    &GetCharactersWrapper,
    &GetCharacterCountWrapper,
    &GetSettingBoolWrapper,
    &GetSettingIntWrapper,
    &SetSettingBoolWrapper,
    &SetSettingIntWrapper,
    &GetSettingStringWrapper,
    &SetSettingStringWrapper,
    &Engine::IsHostedMode,
    &Engine::IsHostedGameplayLive,
    &Engine::IsKeyDown,
    &ClearFeaturePluginCallbacksWrapper,
    &IsGameplayReadySafeWrapper,
    &TryGetGameplayMapNameWrapper,
    &RequestGameplayActivationWrapper,
    &CancelGameplayActivationWrapper,
    &AreGameplayHooksInstalledWrapper,
    &TryGetSeedHostPoseWrapper,
    &GetIdlePcSeedAgeMsWrapper,
};

} // namespace

extern "C" __declspec(dllexport) const MmultiplayerApi *MMOD_GetMmultiplayerApi() {
	return &g_api;
}
