#include "mod_plugin_settings.h"

#include "mmod_settings_path.h"
#include "version.h"

#include <Windows.h>

#include <string>

#ifndef MMOD_MOD_ID
#error mod_plugin_settings.cpp requires MMOD_MOD_ID from the plugin version.h
#endif

namespace {

static json settings;

std::string PluginSettingsPath() {
	return MmodGetPluginSettingsPathA(MMOD_MOD_ID ".settings");
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
	auto reset = true;
	const auto path = PluginSettingsPath();

	const auto file =
	    CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		const auto size = GetFileSize(file, nullptr);
		if (size != INVALID_FILE_SIZE && size > 0 && size < 1024 * 1024) {
			std::string data(static_cast<size_t>(size), '\0');
			DWORD read = 0;

			if (ReadFile(file, &data[0], size, &read, nullptr) && read > 0) {
				data.resize(read);
				if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
				    static_cast<unsigned char>(data[1]) == 0xBB &&
				    static_cast<unsigned char>(data[2]) == 0xBF) {
					data.erase(0, 3);
				}
				try {
					settings = json::parse(data);
					reset = false;
				} catch (json::parse_error &) {
				}
			}
		}

		CloseHandle(file);
	}

	if (reset) {
		settings = json::object();
	}
}

void Settings::Reset() {
	settings = json::object();
	Settings::Save();
}

void Settings::Save() {
	const auto dump = settings.dump();
	const auto path = PluginSettingsPath();

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
