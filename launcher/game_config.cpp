#include "stdafx.h"

#include "game_config.h"

#include "window_layout_settings.h"

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
		const std::vector<std::pair<std::string, std::string>> movieEntries =
		    settings.skipStartupMovies
		        ? std::vector<std::pair<std::string, std::string>>{
		              {"-StartupMovies", "StartupMovie"},
		              {"-HealthWarningMovies", "HealthWarning"},
		          }
		        : std::vector<std::pair<std::string, std::string>>{
		              {"-StartupMovies", ""},
		              {"-HealthWarningMovies", ""},
		          };

		if (!EnsureIniEntries(tdEnginePath, "FullScreenMovie", movieEntries)) {
			if (logLine) {
				*logLine = L"Failed to update TdEngine.ini [FullScreenMovie].";
			}
			return false;
		}
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

		wchar_t buffer[256] = {};
		if (settings.mode == DisplayMode::Borderless) {
			if (settings.renderMatchWindow) {
				swprintf(buffer, 256,
				           L"Display: %s %dx%d (%.0f%% screen)%s",
				           modeLabel, resX, resY, settings.scale * 100.0f,
				           settings.skipStartupMovies ? L", skip intro" : L"");
			} else {
				swprintf(buffer, 256,
				           L"Display: %s window %.0f%%, render %dx%d%s",
				           modeLabel, settings.scale * 100.0f, resX, resY,
				           settings.skipStartupMovies ? L", skip intro" : L"");
			}
		} else {
			swprintf(buffer, 256, L"Display: %s %dx%d%s", modeLabel, resX, resY,
			           settings.skipStartupMovies ? L", skip intro" : L"");
		}
		*logLine = buffer;
	}

	return true;
}

} // namespace GameConfig
