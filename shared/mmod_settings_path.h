#pragma once

#include "module_contract.h"

#include <Windows.h>
#include <string>

inline std::string MmodGetPluginSettingsPathA(const char *filename) {
	char temp[MAX_PATH] = {};
	GetTempPathA(static_cast<DWORD>(sizeof(temp)), temp);
	return std::string(temp) + filename;
}

inline std::string MmodGetSettingsPathA() {
	return MmodGetPluginSettingsPathA(MMOD_SETTINGS_FILENAME);
}
