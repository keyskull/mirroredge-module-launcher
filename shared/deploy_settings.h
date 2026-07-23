#pragma once

#include <string>
#include <vector>

enum class DeployDisplayMode {
	Windowed = 0,
	Fullscreen = 1,
	Borderless = 2,
};

struct DeployDisplaySettings {
	DeployDisplayMode mode = DeployDisplayMode::Borderless;
	int resX = 1920;
	int resY = 1080;
	float scale = 0.5f;
	bool renderMatchWindow = true;
	bool skipStartupMovies = true;
};

namespace DeploySettings {

bool ResolveSettingsPath(std::wstring &path, const std::wstring &gameRootHint = {});

bool LoadDisplaySettings(DeployDisplaySettings &out,
                         const std::wstring &gameRootHint = {});
bool SaveDisplaySettings(const DeployDisplaySettings &settings,
                         const std::wstring &gameRootHint = {});

bool LoadGameRoot(std::wstring &gameRoot);
bool SaveGameRoot(const std::wstring &gameRoot);

std::vector<std::string> LoadAutoLoadMods(const std::wstring &gameRootHint = {});

bool LoadSkipConfigIntegrityCheck(bool &out,
                                  const std::wstring &gameRootHint = {});
bool SaveSkipConfigIntegrityCheck(bool skip,
                                  const std::wstring &gameRootHint = {});

bool LoadSkipUpdateCheck(bool &out, const std::wstring &gameRootHint = {});
bool SaveSkipUpdateCheck(bool skip, const std::wstring &gameRootHint = {});
bool LoadDismissedUpdateVersion(std::string &out,
                                const std::wstring &gameRootHint = {});
bool SaveDismissedUpdateVersion(const std::string &version,
                                const std::wstring &gameRootHint = {});

bool LoadDiagnosticsEnabled(bool &out, const std::wstring &gameRootHint = {});

void MigrateLegacySettingsIfNeeded(const std::wstring &gameRootHint = {});

} // namespace DeploySettings
