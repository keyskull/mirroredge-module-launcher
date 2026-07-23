#include <string>
#include <Windows.h>

#include "settings.h"

#include "json.h"
#include "mmod_settings_path.h"
#include "module_contract.h"

static json settings;

namespace {

json MergeObjects(const json &base, const json &overlay) {
	if (!overlay.is_object()) {
		return base.is_object() ? base : json::object();
	}
	if (!base.is_object()) {
		return overlay;
	}

	json merged = base;
	for (auto it = overlay.begin(); it != overlay.end(); ++it) {
		if (it.value().is_object() && merged[it.key()].is_object()) {
			merged[it.key()] = MergeObjects(merged[it.key()], it.value());
		} else {
			merged[it.key()] = it.value();
		}
	}
	return merged;
}

bool ReadJsonFileA(const std::string &path, json &out) {
	const auto file =
	    CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	const auto size = GetFileSize(file, nullptr);
	if (size == INVALID_FILE_SIZE || size == 0 || size >= 1024 * 1024) {
		CloseHandle(file);
		return false;
	}

	std::string data(static_cast<size_t>(size), '\0');
	DWORD read = 0;
	const bool ok =
	    ReadFile(file, &data[0], size, &read, nullptr) != FALSE && read > 0;
	CloseHandle(file);
	if (!ok) {
		return false;
	}

	data.resize(read);
	if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
	    static_cast<unsigned char>(data[1]) == 0xBB &&
	    static_cast<unsigned char>(data[2]) == 0xBF) {
		data.erase(0, 3);
	}

	try {
		out = json::parse(data);
		return true;
	} catch (json::parse_error &) {
		return false;
	}
}

std::string GetDeployConfigPathA() {
	wchar_t dllPath[MAX_PATH] = {};
	HMODULE self = GetModuleHandleW(L"core.dll");
	if (!self) {
		self = GetModuleHandleW(L"mm-core.dll");
	}
	if (!self || !GetModuleFileNameW(self, dllPath, MAX_PATH)) {
		return {};
	}

	std::wstring path(dllPath);
	const auto dllSlash = path.find_last_of(L"\\/");
	if (dllSlash == std::wstring::npos) {
		return {};
	}

	path.resize(dllSlash);
	const auto folderSlash = path.find_last_of(L"\\/");
	if (folderSlash == std::wstring::npos) {
		return {};
	}

	path.resize(folderSlash);
	path += L"\\";
	const std::string configUtf8(MMOD_CORE_CONFIG_FILENAME);
	const int configWideLen = MultiByteToWideChar(
	    CP_UTF8, 0, configUtf8.c_str(), -1, nullptr, 0);
	if (configWideLen <= 0) {
		return {};
	}
	std::wstring configWide(static_cast<size_t>(configWideLen), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, configUtf8.c_str(), -1, &configWide[0],
	                    configWideLen);
	if (!configWide.empty() && configWide.back() == L'\0') {
		configWide.pop_back();
	}
	path += configWide;

	const int needed =
	    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (needed <= 0) {
		return {};
	}

	std::string utf8(static_cast<size_t>(needed), '\0');
	WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &utf8[0], needed, nullptr,
	                    nullptr);
	if (!utf8.empty() && utf8.back() == '\0') {
		utf8.pop_back();
	}
	return utf8;
}

} // namespace

void Settings::SetSetting(const char *menu, const char *key, json value) {
	if (settings[menu].is_null()) {
		settings[menu] = json::object();
	}

	settings[menu][key] = value;
	Settings::Save();
}

json Settings::GetSetting(const char *menu, const char *key, json defaultValue) {
	if (settings[menu].is_null()) {
		settings[menu] = json::object();
	}

	auto &v = settings[menu][key];
	if (v.is_null() || (v.type() != defaultValue.type() && v.is_number() != defaultValue.is_number())) {
		v = defaultValue;
	}

	return v;
}

void Settings::Load() {
	settings = json::object();

	json deployConfig;
	const auto deployPath = GetDeployConfigPathA();
	if (!deployPath.empty()) {
		ReadJsonFileA(deployPath, deployConfig);
		if (deployConfig.is_object()) {
			settings = deployConfig;
		}
	}

	auto path = MmodGetSettingsPathA();
	if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		const auto legacyCore =
		    MmodGetPluginSettingsPathA(MMOD_LEGACY_CORE_SETTINGS_FILENAME);
		if (GetFileAttributesA(legacyCore.c_str()) != INVALID_FILE_ATTRIBUTES) {
			path = legacyCore;
		} else {
			const auto legacy =
			    MmodGetPluginSettingsPathA(MMOD_LEGACY_SETTINGS_FILENAME);
			if (GetFileAttributesA(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) {
				path = legacy;
			}
		}
	}

	json userSettings;
	if (ReadJsonFileA(path, userSettings) && userSettings.is_object()) {
		settings = MergeObjects(settings, userSettings);
	}
}

void Settings::Reset() {
	settings = json::object();
	Settings::Save();
}

void Settings::Save() {
	const auto dump = settings.dump();
	const auto path = MmodGetSettingsPathA();

	const auto file =
	    CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
	                FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		printf("settings: failed to save %s\n", path.c_str());
		return;
	}

	DWORD written = 0;
	if (!WriteFile(file, dump.data(), static_cast<DWORD>(dump.size()), &written,
	               nullptr) ||
	    written != dump.size()) {

		printf("settings: failed to write %s\n", path.c_str());
	}

	CloseHandle(file);
}
