#include "deploy_settings.h"

#include "json.h"
#include "module_contract.h"

#include <ShlObj.h>
#include <Shlwapi.h>
#include <Windows.h>

#include <fstream>
#include <sstream>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

namespace {

constexpr auto kLegacyLauncherIni = L"mirroredge-launcher.settings";

std::string WideToUtf8(const std::wstring &text) {
	if (text.empty()) {
		return {};
	}

	const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
	                                     static_cast<int>(text.size()), nullptr,
	                                     0, nullptr, nullptr);
	if (size <= 0) {
		return {};
	}

	std::string out(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                    out.empty() ? nullptr : &out[0], size, nullptr, nullptr);
	return out;
}

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

std::wstring GetLauncherExeDirectory() {
	wchar_t path[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
		return {};
	}
	PathRemoveFileSpecW(path);
	return path;
}

std::wstring GetLegacyIniPath() {
	wchar_t temp[MAX_PATH] = {};
	GetTempPathW(static_cast<DWORD>(sizeof(temp) / sizeof(temp[0])), temp);
	return std::wstring(temp) + kLegacyLauncherIni;
}

bool ReadFileUtf8(const std::wstring &path, std::string &out) {
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		return false;
	}

	std::ostringstream buffer;
	buffer << stream.rdbuf();
	out = buffer.str();
	if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
	    static_cast<unsigned char>(out[1]) == 0xBB &&
	    static_cast<unsigned char>(out[2]) == 0xBF) {
		out.erase(0, 3);
	}
	return !out.empty();
}

bool WriteFileUtf8(const std::wstring &path, const std::string &content) {
	const auto parent = path;
	wchar_t dir[MAX_PATH] = {};
	wcscpy(dir, parent.c_str());
	PathRemoveFileSpecW(dir);
	if (dir[0]) {
		SHCreateDirectoryExW(nullptr, dir, nullptr);
	}

	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream) {
		return false;
	}
	stream.write(content.data(), static_cast<std::streamsize>(content.size()));
	return stream.good();
}

DeployDisplayMode ParseDisplayMode(const std::string &value) {
	if (value == "windowed") {
		return DeployDisplayMode::Windowed;
	}
	if (value == "fullscreen") {
		return DeployDisplayMode::Fullscreen;
	}
	return DeployDisplayMode::Borderless;
}

const char *DisplayModeToString(DeployDisplayMode mode) {
	switch (mode) {
	case DeployDisplayMode::Windowed:
		return "windowed";
	case DeployDisplayMode::Fullscreen:
		return "fullscreen";
	default:
		return "borderless";
	}
}

bool LoadJsonRoot(const std::wstring &path, json &root) {
	std::string content;
	if (!ReadFileUtf8(path, content)) {
		return false;
	}

	try {
		root = json::parse(content);
		return root.is_object();
	} catch (const json::parse_error &) {
		return false;
	}
}

bool SaveJsonRoot(const std::wstring &path, const json &root) {
	return WriteFileUtf8(path, root.dump(2));
}

json &EnsureObject(json &parent, const char *key) {
	if (!parent[key].is_object()) {
		parent[key] = json::object();
	}
	return parent[key];
}

void ApplyDisplayFromJson(const json &display, DeployDisplaySettings &out) {
	if (!display.is_object()) {
		return;
	}

	if (display.contains("mode") && display["mode"].is_string()) {
		out.mode = ParseDisplayMode(display["mode"].get<std::string>());
	}
	if (display.contains("resX") && display["resX"].is_number_integer()) {
		out.resX = display["resX"].get<int>();
	}
	if (display.contains("resY") && display["resY"].is_number_integer()) {
		out.resY = display["resY"].get<int>();
	}
	if (display.contains("scale") && display["scale"].is_number()) {
		out.scale = display["scale"].get<float>();
	}
	if (display.contains("renderMatchWindow") &&
	    display["renderMatchWindow"].is_boolean()) {
		out.renderMatchWindow = display["renderMatchWindow"].get<bool>();
	}
	if (display.contains("skipStartupMovies") &&
	    display["skipStartupMovies"].is_boolean()) {
		out.skipStartupMovies = display["skipStartupMovies"].get<bool>();
	}

	if (out.scale < 0.25f) {
		out.scale = 0.25f;
	}
	if (out.scale > 1.0f) {
		out.scale = 1.0f;
	}
}

void WriteDisplayToJson(json &display, const DeployDisplaySettings &settings) {
	display["mode"] = DisplayModeToString(settings.mode);
	display["resX"] = settings.resX;
	display["resY"] = settings.resY;
	display["scale"] = settings.scale;
	display["renderMatchWindow"] = settings.renderMatchWindow;
	display["skipStartupMovies"] = settings.skipStartupMovies;
}

void ApplyLauncherOptionsFromJson(const json &launcher, bool &skipConfigIntegrityCheck) {
	if (launcher.contains("skipConfigIntegrityCheck") &&
	    launcher["skipConfigIntegrityCheck"].is_boolean()) {
		skipConfigIntegrityCheck =
		    launcher["skipConfigIntegrityCheck"].get<bool>();
	}
}

void WriteLauncherOptionsToJson(json &launcher, bool skipConfigIntegrityCheck) {
	launcher["skipConfigIntegrityCheck"] = skipConfigIntegrityCheck;
}

std::string GetIniValue(const std::string &content, const char *section,
                        const char *key, const char *defaultValue) {
	std::string currentSection;
	std::istringstream stream(content);
	std::string line;

	while (std::getline(stream, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}
		if (line.empty() || line[0] == '#') {
			continue;
		}

		if (line.front() == '[' && line.back() == ']') {
			currentSection = line.substr(1, line.size() - 2);
			continue;
		}

		const auto eq = line.find('=');
		if (eq == std::string::npos || currentSection != section) {
			continue;
		}

		auto parsedKey = line.substr(0, eq);
		auto value = line.substr(eq + 1);
		if (parsedKey == key) {
			return value;
		}
	}

	return defaultValue;
}

void MigrateLegacyLauncherIni(json &root, const std::wstring &gameRootHint) {
	const auto legacyPath = GetLegacyIniPath();
	std::string content;
	if (!ReadFileUtf8(legacyPath, content)) {
		return;
	}

	auto &launcher = EnsureObject(root, "launcher");
	if (!gameRootHint.empty()) {
		launcher["gameRoot"] = WideToUtf8(gameRootHint);
	} else {
		const auto iniRoot = GetIniValue(content, "Launcher", "GameRoot", "");
		if (!iniRoot.empty()) {
			launcher["gameRoot"] = iniRoot;
		}
	}

	DeployDisplaySettings display;
	display.mode =
	    ParseDisplayMode(GetIniValue(content, "Display", "Mode", "borderless"));
	display.resX = atoi(GetIniValue(content, "Display", "ResX", "1920").c_str());
	display.resY = atoi(GetIniValue(content, "Display", "ResY", "1080").c_str());
	display.scale =
	    static_cast<float>(atof(GetIniValue(content, "Display", "Scale", "1.0").c_str()));
	const auto renderMatchValue =
	    GetIniValue(content, "Display", "RenderMatchWindow", "");
	if (renderMatchValue.empty()) {
		display.renderMatchWindow = display.mode == DeployDisplayMode::Borderless;
	} else {
		display.renderMatchWindow = renderMatchValue != "0";
	}
	display.skipStartupMovies =
	    GetIniValue(content, "Display", "SkipStartupMovies", "1") != "0";

	auto &displayJson = EnsureObject(launcher, "display");
	WriteDisplayToJson(displayJson, display);
}

void MigrateCoreConfigAutoModules(json &root, const std::wstring &gameRootHint) {
	std::wstring modulesDir = gameRootHint;
	if (modulesDir.empty()) {
		modulesDir = GetLauncherExeDirectory();
	}
	if (!modulesDir.empty()) {
		modulesDir += L"\\modules";
	} else {
		return;
	}

	const auto configPath =
	    modulesDir + L"\\" + Utf8ToWide(MMOD_CORE_CONFIG_FILENAME);
	std::string content;
	if (!ReadFileUtf8(configPath, content)) {
		return;
	}

	try {
		const auto parsed = json::parse(content);
		if (!parsed.contains("loader") || !parsed["loader"].is_object()) {
			return;
		}
		const auto &loader = parsed["loader"];
		if (!loader.contains("autoModules") || !loader["autoModules"].is_array() ||
		    loader["autoModules"].empty()) {
			return;
		}

		auto &mods = EnsureObject(root, "mods");
		if (!mods.contains("autoLoad") || !mods["autoLoad"].is_array() ||
		    mods["autoLoad"].empty()) {
			mods["autoLoad"] = loader["autoModules"];
		}
	} catch (const json::parse_error &) {
	}
}

json DefaultRoot() {
	json root = json::object();
	auto &launcher = EnsureObject(root, "launcher");
	launcher["gameRoot"] = "";
	WriteLauncherOptionsToJson(launcher, true);
	auto &display = EnsureObject(launcher, "display");
	DeployDisplaySettings defaults;
	WriteDisplayToJson(display, defaults);
	auto &mods = EnsureObject(root, "mods");
	mods["autoLoad"] = json::array();
	auto &diagnostics = EnsureObject(root, "diagnostics");
	diagnostics["enabled"] = false;
	return root;
}

bool LoadOrCreateRoot(const std::wstring &path, const std::wstring &gameRootHint,
                      json &root) {
	if (LoadJsonRoot(path, root)) {
		return true;
	}

	root = DefaultRoot();
	MigrateLegacyLauncherIni(root, gameRootHint);
	MigrateCoreConfigAutoModules(root, gameRootHint);
	SaveJsonRoot(path, root);
	return true;
}

} // namespace

namespace DeploySettings {

bool ResolveSettingsPath(std::wstring &path, const std::wstring &gameRootHint) {
	if (!gameRootHint.empty()) {
		path = gameRootHint + L"\\" + Utf8ToWide(MMOD_SETTINGS_JSON_FILENAME);
		return true;
	}

	const auto exeDir = GetLauncherExeDirectory();
	if (exeDir.empty()) {
		return false;
	}

	path = exeDir + L"\\" + Utf8ToWide(MMOD_SETTINGS_JSON_FILENAME);
	return true;
}

bool LoadDisplaySettings(DeployDisplaySettings &out,
                         const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return false;
	}

	json root;
	if (!LoadOrCreateRoot(path, gameRootHint, root)) {
		return false;
	}

	out = DeployDisplaySettings();
	if (root.contains("launcher") && root["launcher"].is_object()) {
		const auto &launcher = root["launcher"];
		if (launcher.contains("display") && launcher["display"].is_object()) {
			ApplyDisplayFromJson(launcher["display"], out);
		}
	}
	return true;
}

bool SaveDisplaySettings(const DeployDisplaySettings &settings,
                         const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return false;
	}

	json root;
	LoadOrCreateRoot(path, gameRootHint, root);
	auto &launcher = EnsureObject(root, "launcher");
	auto &display = EnsureObject(launcher, "display");
	WriteDisplayToJson(display, settings);
	return SaveJsonRoot(path, root);
}

bool LoadGameRoot(std::wstring &gameRoot) {
	std::wstring path;
	if (!ResolveSettingsPath(path, {})) {
		return false;
	}

	json root;
	if (!LoadOrCreateRoot(path, {}, root)) {
		return false;
	}

	gameRoot.clear();
	if (root.contains("launcher") && root["launcher"].is_object()) {
		const auto &launcher = root["launcher"];
		if (launcher.contains("gameRoot") && launcher["gameRoot"].is_string()) {
			gameRoot = Utf8ToWide(launcher["gameRoot"].get<std::string>());
		}
	}
	return !gameRoot.empty();
}

bool SaveGameRoot(const std::wstring &gameRoot) {
	std::wstring oldPath;
	ResolveSettingsPath(oldPath, {});

	json root;
	LoadOrCreateRoot(oldPath, gameRoot, root);
	auto &launcher = EnsureObject(root, "launcher");
	launcher["gameRoot"] = WideToUtf8(gameRoot);

	const std::wstring newPath =
	    gameRoot + L"\\" + Utf8ToWide(MMOD_SETTINGS_JSON_FILENAME);
	return SaveJsonRoot(newPath, root);
}

std::vector<std::string> LoadAutoLoadMods(const std::wstring &gameRootHint) {
	std::vector<std::string> mods;
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return mods;
	}

	json root;
	if (!LoadOrCreateRoot(path, gameRootHint, root)) {
		return mods;
	}

	if (!root.contains("mods") || !root["mods"].is_object()) {
		return mods;
	}

	const auto &modsObj = root["mods"];
	if (!modsObj.contains("autoLoad") || !modsObj["autoLoad"].is_array()) {
		return mods;
	}

	for (const auto &item : modsObj["autoLoad"]) {
		if (item.is_string()) {
			const auto id = item.get<std::string>();
			if (!id.empty()) {
				mods.push_back(id);
			}
		}
	}
	return mods;
}

bool LoadSkipConfigIntegrityCheck(bool &out, const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return false;
	}

	json root;
	if (!LoadOrCreateRoot(path, gameRootHint, root)) {
		return false;
	}

	out = true;
	if (root.contains("launcher") && root["launcher"].is_object()) {
		ApplyLauncherOptionsFromJson(root["launcher"], out);
	}
	return true;
}

bool SaveSkipConfigIntegrityCheck(bool skip, const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return false;
	}

	json root;
	LoadOrCreateRoot(path, gameRootHint, root);
	auto &launcher = EnsureObject(root, "launcher");
	WriteLauncherOptionsToJson(launcher, skip);
	return SaveJsonRoot(path, root);
}

bool LoadSkipUpdateCheck(bool &out, const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return false;
	}

	json root;
	if (!LoadOrCreateRoot(path, gameRootHint, root)) {
		return false;
	}

	out = false;
	if (root.contains("launcher") && root["launcher"].is_object()) {
		const auto &launcher = root["launcher"];
		if (launcher.contains("skipUpdateCheck") &&
		    launcher["skipUpdateCheck"].is_boolean()) {
			out = launcher["skipUpdateCheck"].get<bool>();
		}
	}
	return true;
}

bool SaveSkipUpdateCheck(bool skip, const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return false;
	}

	json root;
	LoadOrCreateRoot(path, gameRootHint, root);
	auto &launcher = EnsureObject(root, "launcher");
	launcher["skipUpdateCheck"] = skip;
	return SaveJsonRoot(path, root);
}

bool LoadDismissedUpdateVersion(std::string &out,
                                const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return false;
	}

	json root;
	if (!LoadOrCreateRoot(path, gameRootHint, root)) {
		return false;
	}

	out.clear();
	if (root.contains("launcher") && root["launcher"].is_object()) {
		const auto &launcher = root["launcher"];
		if (launcher.contains("dismissedUpdateVersion") &&
		    launcher["dismissedUpdateVersion"].is_string()) {
			out = launcher["dismissedUpdateVersion"].get<std::string>();
		}
	}
	return true;
}

bool SaveDismissedUpdateVersion(const std::string &version,
                                const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return false;
	}

	json root;
	LoadOrCreateRoot(path, gameRootHint, root);
	auto &launcher = EnsureObject(root, "launcher");
	launcher["dismissedUpdateVersion"] = version;
	return SaveJsonRoot(path, root);
}

void MigrateLegacySettingsIfNeeded(const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return;
	}

	json root;
	LoadOrCreateRoot(path, gameRootHint, root);
}

bool LoadDiagnosticsEnabled(bool &out, const std::wstring &gameRootHint) {
	std::wstring path;
	if (!ResolveSettingsPath(path, gameRootHint)) {
		return false;
	}

	json root;
	if (!LoadOrCreateRoot(path, gameRootHint, root)) {
		return false;
	}

	out = false;
	if (root.contains("diagnostics") && root["diagnostics"].is_object()) {
		const auto &diag = root["diagnostics"];
		if (diag.contains("enabled") && diag["enabled"].is_boolean()) {
			out = diag["enabled"].get<bool>();
		}
	}
	return true;
}

} // namespace DeploySettings
