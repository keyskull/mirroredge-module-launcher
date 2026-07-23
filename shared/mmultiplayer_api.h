#pragma once

#include <Windows.h>

#include <d3d9.h>

#define MMULTIPLAYER_API_VERSION 7U
#define MMULTIPLAYER_API_EXPORT "MMOD_GetMmultiplayerApi"

struct IDirect3DDevice9;

typedef void (*MmMainThreadTaskFn)();
typedef void (*MmTickCallbackFn)(float delta);
typedef void (*MmRenderSceneCallbackFn)(IDirect3DDevice9 *device);
typedef bool (*MmProcessEventCallbackFn)(void *object, void *function,
                                         void *params, void *result);
typedef void (*MmLevelLoadCallbackFn)(const wchar_t *levelName);
typedef void (*MmDeathCallbackFn)();
typedef void (*MmActorTickCallbackFn)(void *actor);
typedef void (*MmBonesTickCallbackFn)(void *bones);
typedef void (*MmInputCallbackFn)(unsigned int &message, int keycode);

struct MmultiplayerApi {
	unsigned version;
	void (*QueueMainThreadTask)(MmMainThreadTaskFn task);
	void (*OnTick)(MmTickCallbackFn callback);
	void (*OnRenderScene)(MmRenderSceneCallbackFn callback);
	void (*OnProcessEvent)(MmProcessEventCallbackFn callback);
	void (*OnPreLevelLoad)(MmLevelLoadCallbackFn callback);
	void (*OnPostLevelLoad)(MmLevelLoadCallbackFn callback);
	void (*OnPreDeath)(MmDeathCallbackFn callback);
	void (*OnPostDeath)(MmDeathCallbackFn callback);
	void (*OnActorTick)(MmActorTickCallbackFn callback);
	void (*OnBonesTick)(MmBonesTickCallbackFn callback);
	void (*OnInput)(MmInputCallbackFn callback);
	void (*OnSuperInput)(MmInputCallbackFn callback);
	void (*BlockInput)(bool block);
	bool (*EnsureGameplayHooks)();
	bool (*CanSafelyUsePlayerPawn)();
	void (*SetHostedGameplayLive)(bool live);
	void *(*GetPlayerPawn)(bool update);
	void *(*GetPlayerController)(bool update);
	void *(*GetWorld)(bool update);
	void *(*GetEngine)(bool update);
	HWND (*GetWindow)();
	bool (*WorldToScreen)(IDirect3DDevice9 *device, float *inOutX, float *inOutY,
	                      float *inOutZ);
	void (*SpawnCharacter)(int character, void **spawned);
	void (*Despawn)(void *actor);
	void (*TransformBones)(int character, void *destBones, void *srcBones);
	const char *const *(*GetCharacters)();
	int (*GetCharacterCount)();
	bool (*GetSettingBool)(const char *section, const char *key, bool defaultValue);
	int (*GetSettingInt)(const char *section, const char *key, int defaultValue);
	void (*SetSettingBool)(const char *section, const char *key, bool value);
	void (*SetSettingInt)(const char *section, const char *key, int value);
	bool (*GetSettingString)(const char *section, const char *key, char *out,
	                         size_t outSize, const char *defaultValue);
	void (*SetSettingString)(const char *section, const char *key,
	                         const char *value);
	bool (*IsHostedMode)();
	bool (*IsHostedGameplayLive)();
	bool (*IsKeyDown)(int keycode);
	void (*ClearFeaturePluginCallbacks)();
	bool (*IsGameplayReadySafe)();
	bool (*TryGetGameplayMapName)(char *out, size_t outSize);
	void (*RequestGameplayActivation)(MmMainThreadTaskFn onActivated);
	void (*CancelGameplayActivation)();
	bool (*AreGameplayHooksInstalled)();
	// v6: host pose sampled during EndScene GamePlayers seed — Tick must not
	// read PC->PlayerCamera immediately after (hang after idle.done).
	bool (*TryGetSeedHostPose)(float *x, float *y, float *z, unsigned short *yaw);
	unsigned long long (*GetIdlePcSeedAgeMs)();
};

typedef const MmultiplayerApi *(__cdecl *MMOD_GetMmultiplayerApiFn)();
