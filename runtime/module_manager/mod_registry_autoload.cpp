#include "mod_registry.h"
#include "mod_registry_internal.h"

#include "deploy_settings.h"
#include "game_paths.h"
#include "mod_log.h"
#include "mod_security.h"
#include "module_contract.h"
#include "presentation.h"

#include <Shlwapi.h>
#include <Windows.h>

#include <mutex>
#include "timing_constants.h"

#include <string>
#include <vector>

#include <atomic>

#pragma comment(lib, "Shlwapi.lib")

namespace {

using ModRegistryInternal::FindEntryById;
using ModRegistryInternal::EnsureModuleEntryLocked;
using ModRegistryInternal::g_mutex;
using ModRegistryInternal::g_modules;
using ModRegistryInternal::ModuleEntry;
using ModRegistryInternal::QueueLoadModule;

std::atomic<bool> g_pendingSettingsAutoLoad{false};

std::wstring Utf8ToWide(const std::string &text) {
	if (text.empty()) {
		return {};
	}
	const int size =
	    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                        nullptr, 0);
	if (size <= 0) {
		return {};
	}
	std::wstring out(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                    out.empty() ? nullptr : &out[0], size);
	return out;
}

void EnsureCoreEntryLocked() { EnsureModuleEntryLocked(L"core"); }

void QueueAutoLoadModsLocked(const std::vector<std::string> &mods) {
	for (const auto &modIdUtf8 : mods) {
		if (modIdUtf8.empty()) {
			continue;
		}

		const auto modId = Utf8ToWide(modIdUtf8);
		if (modId.empty() || ModSecurity::IsBuiltinModuleId(modId)) {
			continue;
		}

		if (auto *entry = FindEntryById(modId)) {
			if (!entry->loaded && entry->loadPhase == ModRegistryInternal::LoadPhase::Idle &&
			    !entry->busy) {
				QueueLoadModule(modId, "settings.json");
			}
			continue;
		}

		ModLog::Writef("mod_registry: autoLoad skipped unknown mod %s",
		               modIdUtf8.c_str());
	}
}

} // namespace

namespace ModRegistry {

void BootstrapAutoLoad() {
	ModLog::Write("mod_registry: bootstrap auto-load starting");

	const auto hooksDeadline = GetTickCount() + 120000;
	bool waitingForFirstFrameLogged = false;
	while (GetTickCount() < hooksDeadline) {
		const bool hooksInstalled = Presentation::AreHooksInstalled();
		const bool frameSeen =
		    Presentation::GetEndSceneCallCount() > 0 ||
		    Presentation::GetPresentCallCount() > 0 ||
		    Presentation::IsOverlayReady();
		if (hooksInstalled && frameSeen) {
			break;
		}
		if (hooksInstalled && !frameSeen && !waitingForFirstFrameLogged) {
			waitingForFirstFrameLogged = true;
			ModLog::Write(
			    "mod_registry: core auto-load waiting for first render frame");
		}
		Sleep(Timing::kAutoloadYieldMs);
	}

	const bool hooksInstalled = Presentation::AreHooksInstalled();
	const bool frameSeen =
	    Presentation::GetEndSceneCallCount() > 0 ||
	    Presentation::GetPresentCallCount() > 0 ||
	    Presentation::IsOverlayReady();
	if (!hooksInstalled || !frameSeen) {
		ModLog::Write(!hooksInstalled
		                  ? "mod_registry: core auto-load skipped (presentation hooks pending)"
		                  : "mod_registry: core auto-load skipped (no render frames after hooks)");
		return;
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		EnsureCoreEntryLocked();
	}
	QueueLoadModule(L"core", "auto-bootstrap");

	g_pendingSettingsAutoLoad = true;
	ModLog::Write("mod_registry: core bootstrap queued");
}

void TryQueueSettingsAutoLoadMods() {
	if (!g_pendingSettingsAutoLoad.load()) {
		return;
	}
	if (!ModRegistry::IsLoaded(L"core")) {
		return;
	}

	g_pendingSettingsAutoLoad = false;

	std::wstring gameRoot;
	std::vector<std::string> autoMods;
	if (GamePaths::GetGameRootDirectory(gameRoot)) {
		autoMods = DeploySettings::LoadAutoLoadMods(gameRoot);
	} else {
		autoMods = DeploySettings::LoadAutoLoadMods();
	}

	if (autoMods.empty()) {
		ModLog::Write("mod_registry: no mods.autoLoad entries in settings.json");
		return;
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		QueueAutoLoadModsLocked(autoMods);
	}
	ModLog::Write("mod_registry: settings.json autoLoad queued");
}

} // namespace ModRegistry
