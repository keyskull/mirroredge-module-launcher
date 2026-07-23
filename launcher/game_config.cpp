#include "stdafx.h"

#include "game_config.h"

#include "config.h"
#include "module_contract.h"
#include "paths.h"
#include "window_layout_settings.h"

#include <cmath>
#include <fstream>
#include <objbase.h>
#include <shlobj.h>
#include <sstream>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace {

void ComputeRenderResolution(const DisplaySettings &settings, int &resX,
                             int &resY) {
	if (settings.mode == DisplayMode::Borderless) {
		if (!settings.renderMatchWindow && settings.resX > 0 &&
		    settings.resY > 0) {
			resX = settings.resX;
			resY = settings.resY;
			return;
		}

		WindowLayout_ComputeMatchWindowResolution(settings.scale, resX, resY);
		return;
	}

	resX = settings.resX;
	resY = settings.resY;
	if (resX < 1) {
		resX = 1280;
	}
	if (resY < 1) {
		resY = 720;
	}
}

std::string TrimLine(std::string line) {
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
	                         line.back() == ' ' || line.back() == '\t')) {
		line.pop_back();
	}
	size_t start = 0;
	while (start < line.size() &&
	       (line[start] == ' ' || line[start] == '\t')) {
		++start;
	}
	return line.substr(start);
}

std::string GetLineKey(const std::string &line) {
	const auto eq = line.find('=');
	if (eq == std::string::npos) {
		return {};
	}

	auto key = line.substr(0, eq);
	while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
		key.pop_back();
	}
	return key;
}

bool KeysEquivalent(const std::string &left, const std::string &right) {
	return _stricmp(left.c_str(), right.c_str()) == 0;
}

bool PatchIniLines(std::vector<std::string> &lines, const char *sectionName,
                   const std::vector<std::pair<std::string, std::string>> &entries) {
	bool sectionFound = false;
	size_t sectionIndex = 0;

	for (size_t i = 0; i < lines.size(); ++i) {
		const auto trimmed = TrimLine(lines[i]);
		if (trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']') {
			if (sectionFound) {
				break;
			}

			const auto section = trimmed.substr(1, trimmed.size() - 2);
			if (_stricmp(section.c_str(), sectionName) == 0) {
				sectionFound = true;
				sectionIndex = i;
			}
		}
	}

	if (!sectionFound) {
		lines.push_back(std::string("[") + sectionName + "]");
		sectionIndex = lines.size() - 1;
		for (const auto &entry : entries) {
			lines.push_back(entry.first + "=" + entry.second);
		}
		return true;
	}

	size_t insertAt = sectionIndex + 1;
	while (insertAt < lines.size()) {
		const auto trimmed = TrimLine(lines[insertAt]);
		if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
			break;
		}
		++insertAt;
	}

	for (const auto &entry : entries) {
		bool replaced = false;
		for (size_t i = sectionIndex + 1; i < insertAt; ++i) {
			const auto trimmed = TrimLine(lines[i]);
			if (trimmed.empty() || trimmed[0] == ';') {
				continue;
			}

			const auto key = GetLineKey(trimmed);
			if (KeysEquivalent(key, entry.first)) {
				lines[i] = entry.first + "=" + entry.second;
				replaced = true;
				break;
			}
		}

		if (!replaced) {
			lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt),
			             entry.first + "=" + entry.second);
			++insertAt;
		}
	}

	return true;
}

bool EnsureIniEntries(const std::wstring &iniPath, const char *sectionName,
                      const std::vector<std::pair<std::string, std::string>> &entries) {
	std::vector<std::string> lines;
	{
		std::ifstream input(iniPath);
		if (input) {
			std::string line;
			while (std::getline(input, line)) {
				lines.push_back(line);
			}
		}
	}

	const auto originalLines = lines;
	PatchIniLines(lines, sectionName, entries);
	if (originalLines == lines) {
		return true;
	}

	std::ofstream output(iniPath, std::ios::trunc);
	if (!output) {
		return false;
	}

	for (const auto &line : lines) {
		output << line << "\n";
	}
	return static_cast<bool>(output);
}

bool WriteModuleManagerWindowSettings(const DisplaySettings &settings, int resX,
                                      int resY) {
	const auto path = WindowLayoutSettingsPathA();
	const bool enabled = settings.mode == DisplayMode::Borderless;

	char scaleBuffer[32] = {};
	snprintf(scaleBuffer, sizeof(scaleBuffer), "%.2f", settings.scale);
	char resXBuffer[32] = {};
	char resYBuffer[32] = {};
	snprintf(resXBuffer, sizeof(resXBuffer), "%d", resX);
	snprintf(resYBuffer, sizeof(resYBuffer), "%d", resY);

	WritePrivateProfileStringA("Window", "Enabled", enabled ? "1" : "0", path);
	WritePrivateProfileStringA("Window", "Scale", scaleBuffer, path);
	WritePrivateProfileStringA("Window", "ResX", resXBuffer, path);
	WritePrivateProfileStringA("Window", "ResY", resYBuffer, path);
	return true;
}

// ME ignores UE3 -StartupMovies removers while StartupMovies= still lists the
// clip; renaming the BIK is the reliable skip used by community guides.
bool ApplyStartupMovieAssetSkip(bool skipMovies) {
	std::wstring binaries;
	if (!Paths::GetGameBinariesDirectory(binaries)) {
		return false;
	}

	wchar_t gameRoot[MAX_PATH] = {};
	wcscpy(gameRoot, binaries.c_str());
	if (!PathRemoveFileSpecW(gameRoot)) {
		return false;
	}

	const auto moviesDir = std::wstring(gameRoot) + L"\\TdGame\\Movies";
	const auto activePath = moviesDir + L"\\StartupMovie.bik";
	const auto skippedPath = moviesDir + L"\\StartupMovie.bik.mmskip";

	if (skipMovies) {
		if (PathFileExistsW(activePath.c_str())) {
			return MoveFileExW(activePath.c_str(), skippedPath.c_str(),
			                   MOVEFILE_REPLACE_EXISTING) != FALSE;
		}
		// Already renamed, or movie pack missing — skip intent is satisfied.
		return true;
	}

	if (PathFileExistsW(skippedPath.c_str()) &&
	    !PathFileExistsW(activePath.c_str())) {
		return MoveFileExW(skippedPath.c_str(), activePath.c_str(), 0) != FALSE;
	}
	return true;
}

bool IsStartupMovieAssetSkipped() {
	std::wstring binaries;
	if (!Paths::GetGameBinariesDirectory(binaries)) {
		return false;
	}

	wchar_t gameRoot[MAX_PATH] = {};
	wcscpy(gameRoot, binaries.c_str());
	if (!PathRemoveFileSpecW(gameRoot)) {
		return false;
	}

	const auto moviesDir = std::wstring(gameRoot) + L"\\TdGame\\Movies";
	const auto activePath = moviesDir + L"\\StartupMovie.bik";
	const auto skippedPath = moviesDir + L"\\StartupMovie.bik.mmskip";
	return PathFileExistsW(skippedPath.c_str()) != FALSE &&
	       PathFileExistsW(activePath.c_str()) == FALSE;
}

bool LoadIniLines(const std::wstring &iniPath, std::vector<std::string> &lines) {
	std::ifstream input(iniPath);
	if (!input) {
		return false;
	}

	std::string line;
	while (std::getline(input, line)) {
		lines.push_back(line);
	}
	return true;
}

bool ReadIniValue(const std::vector<std::string> &lines, const char *sectionName,
                  const char *key, std::string &value) {
	bool inSection = false;
	for (const auto &raw : lines) {
		const auto trimmed = TrimLine(raw);
		if (trimmed.empty() || trimmed[0] == ';') {
			continue;
		}

		if (trimmed.front() == '[' && trimmed.back() == ']') {
			const auto section = trimmed.substr(1, trimmed.size() - 2);
			inSection = _stricmp(section.c_str(), sectionName) == 0;
			continue;
		}

		if (!inSection) {
			continue;
		}

		const auto lineKey = GetLineKey(trimmed);
		if (!KeysEquivalent(lineKey, key)) {
			continue;
		}

		const auto eq = trimmed.find('=');
		if (eq == std::string::npos) {
			value.clear();
			return true;
		}

		value = TrimLine(trimmed.substr(eq + 1));
		return true;
	}

	return false;
}

bool ParseBoolTrue(const std::string &text) {
	return _stricmp(text.c_str(), "True") == 0 ||
	       _stricmp(text.c_str(), "1") == 0 ||
	       _stricmp(text.c_str(), "Yes") == 0;
}

bool ValuesMatchIgnoreCase(const std::string &left, const std::string &right) {
	return _stricmp(left.c_str(), right.c_str()) == 0;
}

} // namespace

namespace GameConfig {

bool GetTdEngineIniPath(std::wstring &path) {
	wchar_t documents[MAX_PATH] = {};
	if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr,
	                            SHGFP_TYPE_CURRENT, documents))) {
		return false;
	}

	static const wchar_t *kCandidates[] = {
	    L"\\EA Games\\Mirror's Edge\\TdGame\\Config\\TdEngine.ini",
	    L"\\EA Games\\Mirrors Edge\\TdGame\\Config\\TdEngine.ini",
	};

	for (const auto *suffix : kCandidates) {
		const auto candidate = std::wstring(documents) + suffix;
		if (PathFileExistsW(candidate.c_str())) {
			path = candidate;
			return true;
		}
	}

	path = std::wstring(documents) + kCandidates[0];
	return true;
}

bool ApplyDisplaySettings(const DisplaySettings &settings,
                          std::wstring *logLine) {
	int resX = 0;
	int resY = 0;
	ComputeRenderResolution(settings, resX, resY);

	std::wstring tdEnginePath;
	if (!GetTdEngineIniPath(tdEnginePath)) {
		if (logLine) {
			*logLine = L"Could not resolve TdEngine.ini path.";
		}
		return false;
	}

	wchar_t configDir[MAX_PATH] = {};
	wcscpy(configDir, tdEnginePath.c_str());
	PathRemoveFileSpecW(configDir);
	SHCreateDirectoryExW(nullptr, configDir, nullptr);

	const bool fullscreen = settings.mode == DisplayMode::Fullscreen;
	char resXText[32] = {};
	char resYText[32] = {};
	snprintf(resXText, sizeof(resXText), "%d", resX);
	snprintf(resYText, sizeof(resYText), "%d", resY);

	const std::vector<std::pair<std::string, std::string>> systemEntries = {
	    {"Fullscreen", fullscreen ? "True" : "False"},
	    {"ResX", resXText},
	    {"ResY", resYText},
	};

	if (!EnsureIniEntries(tdEnginePath, "SystemSettings", systemEntries)) {
		if (logLine) {
			*logLine = L"Failed to update TdEngine.ini [SystemSettings].";
		}
		return false;
	}

	{
		// Clear positive StartupMovies= (stock still lists the clip); keep UE3
		// removers. Alone, -StartupMovies does not override StartupMovies=.
		const std::vector<std::pair<std::string, std::string>> movieEntries =
		    settings.skipStartupMovies
		        ? std::vector<std::pair<std::string, std::string>>{
		              {"StartupMovies", ""},
		              {"-StartupMovies", "StartupMovie"},
		              {"-HealthWarningMovies", "HealthWarning"},
		          }
		        : std::vector<std::pair<std::string, std::string>>{
		              {"StartupMovies", "StartupMovie"},
		              {"-StartupMovies", ""},
		              {"-HealthWarningMovies", "HealthWarning"},
		          };

		if (!EnsureIniEntries(tdEnginePath, "FullScreenMovie", movieEntries)) {
			if (logLine) {
				*logLine = L"Failed to update TdEngine.ini [FullScreenMovie].";
			}
			return false;
		}
	}

	std::wstring movieAssetWarn;
	if (!ApplyStartupMovieAssetSkip(settings.skipStartupMovies)) {
		movieAssetWarn =
		    settings.skipStartupMovies
		        ? L" (warn: could not rename StartupMovie.bik)"
		        : L" (warn: could not restore StartupMovie.bik)";
	}

	if (!WriteModuleManagerWindowSettings(settings, resX, resY)) {
		if (logLine) {
			*logLine = L"Failed to update module_manager.settings.ini.";
		}
		return false;
	}

	if (logLine) {
		const wchar_t *modeLabel = L"borderless";
		switch (settings.mode) {
		case DisplayMode::Windowed:
			modeLabel = L"windowed";
			break;
		case DisplayMode::Fullscreen:
			modeLabel = L"fullscreen";
			break;
		case DisplayMode::Borderless:
		default:
			break;
		}

		wchar_t buffer[320] = {};
		if (settings.mode == DisplayMode::Borderless) {
			if (settings.renderMatchWindow) {
				swprintf(buffer, 320,
				           L"Display: %s %dx%d (%.0f%% screen)%s%s",
				           modeLabel, resX, resY, settings.scale * 100.0f,
				           settings.skipStartupMovies ? L", skip intro" : L"",
				           movieAssetWarn.c_str());
			} else {
				swprintf(buffer, 320,
				           L"Display: %s window %.0f%%, render %dx%d%s%s",
				           modeLabel, settings.scale * 100.0f, resX, resY,
				           settings.skipStartupMovies ? L", skip intro" : L"",
				           movieAssetWarn.c_str());
			}
		} else {
			swprintf(buffer, 320, L"Display: %s %dx%d%s%s", modeLabel, resX, resY,
			           settings.skipStartupMovies ? L", skip intro" : L"",
			           movieAssetWarn.c_str());
		}
		*logLine = buffer;
	}

	return true;
}

bool ReadAppliedDisplaySettings(DisplaySettings &out, int *appliedResX,
                                int *appliedResY) {
	out = {};
	if (appliedResX) {
		*appliedResX = 0;
	}
	if (appliedResY) {
		*appliedResY = 0;
	}

	std::wstring tdEnginePath;
	if (!GetTdEngineIniPath(tdEnginePath) ||
	    !PathFileExistsW(tdEnginePath.c_str())) {
		return false;
	}

	std::vector<std::string> lines;
	if (!LoadIniLines(tdEnginePath, lines)) {
		return false;
	}

	std::string fullscreen;
	std::string resXText;
	std::string resYText;
	std::string startupMovies;
	ReadIniValue(lines, "SystemSettings", "Fullscreen", fullscreen);
	ReadIniValue(lines, "SystemSettings", "ResX", resXText);
	ReadIniValue(lines, "SystemSettings", "ResY", resYText);
	ReadIniValue(lines, "FullScreenMovie", "StartupMovies", startupMovies);

	out.mode = ParseBoolTrue(fullscreen) ? DisplayMode::Fullscreen
	                                     : DisplayMode::Windowed;
	out.resX = atoi(resXText.c_str());
	out.resY = atoi(resYText.c_str());
	out.skipStartupMovies =
	    startupMovies.empty() || IsStartupMovieAssetSkipped();

	char enabledBuf[16] = {};
	GetPrivateProfileStringA("Window", "Enabled", "0", enabledBuf,
	                         sizeof(enabledBuf), WindowLayoutSettingsPathA());
	if (atoi(enabledBuf) != 0 && out.mode != DisplayMode::Fullscreen) {
		out.mode = DisplayMode::Borderless;
	}

	char scaleBuf[32] = {};
	GetPrivateProfileStringA("Window", "Scale", "0.50", scaleBuf,
	                         sizeof(scaleBuf), WindowLayoutSettingsPathA());
	out.scale = static_cast<float>(atof(scaleBuf));

	if (appliedResX) {
		*appliedResX = out.resX;
	}
	if (appliedResY) {
		*appliedResY = out.resY;
	}
	return true;
}

ConfigSyncStatus EvaluateConfigSync(const DisplaySettings &expected) {
	ConfigSyncStatus status;

	int expectedResX = 0;
	int expectedResY = 0;
	ComputeRenderResolution(expected, expectedResX, expectedResY);
	const bool expectedFullscreen =
	    expected.mode == DisplayMode::Fullscreen;
	const bool expectedBorderless =
	    expected.mode == DisplayMode::Borderless;

	std::wstring tdEnginePath;
	status.tdEngineOk = GetTdEngineIniPath(tdEnginePath) &&
	                    PathFileExistsW(tdEnginePath.c_str());

	bool tdMatched = false;
	if (status.tdEngineOk) {
		std::vector<std::string> lines;
		if (LoadIniLines(tdEnginePath, lines)) {
			std::string fullscreen;
			std::string resXText;
			std::string resYText;
			std::string startupMovies;
			std::string startupMoviesRemove;
			std::string healthMovies;
			ReadIniValue(lines, "SystemSettings", "Fullscreen", fullscreen);
			ReadIniValue(lines, "SystemSettings", "ResX", resXText);
			ReadIniValue(lines, "SystemSettings", "ResY", resYText);
			ReadIniValue(lines, "FullScreenMovie", "StartupMovies",
			             startupMovies);
			ReadIniValue(lines, "FullScreenMovie", "-StartupMovies",
			             startupMoviesRemove);
			ReadIniValue(lines, "FullScreenMovie", "-HealthWarningMovies",
			             healthMovies);

			const bool fullscreenMatched =
			    expectedFullscreen ? ParseBoolTrue(fullscreen)
			                       : !ParseBoolTrue(fullscreen);
			const bool resMatched =
			    atoi(resXText.c_str()) == expectedResX &&
			    atoi(resYText.c_str()) == expectedResY;
			const bool assetSkipped = IsStartupMovieAssetSkipped();
			const bool skipMatched =
			    expected.skipStartupMovies
			        ? (startupMovies.empty() && assetSkipped &&
			           ValuesMatchIgnoreCase(startupMoviesRemove,
			                                 "StartupMovie") &&
			           ValuesMatchIgnoreCase(healthMovies, "HealthWarning"))
			        : (!startupMovies.empty() && !assetSkipped);
			tdMatched = fullscreenMatched && resMatched && skipMatched;
		}
	}

	{
		const auto path = WindowLayoutSettingsPathA();
		char enabledBuf[16] = {};
		char scaleBuf[32] = {};
		char resXBuf[32] = {};
		char resYBuf[32] = {};
		GetPrivateProfileStringA("Window", "Enabled", "", enabledBuf,
		                         sizeof(enabledBuf), path);
		GetPrivateProfileStringA("Window", "Scale", "", scaleBuf,
		                         sizeof(scaleBuf), path);
		GetPrivateProfileStringA("Window", "ResX", "", resXBuf, sizeof(resXBuf),
		                         path);
		GetPrivateProfileStringA("Window", "ResY", "", resYBuf, sizeof(resYBuf),
		                         path);

		const bool enabled = atoi(enabledBuf) != 0;
		const float scale = static_cast<float>(atof(scaleBuf));
		const int layoutResX = atoi(resXBuf);
		const int layoutResY = atoi(resYBuf);

		if (expectedBorderless) {
			status.windowLayoutOk =
			    enabled && fabsf(scale - expected.scale) <= 0.01f &&
			    layoutResX == expectedResX && layoutResY == expectedResY;
		} else {
			status.windowLayoutOk = !enabled;
		}
	}

	status.tdContentOk = tdMatched;
	status.displayMatched = status.tdEngineOk && tdMatched &&
	                        status.windowLayoutOk;

	std::wstring binaries;
	if (Paths::GetGameBinariesDirectory(binaries)) {
		const auto &config = LauncherConfig::Get();
		const auto proxyPath = binaries + L"\\" + config.graphicsProxyDllName;
		status.proxyPresent = PathFileExistsW(proxyPath.c_str()) != FALSE;

		wchar_t gameRoot[MAX_PATH] = {};
		wcscpy(gameRoot, binaries.c_str());
		if (PathRemoveFileSpecW(gameRoot)) {
			const auto managerPath =
			    std::wstring(gameRoot) + L"\\" + MMOD_MANAGER_DEPLOY_SUBDIR +
			    L"\\" + MMOD_MANAGER_DLL_FILENAME;
			status.managerPresent =
			    PathFileExistsW(managerPath.c_str()) != FALSE;
		}
	}

	status.deployMatched = status.proxyPresent && status.managerPresent;

	std::wstring detail;
	if (!status.tdEngineOk) {
		detail += L"TdEngine missing; ";
	} else if (!tdMatched) {
		detail += L"TdEngine mismatch; ";
	}
	if (!status.windowLayoutOk) {
		detail += L"window layout mismatch; ";
	}
	if (!status.proxyPresent) {
		detail += L"d3d9 missing; ";
	}
	if (!status.managerPresent) {
		detail += L"module_manager missing; ";
	}
	if (detail.empty()) {
		detail = L"synced";
	} else if (detail.size() >= 2) {
		detail.resize(detail.size() - 2);
	}
	status.detail = std::move(detail);
	return status;
}

} // namespace GameConfig

