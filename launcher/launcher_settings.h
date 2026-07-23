#pragma once

#include <string>

// std::string used by dismissed update version helpers.

enum class DisplayMode {
	Windowed = 0,
	Fullscreen = 1,
	Borderless = 2,
};

struct DisplaySettings {
	DisplayMode mode = DisplayMode::Borderless;
	int resX = 1920;
	int resY = 1080;
	float scale = 0.5f;
	// Borderless only: render at Scale×monitor; when false, use resX/resY for render.
	bool renderMatchWindow = true;
	bool skipStartupMovies = true;
};

namespace LauncherSettings {

std::wstring GetSettingsFilePath();

bool LoadGameRoot(std::wstring &gameRoot);
bool SaveGameRoot(const std::wstring &gameRoot);

DisplaySettings LoadDisplaySettings();
bool SaveDisplaySettings(const DisplaySettings &settings);

bool LoadSkipConfigIntegrityCheck();
bool SaveSkipConfigIntegrityCheck(bool skip);

bool LoadSkipUpdateCheck();
bool SaveSkipUpdateCheck(bool skip);
bool LoadDismissedUpdateVersion(std::string &out);
bool SaveDismissedUpdateVersion(const std::string &version);

} // namespace LauncherSettings
