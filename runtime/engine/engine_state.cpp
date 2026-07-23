#include "engine_internal.h"

namespace EngineInternal {

LoadLibraryAFn LoadLibraryAOriginal = nullptr;

CommandsState commands;
SpawnsState spawns;
ProcessEventState processEvent;
LevelLoadState levelLoad;
DeathState death;
ActorTickState actorTick;
BonesTickState bonesTick;
ProjectionTickState projectionTick;
TickState tick;

std::atomic<bool> modReady{false};
std::atomic<bool> modInitializing{false};
std::atomic<bool> hostedMode{false};
std::atomic<bool> hostedGameplayLive{false};
std::atomic<bool> gameplayHooksInstalled{false};

} // namespace EngineInternal
