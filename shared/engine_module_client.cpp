#include "engine_module_client.h"

#include "engine_api.h"
#include "runtime_module_client.h"

#include <Windows.h>

#include <cstring>
#include <vector>

namespace {

HMODULE g_engineModule = nullptr;

constexpr RuntimeModuleClient::Binding kEngineBinding = {L"engine", L"engine.dll"};

struct EngineExports {
	bool resolved = false;
	DWORD lastAttemptTick = 0;
	MMOD_EngineFormatStatusJsonFn formatStatus = nullptr;
	MMOD_BorderlessInstallHostFn installBorderlessHost = nullptr;
	MMOD_BorderlessAppendStatusFn appendBorderlessStatus = nullptr;
	MMOD_BorderlessQueryUiStateFn queryBorderlessUi = nullptr;
	MMOD_BorderlessSetEnabledFn setBorderlessEnabled = nullptr;
	MMOD_BorderlessSetScaleFn setBorderlessScale = nullptr;
	MMOD_BorderlessMarkApplyFn markBorderlessApply = nullptr;
	MMOD_BorderlessTrySyncFn trySync = nullptr;
	MMOD_BorderlessTryMouseFn tryMouse = nullptr;
	MMOD_BorderlessQueryViewportFn queryViewport = nullptr;
	MMOD_BorderlessGetMouseScaleFn getMouseScale = nullptr;
};

EngineExports &Exports() {
	static EngineExports exports;
	return exports;
}

// ResolveExports sets all 11 function pointers from engine.dll.
// The return value only requires 3 core exports to succeed (formatStatus,
// installBorderlessHost, trySync); the remaining 8 are optional.
// Every public caller in EngineModuleClient individually null-checks the
// specific export it needs before calling — there is no unsafe dereference.
bool ResolveExports() {
	auto &exports = Exports();
	if (exports.resolved) {
		return exports.formatStatus != nullptr || exports.installBorderlessHost != nullptr ||
		       exports.trySync != nullptr;
	}

	// Cooldown: don't retry LoadLibrary more than once every 2 seconds to avoid
	// hammering the loader during hook-wait polling loops.
	const DWORD tick = GetTickCount();
	if (exports.lastAttemptTick && (tick - exports.lastAttemptTick) < 2000) {
		return false;
	}
	exports.lastAttemptTick = tick;

	// GET_STATUS polls during hook wait must not LoadLibrary(engine.dll) before
	// core bootstrap — early engine load + SDK touch crashed MirrorsEdge on 2号机.
	const HMODULE existingEngine = GetModuleHandleW(L"engine.dll");
	const HMODULE core = GetModuleHandleW(L"core.dll");
	if (!existingEngine && !core) {
		return false;
	}

	const HMODULE engine = RuntimeModuleClient::EnsureLoaded(
	    kEngineBinding, RuntimeModuleClient::FindHostModule(), &g_engineModule);
	if (!engine) {
		return false;
	}

	exports.formatStatus = reinterpret_cast<MMOD_EngineFormatStatusJsonFn>(
	    GetProcAddress(engine, MMOD_ENGINE_FORMAT_STATUS_JSON));
	exports.installBorderlessHost = reinterpret_cast<MMOD_BorderlessInstallHostFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_INSTALL_HOST));
	exports.appendBorderlessStatus = reinterpret_cast<MMOD_BorderlessAppendStatusFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_APPEND_STATUS));
	exports.queryBorderlessUi = reinterpret_cast<MMOD_BorderlessQueryUiStateFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_QUERY_UI_STATE));
	exports.setBorderlessEnabled = reinterpret_cast<MMOD_BorderlessSetEnabledFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_SET_ENABLED));
	exports.setBorderlessScale = reinterpret_cast<MMOD_BorderlessSetScaleFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_SET_SCALE));
	exports.markBorderlessApply = reinterpret_cast<MMOD_BorderlessMarkApplyFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_MARK_APPLY));
	exports.trySync = reinterpret_cast<MMOD_BorderlessTrySyncFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_TRY_SYNC));
	exports.tryMouse = reinterpret_cast<MMOD_BorderlessTryMouseFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_TRY_MOUSE));
	exports.queryViewport = reinterpret_cast<MMOD_BorderlessQueryViewportFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_QUERY_VIEWPORT));
	exports.getMouseScale = reinterpret_cast<MMOD_BorderlessGetMouseScaleFn>(
	    GetProcAddress(engine, MMOD_BORDERLESS_GET_MOUSE_SCALE));

	exports.resolved = true;
	return exports.formatStatus != nullptr || exports.installBorderlessHost != nullptr ||
	       exports.trySync != nullptr;
}

} // namespace

namespace EngineModuleClient {

bool TryFormatStatusJson(std::string &out) {
	out.clear();
	if (!ResolveExports() || !Exports().formatStatus) {
		return false;
	}

	const int need = Exports().formatStatus(nullptr, 0);
	if (need <= 1) {
		return false;
	}

	std::vector<char> buffer(static_cast<size_t>(need), '\0');
	Exports().formatStatus(buffer.data(), need);
	out.assign(buffer.data());
	return !out.empty();
}

void InstallBorderlessHost(const ModHostApi *host) {
	if (ResolveExports() && Exports().installBorderlessHost) {
		Exports().installBorderlessHost(host);
	}
}

bool TryAppendBorderlessStatusJson(std::string &out) {
	if (!ResolveExports() || !Exports().appendBorderlessStatus) {
		return false;
	}

	char buffer[256] = {};
	size_t written = 0;
	Exports().appendBorderlessStatus(buffer, sizeof(buffer), &written);
	if (written == 0) {
		return false;
	}

	out.append(buffer, written);
	return true;
}

bool QueryBorderlessUiState(BorderlessUiState &out) {
	if (!ResolveExports() || !Exports().queryBorderlessUi) {
		return false;
	}
	return Exports().queryBorderlessUi(&out.enabled, &out.scale, &out.clientWidth,
	                                  &out.clientHeight, &out.backBufferWidth,
	                                  &out.backBufferHeight);
}

void SetBorderlessEnabled(bool enabled) {
	if (ResolveExports() && Exports().setBorderlessEnabled) {
		Exports().setBorderlessEnabled(enabled);
	}
}

void SetBorderlessScale(float scale) {
	if (ResolveExports() && Exports().setBorderlessScale) {
		Exports().setBorderlessScale(scale);
	}
}

void MarkBorderlessApply() {
	if (ResolveExports() && Exports().markBorderlessApply) {
		Exports().markBorderlessApply();
	}
}

bool TrySyncViewportResolution(int width, int height) {
	return ResolveExports() && Exports().trySync && Exports().trySync(width, height);
}

bool TryCompensateMouseLook(int clientWidth, int clientHeight, int renderWidth,
                            int renderHeight) {
	return ResolveExports() && Exports().tryMouse &&
	       Exports().tryMouse(clientWidth, clientHeight, renderWidth, renderHeight);
}

bool QueryEngineViewportSize(int &width, int &height) {
	return ResolveExports() && Exports().queryViewport &&
	       Exports().queryViewport(&width, &height);
}

float GetLastMouseLookScale() {
	if (!ResolveExports() || !Exports().getMouseScale) {
		return 1.f;
	}
	return Exports().getMouseScale();
}

} // namespace EngineModuleClient
