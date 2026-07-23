#pragma once

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <d3d9.h>

#include "engine.h"
#include "me_sdk/me_sdk.h"

namespace EngineInternal {

using LoadLibraryAFn = HMODULE(WINAPI *)(const char *);

extern LoadLibraryAFn LoadLibraryAOriginal;

struct CommandsState {
	std::vector<std::wstring> Queue;
	std::mutex Mutex;
};

struct SpawnsState {
	std::vector<
	    std::pair<Engine::Character, Classes::ASkeletalMeshActorSpawnable **>>
	    Queue;
	std::mutex Mutex;
};

struct ProcessEventState {
	std::vector<ProcessEventCallback> Callbacks;
	std::mutex Mutex;
	std::atomic<int> CallbackCount{0};
	int(__thiscall *Original)(Classes::UObject *, Classes::UFunction *, void *,
	                          void *) = nullptr;
};

struct LevelLoadState {
	bool Loading = false;
	ULONGLONG loadingStartTick = 0;
	void *Base = nullptr;
	std::vector<LevelLoadCallback> PreCallbacks;
	std::vector<LevelLoadCallback> PostCallbacks;
	std::mutex Mutex;
	int(__thiscall *Original)(void *, void *, unsigned long long arg) = nullptr;
};

struct DeathState {
	void *PreBase = nullptr;
	void *PostBase = nullptr;
	std::vector<DeathCallback> PreCallbacks;
	std::vector<DeathCallback> PostCallbacks;
	std::mutex Mutex;
	int (*PreOriginal)() = nullptr;
	int (*PostOriginal)() = nullptr;
};

struct ActorTickState {
	std::vector<ActorTickCallback> Callbacks;
	std::mutex Mutex;
	std::atomic<int> CallbackCount{0};
	void *(__thiscall *Original)(Classes::AActor *, void *) = nullptr;
};

struct BonesTickState {
	std::vector<BonesTickCallback> Callbacks;
	std::mutex Mutex;
	std::atomic<int> CallbackCount{0};
	void *(__thiscall *Original)(void *, void *) = nullptr;
};

struct ProjectionTickState {
	D3DXMATRIX *Matrix = nullptr;
	int *(__thiscall *Original)(Classes::FMatrix *, void *) = nullptr;
};

struct TickState {
	std::vector<TickCallback> Callbacks;
	std::mutex Mutex;
	void(__thiscall *Original)(float *, int, float) = nullptr;
};

extern CommandsState commands;
extern SpawnsState spawns;
extern ProcessEventState processEvent;
extern LevelLoadState levelLoad;
extern DeathState death;
extern ActorTickState actorTick;
extern BonesTickState bonesTick;
extern ProjectionTickState projectionTick;
extern TickState tick;

extern std::atomic<bool> modReady;
extern std::atomic<bool> modInitializing;
extern std::atomic<bool> hostedMode;
extern std::atomic<bool> hostedGameplayLive;
extern std::atomic<bool> gameplayHooksInstalled;

Classes::ASkeletalMeshActorSpawnable *SpawnCharacter(Engine::Character character);

// Always-on freeze breadcrumb. Writes a rolling ring of "phase" markers into a
// memory-mapped file (%TEMP%\mirroredge-phase.bin) with no per-call syscalls.
// When the game hangs, the watchdog reads the ring; the last main-thread entry
// pinpoints the stuck function without a debugger. Only instrument main/game
// and EndScene paths — never high-frequency background threads.
void SetPhase(const char *phase);

// Soft-hide remote spawnables queued by Engine::Despawn — Tick thread only.
void DrainSoftDespawnQueue();

int __fastcall ProcessEventHook(Classes::UObject *object, void *idle,
                                Classes::UFunction *function, void *args,
                                void *result);
int __fastcall LevelLoadHook(void *this_, void *idle, void **levelInfo,
                             unsigned long long arg);
int PreDeathHook();
int PostDeathHook();
HMODULE WINAPI LoadLibraryAHook(const char *module);
void *__fastcall ActorTickHook(Classes::AActor *actor, void *idle, void *arg);
void *__fastcall BonesTickHook(void *this_, void *idle, void *arg);
int *__fastcall ProjectionTick(Classes::FMatrix *matrix, void *idle, void *arg);
void __fastcall TickHook(float *scales, void *idle, int arg, float delta);

bool EnsurePeekMessageHookForGameplay();
bool InstallGameplayHooksInternal();
void InstallGameplayHooksOnMainThread();
bool WaitForGameplayHooks(DWORD timeoutMs);

// Set while MMOD_EngineDrainSpawnQueue runs — GetPlayerController(false) must
// not touch GamePlayers on this path (EndScene hang after TdEngine warm).
extern std::atomic<bool> inEndSceneSpawnDrain;

// EndScene must never read GamePlayers — queue PC/world seed for TickHook.
extern std::atomic<bool> pendingIdlePcSeed;

void RequestIdlePcSeed();
void DrainPendingIdlePcSeedOnGameThread();
bool IsIdlePcSeedQuietPeriod();
void CommitIdleWarmWorldAndPoseFromPc();
void WarmTdGameEngineOnGameThread();

} // namespace EngineInternal
