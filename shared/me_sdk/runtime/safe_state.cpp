#include "safe_state.h"

#include "init.h"
#include "safe_seh.h"

#include <Windows.h>

namespace MeSdk {
namespace Safe {
namespace State {

namespace {

// ── Raw PRI reads ────────────────────────────────────────────────────────

bool RawReadPlayerReplicationInfo(Classes::APlayerReplicationInfo *pri,
                                  PlayerReplicationInfoSnapshot &out) {
	__try {
		out.score = pri->Score;
		out.deaths = pri->Deaths;
		out.ping = static_cast<int>(pri->Ping);
		out.playerId = pri->PlayerId;
		out.bIsSpectator = pri->bIsSpectator;
		out.bReadyToPlay = pri->bReadyToPlay;
		out.bBot = pri->bBot;
		out.bIsFemale = pri->bIsFemale;
		out.kills = pri->Kills;
		out.teamId = pri->TeamId;
		out.exactPing = pri->ExactPing;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_state::PRI")) {
		return false;
	}
}

bool RawReadTdPlayerReplicationInfo(Classes::ATdPlayerReplicationInfo *pri,
                                    TdPlayerReplicationInfoSnapshot &out) {
	__try {
		out.clientVersion = pri->ClientVersion;
		out.roleIndexInTeam = pri->RoleIndexInTeam;
		out.bPlayerIsReady = pri->bPlayerIsReady;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_state::TdPRI")) {
		return false;
	}
}

// ── Raw TeamInfo reads ───────────────────────────────────────────────────

bool RawReadTeamInfo(Classes::ATeamInfo *team, TeamInfoSnapshot &out) {
	__try {
		out.teamIndex = team->TeamIndex;
		out.score = team->Score;
		out.size = team->Size;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_state::TeamInfo")) {
		return false;
	}
}

bool RawReadTdTeamInfo(Classes::ATdTeamInfo *team, TdTeamInfoSnapshot &out) {
	__try {
		out.maxTeamMembers = team->MaxTeamMembers;
		out.bCanSeeBag = team->bCanSeeBag;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_state::TdTeamInfo")) {
		return false;
	}
}

// ── Raw GRI reads ────────────────────────────────────────────────────────

bool RawReadGameReplicationInfo(Classes::AGameReplicationInfo *gri,
                                GameReplicationInfoSnapshot &out) {
	__try {
		out.bMatchHasBegun = gri->bMatchHasBegun;
		out.bMatchIsOver = gri->bMatchIsOver;
		out.remainingTime = gri->RemainingTime;
		out.elapsedTime = gri->ElapsedTime;
		out.goalScore = gri->GoalScore;
		out.timeLimit = gri->TimeLimit;
		out.maxLives = gri->MaxLives;
		out.matchId = gri->MatchID;
		out.teamCount = static_cast<int>(gri->Teams.Num());
		out.playerCount = static_cast<int>(gri->PRIArray.Num());
		out.inactivePlayerCount = static_cast<int>(gri->InactivePRIArray.Num());
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_state::GRI")) {
		return false;
	}
}

bool RawReadTdGameReplicationInfo(Classes::ATdGameReplicationInfo *gri,
                                  TdGameReplicationInfoSnapshot &out) {
	__try {
		out.serverVersion = gri->ServerVersion;
		out.bLobbyFinalized = gri->bLobbyFinalized;
		out.bMatchIsInWarmup = gri->bMatchIsInWarmup;
		out.roundCount = gri->RoundCount;
		return true;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_state::TdGRI")) {
		return false;
	}
}

} // namespace

// ── Public Try* wrappers (two-layer: IsPlausibleUObject + Raw) ───────────

bool TryReadPlayerReplicationInfo(Classes::APlayerReplicationInfo *pri,
                                  PlayerReplicationInfoSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(pri) ||
	    !TryIsA(pri, Classes::APlayerReplicationInfo::StaticClass())) {
		return false;
	}
	if (!RawReadPlayerReplicationInfo(pri, out)) {
		return false;
	}
	out.valid = true;
	return true;
}

bool TryReadTdPlayerReplicationInfo(Classes::ATdPlayerReplicationInfo *pri,
                                    TdPlayerReplicationInfoSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(pri) ||
	    !TryIsA(pri, Classes::ATdPlayerReplicationInfo::StaticClass())) {
		return false;
	}
	if (!RawReadPlayerReplicationInfo(pri, out.base) ||
	    !RawReadTdPlayerReplicationInfo(pri, out)) {
		return false;
	}
	out.base.valid = true;
	out.valid = true;
	return true;
}

bool TryReadTeamInfo(Classes::ATeamInfo *team, TeamInfoSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(team) ||
	    !TryIsA(team, Classes::ATeamInfo::StaticClass())) {
		return false;
	}
	if (!RawReadTeamInfo(team, out)) {
		return false;
	}
	out.valid = true;
	return true;
}

bool TryReadTdTeamInfo(Classes::ATdTeamInfo *team, TdTeamInfoSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(team) ||
	    !TryIsA(team, Classes::ATdTeamInfo::StaticClass())) {
		return false;
	}
	if (!RawReadTeamInfo(team, out.base) ||
	    !RawReadTdTeamInfo(team, out)) {
		return false;
	}
	out.base.valid = true;
	out.valid = true;
	return true;
}

bool TryReadGameReplicationInfo(Classes::AGameReplicationInfo *gri,
                                GameReplicationInfoSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(gri) ||
	    !TryIsA(gri, Classes::AGameReplicationInfo::StaticClass())) {
		return false;
	}
	if (!RawReadGameReplicationInfo(gri, out)) {
		return false;
	}
	out.valid = true;
	return true;
}

bool TryReadTdGameReplicationInfo(Classes::ATdGameReplicationInfo *gri,
                                  TdGameReplicationInfoSnapshot &out) {
	out = {};
	if (!IsPlausibleUObject(gri) ||
	    !TryIsA(gri, Classes::ATdGameReplicationInfo::StaticClass())) {
		return false;
	}
	if (!RawReadGameReplicationInfo(gri, out.base) ||
	    !RawReadTdGameReplicationInfo(gri, out)) {
		return false;
	}
	out.base.valid = true;
	out.valid = true;
	return true;
}

// ── GObject-lookup helpers ───────────────────────────────────────────────

Classes::APlayerReplicationInfo *TryFindLocalPlayerReplicationInfo(
    Classes::ATdPlayerController *controller) {
	if (!IsPlausibleUObject(controller)) {
		return nullptr;
	}

	Classes::APlayerReplicationInfo *pri = nullptr;
	__try {
		pri = controller->PlayerReplicationInfo;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_state::FindLocalPRI")) {
		return nullptr;
	}

	if (!pri || !IsPlausibleUObject(pri)) {
		return nullptr;
	}

	return pri;
}

Classes::AGameReplicationInfo *TryFindGameReplicationInfo(
    Classes::AWorldInfo *world) {
	if (!IsPlausibleUObject(world)) {
		return nullptr;
	}

	Classes::AGameReplicationInfo *gri = nullptr;
	__try {
		gri = world->GRI;
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_state::FindGRI")) {
		return nullptr;
	}

	if (!gri || !IsPlausibleUObject(gri)) {
		return nullptr;
	}

	return gri;
}

} // namespace State
} // namespace Safe
} // namespace MeSdk
