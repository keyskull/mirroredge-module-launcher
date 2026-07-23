#include "timing_constants.h"
#include "viewport.h"

#include "debug_trace.h"
#include "engine.h"
#include "me_sdk/runtime/init.h"
#include "me_sdk/runtime/safe_gui.h"

#include <Windows.h>
#include <cmath>

namespace {

bool g_sdkReady = false;
int g_lastSyncedWidth = 0;
int g_lastSyncedHeight = 0;
float g_baseMouseSensitivity = -1.f;
float g_baseTdSensitivityMultiplier = -1.f;
float g_lastMouseLookScale = 1.f;
bool g_loggedMouseCompensation = false;

bool IsSafeForGameplaySdkTouch() {
	if (!MeSdk::AreGlobalsReady()) {
		return false;
	}

	const HWND game = Engine::GetWindow();
	if (!game) {
		return false;
	}

	const HWND fg = GetForegroundWindow();
	if (!fg) {
		return false;
	}

	return fg == game || IsChild(game, fg);
}

bool EnsureSdk() {
	if (g_sdkReady) {
		return true;
	}

	// Viewport sync must not initialize the SDK — engine/core own init timing.
	// GET_STATUS polling can load engine.dll before hooks/core; early
	// InitializeGlobals() caused FindObject crashes and boot freezes on 2号机.
	if (!MeSdk::AreGlobalsReady()) {
		return false;
	}

	g_sdkReady = true;
	return true;
}

} // namespace

namespace EngineBorderlessSync {

bool QueryEngineViewportSize(int &width, int &height) {
	width = 0;
	height = 0;
	if (!EnsureSdk()) {
		return false;
	}

	auto *viewport = MeSdk::Safe::Gui::TryFindTdViewportClient(false);
	return viewport && MeSdk::Safe::Gui::TryQueryViewportSize(viewport, width, height);
}

bool TrySyncViewportResolution(int width, int height) {
	if (width < 1 || height < 1 || !IsSafeForGameplaySdkTouch()) {
		return false;
	}

	if (!EnsureSdk()) {
		return false;
	}

	auto *viewport = MeSdk::Safe::Gui::TryFindTdViewportClient(false);
	if (!viewport) {
		return false;
	}

	int currentW = 0;
	int currentH = 0;
	if (!MeSdk::Safe::Gui::TryQueryViewportSize(viewport, currentW, currentH)) {
		return false;
	}

	if (abs(currentW - width) <= 1 && abs(currentH - height) <= 1) {
		g_lastSyncedWidth = width;
		g_lastSyncedHeight = height;
		return true;
	}

	if (width == g_lastSyncedWidth && height == g_lastSyncedHeight) {
		g_lastSyncedWidth = 0;
		g_lastSyncedHeight = 0;
	}

	for (int attempt = 0; attempt < 3; ++attempt) {
		MeSdk::Safe::Gui::TryRunSetResCommand(viewport, width, height);
		if (MeSdk::Safe::Gui::TryQueryViewportSize(viewport, currentW, currentH) &&
		    abs(currentW - width) <= 1 && abs(currentH - height) <= 1) {
			EngineDebugTrace::Logf("engine: viewport synced via setres %dx%d", width,
			                       height);
			EngineDebugTrace::Event("borderless_sync.cpp:TrySyncViewportResolution",
			                        "setres_ok", "H-VIEWPORT",
			                        static_cast<uintptr_t>(width),
			                        static_cast<uintptr_t>(height),
			                        static_cast<uintptr_t>(attempt + 1), 0);
			g_lastSyncedWidth = width;
			g_lastSyncedHeight = height;
			return true;
		}
		Sleep(Timing::kD3D9PollMs);
	}

	EngineDebugTrace::Event("borderless_sync.cpp:TrySyncViewportResolution",
	                        "setres_fail", "H-VIEWPORT",
	                        static_cast<uintptr_t>(width),
	                        static_cast<uintptr_t>(height),
	                        static_cast<uintptr_t>(currentW), currentH);
	return false;
}

bool TryCompensateMouseLook(int clientWidth, int clientHeight, int renderWidth,
                            int renderHeight) {
	if (clientWidth < 1 || clientHeight < 1 || !IsSafeForGameplaySdkTouch()) {
		return false;
	}

	if (!EnsureSdk()) {
		return false;
	}

	if (renderWidth < 1 || renderHeight < 1) {
		renderWidth = clientWidth;
		renderHeight = clientHeight;
	}

	TrySyncViewportResolution(renderWidth, renderHeight);

	int viewportWidth = 0;
	int viewportHeight = 0;
	if (!QueryEngineViewportSize(viewportWidth, viewportHeight)) {
		return false;
	}

	float scale = 1.f;
	if (abs(viewportWidth - clientWidth) > 1 || abs(viewportHeight - clientHeight) > 1) {
		const float scaleW = static_cast<float>(clientWidth) /
		                     static_cast<float>(viewportWidth);
		const float scaleH = static_cast<float>(clientHeight) /
		                     static_cast<float>(viewportHeight);
		scale = scaleW < scaleH ? scaleW : scaleH;
	}

	auto *input = MeSdk::Safe::Gui::TryFindLocalPlayerInput();
	if (!input) {
		g_lastMouseLookScale = scale;
		return scale >= 0.99f;
	}

	static Classes::UClass *tdInputClass = nullptr;
	if (!tdInputClass) {
		tdInputClass = Classes::UTdPlayerInput::StaticClass();
	}
	if (tdInputClass && MeSdk::Safe::TryIsA(input, tdInputClass)) {
		auto *tdInput = static_cast<Classes::UTdPlayerInput *>(input);
		float currentTd = 0.f;
		if (!MeSdk::Safe::TryReadField(&tdInput->SensitivityMultiplier, currentTd)) {
			g_lastMouseLookScale = scale;
			return scale >= 0.99f;
		}
		if (g_baseTdSensitivityMultiplier < 0.f) {
			g_baseTdSensitivityMultiplier = currentTd;
		}
		const float desiredTd = g_baseTdSensitivityMultiplier * scale;
		if (fabsf(currentTd - desiredTd) > 0.0005f) {
			MeSdk::Safe::TryWriteField(&tdInput->SensitivityMultiplier, desiredTd);
		}
	} else {
		float currentSensitivity = 0.f;
		if (!MeSdk::Safe::TryReadField(&input->MouseSensitivity, currentSensitivity)) {
			g_lastMouseLookScale = scale;
			return scale >= 0.99f;
		}
		if (g_baseMouseSensitivity < 0.f) {
			g_baseMouseSensitivity = currentSensitivity;
		}
		const float desiredSensitivity = g_baseMouseSensitivity * scale;
		if (fabsf(currentSensitivity - desiredSensitivity) > 0.0005f) {
			MeSdk::Safe::TryWriteField(&input->MouseSensitivity, desiredSensitivity);
		}
	}

	g_lastMouseLookScale = scale;

	if (!g_loggedMouseCompensation && scale < 0.99f) {
		g_loggedMouseCompensation = true;
		EngineDebugTrace::Logf(
		    "engine: mouse look scaled x%.3f (client %dx%d, viewport %dx%d)", scale,
		    clientWidth, clientHeight, viewportWidth, viewportHeight);
		EngineDebugTrace::Event("borderless_sync.cpp:TryCompensateMouseLook",
		                        "mouse_scaled", "H-VIEWPORT",
		                        static_cast<uintptr_t>(clientWidth),
		                        static_cast<uintptr_t>(clientHeight),
		                        static_cast<uintptr_t>(viewportWidth), viewportHeight);
	}

	return scale >= 0.99f;
}

float GetLastMouseLookScale() { return g_lastMouseLookScale; }

} // namespace EngineBorderlessSync
