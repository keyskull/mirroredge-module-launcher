#include "stdafx.h"

#include "deploy_settings.h"
#include "game_path.h"
#include "launcher_settings.h"

namespace LauncherSettings {

namespace {

DisplayMode ToLauncherMode(DeployDisplayMode mode) {
	switch (mode) {
	case DeployDisplayMode::Windowed:
		return DisplayMode::Windowed;
	case DeployDisplayMode::Fullscreen:
		return DisplayMode::Fullscreen;
	default:
		return DisplayMode::Borderless;
	}
}

DeployDisplayMode FromLauncherMode(DisplayMode mode) {
	switch (mode) {
	case DisplayMode::Windowed:
		return DeployDisplayMode::Windowed;
	case DisplayMode::Fullscreen:
		return DeployDisplayMode::Fullscreen;
	default:
		return DeployDisplayMode::Borderless;
	}
}

DeployDisplaySettings ToDeploySettings(const DisplaySettings &settings) {
	DeployDisplaySettings out;
	out.mode = FromLauncherMode(settings.mode);
	out.resX = settings.resX;
	out.resY = settings.resY;
	out.scale = settings.scale;
	out.renderMatchWindow = settings.renderMatchWindow;
	out.skipStartupMovies = settings.skipStartupMovies;
	return out;
}

DisplaySettings FromDeploySettings(const DeployDisplaySettings &settings) {
	DisplaySettings out;
	out.mode = ToLauncherMode(settings.mode);
	out.resX = settings.resX;
	out.resY = settings.resY;
	out.scale = settings.scale;
	out.renderMatchWindow = settings.renderMatchWindow;
	out.skipStartupMovies = settings.skipStartupMovies;
	return out;
}

} // namespace

std::wstring GetSettingsFilePath() {
	std::wstring path;
	if (DeploySettings::ResolveSettingsPath(path, {})) {
		return path;
	}
	return L"settings.json";
}

bool LoadGameRoot(std::wstring &gameRoot) {
	DeploySettings::MigrateLegacySettingsIfNeeded();
	return DeploySettings::LoadGameRoot(gameRoot);
}

bool SaveGameRoot(const std::wstring &gameRoot) {
	if (!GamePath::ValidateGameRoot(gameRoot, nullptr)) {
		return false;
	}
	return DeploySettings::SaveGameRoot(gameRoot);
}

DisplaySettings LoadDisplaySettings() {
	DeploySettings::MigrateLegacySettingsIfNeeded();

	DeployDisplaySettings deploy;
	DeploySettings::LoadDisplaySettings(deploy, {});
	return FromDeploySettings(deploy);
}

bool SaveDisplaySettings(const DisplaySettings &settings) {
	return DeploySettings::SaveDisplaySettings(ToDeploySettings(settings), {});
}

bool LoadSkipConfigIntegrityCheck() {
	DeploySettings::MigrateLegacySettingsIfNeeded();

	bool skip = true;
	DeploySettings::LoadSkipConfigIntegrityCheck(skip, {});
	return skip;
}

bool SaveSkipConfigIntegrityCheck(const bool skip) {
	return DeploySettings::SaveSkipConfigIntegrityCheck(skip, {});
}

bool LoadSkipUpdateCheck() {
	DeploySettings::MigrateLegacySettingsIfNeeded();
	bool skip = false;
	DeploySettings::LoadSkipUpdateCheck(skip, {});
	return skip;
}

bool SaveSkipUpdateCheck(const bool skip) {
	return DeploySettings::SaveSkipUpdateCheck(skip, {});
}

bool LoadDismissedUpdateVersion(std::string &out) {
	DeploySettings::MigrateLegacySettingsIfNeeded();
	return DeploySettings::LoadDismissedUpdateVersion(out, {});
}

bool SaveDismissedUpdateVersion(const std::string &version) {
	return DeploySettings::SaveDismissedUpdateVersion(version, {});
}

} // namespace LauncherSettings
