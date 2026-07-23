#include "engine.h"
#include "engine_internal.h"
#include "plugin_seh_guard.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>

namespace {

std::atomic<bool> g_activationPending{false};
std::atomic<bool> g_activationScheduled{false};
std::atomic<int> g_activationRetries{0};
std::mutex g_activationCallbackMutex;
Engine::MainThreadTask g_activationCallback = nullptr;
std::atomic<int> g_pawnReadyStreak{0};

void QueueActivateRetry();
void ProcessGameplayActivationBody(void *);

std::string WideToUtf8(const wchar_t *text) {
	if (!text || !*text) {
		return {};
	}

	const int needed =
	    WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (needed <= 0) {
		return {};
	}

	std::string result(static_cast<size_t>(needed - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], needed, nullptr,
	                    nullptr);
	return result;
}

std::string ToLowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
	               [](unsigned char c) {
		               return static_cast<char>(std::tolower(c));
	               });
	return value;
}

struct PawnReadyContext {
	bool ready = false;
};

static void ProbePawnReadyBody(void *data) {
	auto *ctx = static_cast<PawnReadyContext *>(data);
	ctx->ready = Engine::CanSafelyUsePlayerPawn();
}

struct MapNameContext {
	char *out;
	size_t outSize;
	bool ok;
};

static void ProbeMapNameBody(void *data) {
	auto *ctx = static_cast<MapNameContext *>(data);
	ctx->ok = false;
	if (!ctx->out || ctx->outSize == 0) {
		return;
	}

	ctx->out[0] = '\0';
	if (!EngineInternal::modReady.load() ||
	    EngineInternal::levelLoad.Loading) {
		return;
	}

	auto *world = Engine::GetWorld(false);
	if (!world) {
		return;
	}

	const auto mapName =
	    ToLowerAscii(WideToUtf8(world->GetMapName(false).c_str()));
	if (mapName.empty()) {
		return;
	}

	strncpy(ctx->out, mapName.c_str(), ctx->outSize - 1);
	ctx->out[ctx->outSize - 1] = '\0';
	ctx->ok = true;
}

static void InvokeActivationCallback() {
	Engine::MainThreadTask callback = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_activationCallbackMutex);
		callback = g_activationCallback;
	}

	if (callback) {
		callback();
	}
}

static void FinishGameplayActivation() {
	// Do not set hosted gameplay live here. Multiplayer registers tick/bones
	// callbacks in the activation callback and must flip live only after that.
	g_activationPending.store(false);
	g_activationRetries.store(0);
	Engine::QueueMainThreadTask([]() { InvokeActivationCallback(); });
}

static bool ProbeMapReady() {
	char mapName[0x100] = {};
	MapNameContext ctx = {mapName, sizeof(mapName), false};
	DWORD exceptionCode = 0;
	if (!PluginSehGuard::InvokeVoid(
	        "engine_gameplay_map",
	        "engine_gameplay_activation.cpp:ProcessGameplayActivation",
	        ProbeMapNameBody, &ctx, &exceptionCode)) {
		return false;
	}

	return ctx.ok && mapName[0] != '\0' && strcmp(mapName, "tdmainmenu") != 0;
}

static void ProcessGameplayActivationBody(void *) {
	g_activationScheduled.store(false);

	if (!g_activationPending.load()) {
		return;
	}

	if (!EngineInternal::hostedMode.load()) {
		FinishGameplayActivation();
		return;
	}

	if (!EngineInternal::modReady.load() ||
	    !Engine::AreGameplayHooksInstalled()) {
		const int retries = g_activationRetries.fetch_add(1) + 1;
		if (retries < 900) {
			QueueActivateRetry();
		}
		return;
	}

	PawnReadyContext pawnCtx = {false};
	DWORD pawnExceptionCode = 0;
	const bool pawnProbeOk = PluginSehGuard::InvokeVoid(
	    "engine_gameplay_ready", "engine_gameplay_activation.cpp:ProcessGameplayActivation",
	    ProbePawnReadyBody, &pawnCtx, &pawnExceptionCode);
	const bool pawnReady = pawnProbeOk && pawnCtx.ready;

	// Hosted split must wait for a gameplay-safe pawn. Map name alone is not
	// enough: tutorial_p can be loaded while intro cinematics still run.
	if (!pawnReady) {
		g_pawnReadyStreak.store(0);
		const int retries = g_activationRetries.fetch_add(1) + 1;
		if (retries < 900) {
			QueueActivateRetry();
		}
		return;
	}

	const int streak = g_pawnReadyStreak.fetch_add(1) + 1;
	if (streak < 90) {
		QueueActivateRetry();
		return;
	}
	g_pawnReadyStreak.store(0);

	FinishGameplayActivation();
}

void QueueActivateRetry() {
	if (!g_activationPending.load()) {
		return;
	}

	if (g_activationScheduled.exchange(true)) {
		return;
	}

	if (!Engine::IsHostedGameplayLive()) {
		Engine::SetHostedGameplayLive(false);
	}
	Engine::QueueMainThreadTask([]() { ProcessGameplayActivationBody(nullptr); });
}

} // namespace

namespace Engine {

bool IsGameplayReadySafe() {
	PawnReadyContext ctx = {false};
	DWORD exceptionCode = 0;
	if (!PluginSehGuard::InvokeVoid("engine_gameplay_ready",
	                                "engine_gameplay_activation.cpp:IsGameplayReadySafe",
	                                ProbePawnReadyBody, &ctx, &exceptionCode)) {
		return false;
	}
	return ctx.ready;
}

bool TryGetGameplayMapName(char *out, size_t outSize) {
	if (!out || outSize == 0) {
		return false;
	}

	MapNameContext ctx = {out, outSize, false};
	DWORD exceptionCode = 0;
	if (!PluginSehGuard::InvokeVoid(
	        "engine_gameplay_map", "engine_gameplay_activation.cpp:TryGetGameplayMapName",
	        ProbeMapNameBody, &ctx, &exceptionCode)) {
		out[0] = '\0';
		return false;
	}

	return ctx.ok;
}

void RequestGameplayActivation(MainThreadTask onActivated) {
	{
		std::lock_guard<std::mutex> lock(g_activationCallbackMutex);
		g_activationCallback = onActivated;
	}

	if (EngineInternal::hostedMode.load() && Engine::IsHostedGameplayLive()) {
		PawnReadyContext pawnCtx = {false};
		DWORD pawnExceptionCode = 0;
		const bool pawnProbeOk = PluginSehGuard::InvokeVoid(
		    "engine_gameplay_ready",
		    "engine_gameplay_activation.cpp:RequestGameplayActivation",
		    ProbePawnReadyBody, &pawnCtx, &pawnExceptionCode);
		if (pawnProbeOk && pawnCtx.ready) {
			OutputDebugStringA("RequestGameplayActivation: already live, callback invoked\n");
			g_activationPending.store(false);
			g_activationRetries.store(0);
			g_activationScheduled.store(false);
			Engine::QueueMainThreadTask([]() { InvokeActivationCallback(); });
			return;
		}
	}

	// Hosted mode fast path: fire the callback immediately without waiting
	// for pawn readiness.  The retry mechanism (ProcessGameplayActivationBody)
	// requires PeekMessage / PumpMainThreadTasks to process, but during intro
	// cinematics (tutorial_p) PeekMessage may not be called for extended
	// periods, starving the retry loop.  SpawnCharacter handles the "no pawn"
	// case gracefully and TrySpawnPendingRemotePlayers retries every 2 s.
	// Called from main-thread engine hooks so direct invocation is safe.
	if (EngineInternal::hostedMode.load()) {
		OutputDebugStringA("RequestGameplayActivation: hosted fast path\n");
		g_activationPending.store(false);
		g_activationRetries.store(0);
		g_activationScheduled.store(false);
		InvokeActivationCallback();
		return;
	}

	OutputDebugStringA("RequestGameplayActivation: pending, starting retry\n");
	g_activationPending.store(true);
	g_activationRetries.store(0);
	QueueActivateRetry();
	OutputDebugStringA("RequestGameplayActivation: retry queued\n");
}

void CancelGameplayActivation() {
	g_activationPending.store(false);
	g_activationRetries.store(0);
	g_activationScheduled.store(false);
	SetHostedGameplayLive(false);

	std::lock_guard<std::mutex> lock(g_activationCallbackMutex);
	g_activationCallback = nullptr;
}

} // namespace Engine
