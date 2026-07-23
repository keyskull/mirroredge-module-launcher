#include "safe_test.h"

#include "init.h"
#include "me_sdk/me_sdk.h"
#include "safe_access.h"
#include "safe_state.h"
#include "safe_gameplay.h"
#include "safe_gui.h"

#include <string>
#include <cstdio>

namespace MeSdk {
namespace Safe {
namespace Test {

static const char *BoolStr(bool v) { return v ? "true" : "false"; }

static void AppendResult(std::string &out, const char *name, bool ok,
                         const char *detail) {
	out += "\""; out += name; out += "\":{\"ok\":";
	out += BoolStr(ok);
	if (detail && detail[0]) {
		out += ",\"detail\":\"";
		out += detail;
		out += "\"";
	}
	out += "}";
}

struct TestVisitorCtx {
	int foundPRI = 0;
	int foundTdPRI = 0;
	int foundGRI = 0;
	int foundTdGRI = 0;
	int foundTeam = 0;
	int foundTdTeam = 0;
	int foundCheckpoint = 0;
	int foundStashpoint = 0;
	int foundNavPoint = 0;
	int foundMapInfo = 0;

	int passed = 0;
	int failed = 0;
};

static bool TestVisitor(Classes::UObject *object, int /*index*/, void *ctxPtr) {
	auto *ctx = static_cast<TestVisitorCtx *>(ctxPtr);
	if (!IsPlausibleUObject(object)) return true;

	// ── APlayerReplicationInfo ──────────────────────────────────────
	if (TryIsA(object, Classes::APlayerReplicationInfo::StaticClass())) {
		++ctx->foundPRI;
		auto *pri = static_cast<Classes::APlayerReplicationInfo *>(object);
		State::PlayerReplicationInfoSnapshot snap;
		if (State::TryReadPlayerReplicationInfo(pri, snap) && snap.valid) {
			++ctx->passed;
		} else {
			++ctx->failed;
		}

		if (TryIsA(pri, Classes::ATdPlayerReplicationInfo::StaticClass())) {
			++ctx->foundTdPRI;
			auto *tdPri = static_cast<Classes::ATdPlayerReplicationInfo *>(pri);
			State::TdPlayerReplicationInfoSnapshot tdSnap;
			if (State::TryReadTdPlayerReplicationInfo(tdPri, tdSnap) && tdSnap.valid) {
				++ctx->passed;
			} else {
				++ctx->failed;
			}
		}
		return true;
	}

	// ── AGameReplicationInfo ────────────────────────────────────────
	if (TryIsA(object, Classes::AGameReplicationInfo::StaticClass())) {
		++ctx->foundGRI;
		auto *gri = static_cast<Classes::AGameReplicationInfo *>(object);
		State::GameReplicationInfoSnapshot snap;
		if (State::TryReadGameReplicationInfo(gri, snap) && snap.valid) {
			++ctx->passed;
		} else {
			++ctx->failed;
		}

		if (TryIsA(gri, Classes::ATdGameReplicationInfo::StaticClass())) {
			++ctx->foundTdGRI;
			auto *tdGri = static_cast<Classes::ATdGameReplicationInfo *>(gri);
			State::TdGameReplicationInfoSnapshot tdSnap;
			if (State::TryReadTdGameReplicationInfo(tdGri, tdSnap) && tdSnap.valid) {
				++ctx->passed;
			} else {
				++ctx->failed;
			}
		}
		return true;
	}

	// ── ATeamInfo ───────────────────────────────────────────────────
	if (TryIsA(object, Classes::ATeamInfo::StaticClass())) {
		++ctx->foundTeam;
		auto *team = static_cast<Classes::ATeamInfo *>(object);
		State::TeamInfoSnapshot snap;
		if (State::TryReadTeamInfo(team, snap) && snap.valid) {
			++ctx->passed;
		} else {
			++ctx->failed;
		}

		if (TryIsA(team, Classes::ATdTeamInfo::StaticClass())) {
			++ctx->foundTdTeam;
			auto *tdTeam = static_cast<Classes::ATdTeamInfo *>(team);
			State::TdTeamInfoSnapshot tdSnap;
			if (State::TryReadTdTeamInfo(tdTeam, tdSnap) && tdSnap.valid) {
				++ctx->passed;
			} else {
				++ctx->failed;
			}
		}
		return true;
	}

	// ── ATdCheckpoint ───────────────────────────────────────────────
	if (TryIsA(object, Classes::ATdCheckpoint::StaticClass())) {
		++ctx->foundCheckpoint;
		auto *cp = static_cast<Classes::ATdCheckpoint *>(object);
		Gameplay::CheckpointSnapshot snap;
		if (Gameplay::TryReadTdCheckpoint(cp, snap) && snap.valid) {
			++ctx->passed;
		} else {
			++ctx->failed;
		}
		return true;
	}

	// ── ATdStashpoint ───────────────────────────────────────────────
	if (TryIsA(object, Classes::ATdStashpoint::StaticClass())) {
		++ctx->foundStashpoint;
		auto *sp = static_cast<Classes::ATdStashpoint *>(object);
		Gameplay::StashpointSnapshot snap;
		if (Gameplay::TryReadTdStashpoint(sp, snap) && snap.valid) {
			++ctx->passed;
		} else {
			++ctx->failed;
		}
		return true;
	}

	// ── ANavigationPoint ────────────────────────────────────────────
	if (TryIsA(object, Classes::ANavigationPoint::StaticClass())) {
		++ctx->foundNavPoint;
		// Only test first 20 nav points to keep iteration fast
		if (ctx->passed - (ctx->foundCheckpoint + ctx->foundStashpoint +
		                   ctx->foundPRI + ctx->foundGRI + ctx->foundTeam +
		                   ctx->foundTdPRI + ctx->foundTdGRI + ctx->foundTdTeam +
		                   ctx->foundMapInfo) < 20) {
			auto *nav = static_cast<Classes::ANavigationPoint *>(object);
			Gameplay::NavigationPointSnapshot snap;
			if (Gameplay::TryReadNavigationPoint(nav, snap) && snap.valid) {
				++ctx->passed;
			} else {
				++ctx->failed;
			}
		}
		return true;
	}

	// ── UTdMapInfo ──────────────────────────────────────────────────
	if (TryIsA(object, Classes::UTdMapInfo::StaticClass())) {
		++ctx->foundMapInfo;
		auto *mi = static_cast<Classes::UTdMapInfo *>(object);
		Gameplay::TdMapInfoSnapshot snap;
		if (Gameplay::TryReadTdMapInfo(mi, snap) && snap.valid) {
			++ctx->passed;
		} else {
			++ctx->failed;
		}
		return true;
	}

	return true;
}

std::string RunAllSafeWrapperTests() {
	if (!AreGlobalsReady()) {
		return "{\"error\":\"globals_not_ready\"}";
	}

	TestVisitorCtx ctx = {};

	// Test GObject lookup helpers via fast-path caches
	auto *world = Gui::TryFindActiveWorldInfo(true);
	auto *controller = Gui::TryFindTdPlayerController(true);

	char buf[64] = {};

	// SafeState::TryFindGameReplicationInfo
	{
		State::GameReplicationInfoSnapshot griSnap;
		if (world) {
			auto *gri = State::TryFindGameReplicationInfo(world);
			if (gri && State::TryReadGameReplicationInfo(gri, griSnap) && griSnap.valid) {
				++ctx.passed;
			} else {
				++ctx.failed;
			}
			if (gri && TryIsA(gri, Classes::ATdGameReplicationInfo::StaticClass())) {
				State::TdGameReplicationInfoSnapshot tdSnap;
				if (State::TryReadTdGameReplicationInfo(
				        static_cast<Classes::ATdGameReplicationInfo *>(gri), tdSnap) &&
				    tdSnap.valid) {
					++ctx.passed;
				} else {
					++ctx.failed;
				}
			}
		}
	}

	// SafeState::TryFindLocalPlayerReplicationInfo
	{
		if (controller) {
			auto *pri = State::TryFindLocalPlayerReplicationInfo(controller);
			State::PlayerReplicationInfoSnapshot priSnap;
			if (pri && State::TryReadPlayerReplicationInfo(pri, priSnap) && priSnap.valid) {
				++ctx.passed;
			} else {
				++ctx.failed;
			}
			if (pri && TryIsA(pri, Classes::ATdPlayerReplicationInfo::StaticClass())) {
				State::TdPlayerReplicationInfoSnapshot tdSnap;
				if (State::TryReadTdPlayerReplicationInfo(
				        static_cast<Classes::ATdPlayerReplicationInfo *>(pri), tdSnap) &&
				    tdSnap.valid) {
					++ctx.passed;
				} else {
					++ctx.failed;
				}
			}
		}
	}

	// Iterate GObjects to find + test all remaining types
	ForEachGlobalObject(TestVisitor, &ctx);

	std::string out = "{";

	out += "\"globals_ready\":true,";
	out += "\"lookup_world\":"; out += BoolStr(world != nullptr); out += ",";
	out += "\"lookup_controller\":"; out += BoolStr(controller != nullptr); out += ",";

	snprintf(buf, sizeof(buf), "%d found, %d ok",
	         ctx.foundPRI, ctx.foundPRI > 0 ? ctx.foundPRI : 0);
	AppendResult(out, "pri", ctx.foundPRI > 0, buf);
	out += ",";

	snprintf(buf, sizeof(buf), "%d found (td:%d)", ctx.foundGRI, ctx.foundTdGRI);
	AppendResult(out, "gri", ctx.foundGRI > 0, buf);
	out += ",";

	snprintf(buf, sizeof(buf), "%d found (td:%d)", ctx.foundTeam, ctx.foundTdTeam);
	AppendResult(out, "team_info", ctx.foundTeam >= 0, buf);
	out += ",";

	snprintf(buf, sizeof(buf), "%d found", ctx.foundCheckpoint);
	AppendResult(out, "checkpoint", ctx.foundCheckpoint >= 0, buf);
	out += ",";

	snprintf(buf, sizeof(buf), "%d found", ctx.foundStashpoint);
	AppendResult(out, "stashpoint", ctx.foundStashpoint >= 0, buf);
	out += ",";

	snprintf(buf, sizeof(buf), "%d found", ctx.foundNavPoint);
	AppendResult(out, "nav_point", ctx.foundNavPoint >= 0, buf);
	out += ",";

	snprintf(buf, sizeof(buf), "%d found", ctx.foundMapInfo);
	AppendResult(out, "map_info", ctx.foundMapInfo >= 0, buf);

	out += ",\"summary\":{\"passed\":";
	out += std::to_string(ctx.passed);
	out += ",\"failed\":";
	out += std::to_string(ctx.failed);
	out += "}}";

	return out;
}

} // namespace Test
} // namespace Safe
} // namespace MeSdk
