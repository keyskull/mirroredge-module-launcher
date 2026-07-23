#pragma once

#include <d3d9.h>
#include <string>
#include <vector>

#include "safe_access.h"
#include "me_sdk/me_sdk.h"
#include "safe_gui_invoke.h"

namespace MeSdk {
namespace Safe {
namespace Gui {

struct PlayerOverlayInfo {
	bool valid = false;
	float posX = 0.f;
	float posY = 0.f;
	float posZ = 0.f;
	float velocity2D = 0.f;
	float rotPitchDeg = 0.f;
	float rotYawDeg = 0.f;
	int movementState = 0;
};

struct EngineMenuState {
	bool valid = false;
	bool smoothFrameRate = false;
	float minSmoothedFrameRate = 0.f;
	float maxSmoothedFrameRate = 0.f;
	bool hasClient = false;
	float displayGamma = 0.f;
};

struct StreamingLevelEntry {
	std::string packageName;
	bool shouldBeLoaded = false;
	Classes::ULevelStreaming *level = nullptr;
};

struct WorldMenuState {
	bool valid = false;
	bool inMainMenu = true;
	float timeDilation = 1.f;
	float worldGravityZ = 0.f;
	std::vector<StreamingLevelEntry> streamingLevels;
};

struct DollyGuiContext {
	bool valid = false;
	Classes::FVector location = {};
	Classes::FRotator rotation = {};
	float fov = 0.f;
};

bool TryReadPlayerOverlay(Classes::ATdPlayerPawn *pawn,
                          Classes::ATdPlayerController *controller,
                          PlayerOverlayInfo &out);

bool TryReadEngineMenuState(Classes::UTdGameEngine *engine, EngineMenuState &out);
bool TryWriteEngineSmoothFrameRate(Classes::UTdGameEngine *engine, bool enabled);
bool TryWriteEngineFrameRateLimits(Classes::UTdGameEngine *engine, float minFps,
                                   float maxFps);
bool TryWriteClientGamma(Classes::UTdGameEngine *engine, float gamma);

bool TryReadWorldMenuState(Classes::ATdPlayerController *controller,
                           Classes::AWorldInfo *world, WorldMenuState &out);
bool TryWriteWorldScalars(Classes::AWorldInfo *world, float timeDilation,
                          float gravityZ);
bool TrySetStreamingLevelLoaded(Classes::ULevelStreaming *level, bool loaded);

Classes::ATdPlayerController *TryFindTdPlayerController(bool refresh = false);
Classes::AWorldInfo *TryFindActiveWorldInfo(bool refresh = false);

bool TryReadDollyGuiContext(Classes::ATdPlayerPawn *pawn,
                            Classes::ATdPlayerController *controller,
                            DollyGuiContext &out);
bool TryWriteCameraFov(Classes::ATdPlayerController *controller, float fov);

// refresh=false is cache-only (never walks GObjects). refresh=true does a full
// scan — use only from idle UI paths (e.g. Engine tab), never EndScene/Tick.
Classes::UTdGameEngine *TryFindTdGameEngine(bool refresh = false);
// Call once after InitializeGlobals (e.g. Engine::InitializeSDK) so EndScene
// spawn warm never pays FindClass. Safe from init; not from EndScene/Tick.
void PrewarmTdGameEngineClass();
// Call once after InitializeGlobals with PrewarmTdGameEngineClass.
void PrewarmWorldInfoClass();
// Seed cache from a PlayerController (LocalPlayer->Outer == TdGameEngine).
// O(1); call from EndScene/Tick before incremental warm when a PC is known.
bool TrySeedTdGameEngineFromPlayerController(Classes::APlayerController *pc);
// SEH-only GamePlayers[0]->Actor read (no VirtualQuery). EndScene
// CommitIdleWarmPlayerSeed only — never from Tick (concurrent hang).
bool TrySeedPlayerControllerFromTdEngineGamePlayers(
    Classes::UTdGameEngine *engine, Classes::ATdPlayerController *&outPc);
// Spread GObjects discovery across frames. Returns true once the cache is set.
// Safe to call from EndScene/Tick. Uses Name-index filter (cheap) then IsA.
bool TryWarmTdGameEngineIncremental(int maxVisit);
// Same incremental pattern for AWorldInfo that owns a TdPlayerController.
bool TryWarmActiveWorldInfoIncremental(int maxVisit);
Classes::UTdGameViewportClient *TryFindTdViewportClient(bool refresh = false);
Classes::UPlayerInput *TryFindLocalPlayerInput();

bool TryQueryViewportSize(Classes::UTdGameViewportClient *viewport, int &width,
                          int &height);
bool TryRunSetResCommand(Classes::UTdGameViewportClient *viewport, int width,
                         int height);

// Safe world map name read — wraps GetMapName virtual call with SEH guard.
// world->GetMapName() is a virtual call that can access corrupted vtable
// after garbage collection; this prevents the ACCESS_VIOLATION crash.
bool TryGetWorldMapName(Classes::AWorldInfo *world, std::string &out);

} // namespace Gui
} // namespace Safe
} // namespace MeSdk
