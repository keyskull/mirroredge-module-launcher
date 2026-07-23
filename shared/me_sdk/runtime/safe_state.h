#pragma once

#include "safe_access.h"
#include "me_sdk/me_sdk.h"

namespace MeSdk {
namespace Safe {
namespace State {

// ── PlayerReplicationInfo ────────────────────────────────────────────────

struct PlayerReplicationInfoSnapshot {
	bool valid = false;
	float score = 0.f;
	float deaths = 0.f;
	int ping = 0;
	int playerId = -1;
	bool bIsSpectator = false;
	bool bReadyToPlay = false;
	bool bBot = false;
	bool bIsFemale = false;
	int kills = 0;
	int teamId = -1;
	float exactPing = 0.f;
};

bool TryReadPlayerReplicationInfo(Classes::APlayerReplicationInfo *pri,
                                  PlayerReplicationInfoSnapshot &out);

struct TdPlayerReplicationInfoSnapshot {
	bool valid = false;
	PlayerReplicationInfoSnapshot base = {};
	int clientVersion = 0;
	int roleIndexInTeam = -1;
	bool bPlayerIsReady = false;
};

bool TryReadTdPlayerReplicationInfo(Classes::ATdPlayerReplicationInfo *pri,
                                    TdPlayerReplicationInfoSnapshot &out);

// ── TeamInfo ─────────────────────────────────────────────────────────────

struct TeamInfoSnapshot {
	bool valid = false;
	int teamIndex = -1;
	float score = 0.f;
	int size = 0;
};

bool TryReadTeamInfo(Classes::ATeamInfo *team, TeamInfoSnapshot &out);

struct TdTeamInfoSnapshot {
	bool valid = false;
	TeamInfoSnapshot base = {};
	int maxTeamMembers = 0;
	bool bCanSeeBag = false;
};

bool TryReadTdTeamInfo(Classes::ATdTeamInfo *team, TdTeamInfoSnapshot &out);

// ── GameReplicationInfo ──────────────────────────────────────────────────

struct GameReplicationInfoSnapshot {
	bool valid = false;
	bool bMatchHasBegun = false;
	bool bMatchIsOver = false;
	int remainingTime = 0;
	int elapsedTime = 0;
	int goalScore = 0;
	int timeLimit = 0;
	int maxLives = 0;
	int matchId = 0;
	int teamCount = 0;
	int playerCount = 0;
	int inactivePlayerCount = 0;
};

bool TryReadGameReplicationInfo(Classes::AGameReplicationInfo *gri,
                                GameReplicationInfoSnapshot &out);

struct TdGameReplicationInfoSnapshot {
	bool valid = false;
	GameReplicationInfoSnapshot base = {};
	int serverVersion = 0;
	bool bLobbyFinalized = false;
	bool bMatchIsInWarmup = false;
	int roundCount = 0;
};

bool TryReadTdGameReplicationInfo(Classes::ATdGameReplicationInfo *gri,
                                  TdGameReplicationInfoSnapshot &out);

// ── GObject-lookup helpers for replication state ─────────────────────────

Classes::APlayerReplicationInfo *TryFindLocalPlayerReplicationInfo(
    Classes::ATdPlayerController *controller);
Classes::AGameReplicationInfo *TryFindGameReplicationInfo(
    Classes::AWorldInfo *world);

} // namespace State
} // namespace Safe
} // namespace MeSdk
