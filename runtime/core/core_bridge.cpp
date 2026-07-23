#include "engine_core_bridge.h"
#include "engine_loader.h"
#include "mod_log.h"
#include "mod_ipc.h"
#include "menu.h"
#include "modhost.h"
#include "settings.h"

#include <cstring>

namespace CoreBridge {

namespace {

typedef void(__cdecl *SetEngineBridgeFn)(const MmCoreBridge *);

static MmCoreBridge g_bridge = {};

void IpcPump() { ModIpc::Pump(); }

void MenuPoll() { Menu::PollToggle(); }

void LogWrite(const char *message) { ModLog::Write(message); }

bool GetSettingBool(const char *section, const char *key, bool defaultValue) {
	return Settings::GetSetting(section, key, defaultValue).get<bool>();
}

int GetSettingInt(const char *section, const char *key, int defaultValue) {
	return Settings::GetSetting(section, key, defaultValue).get<int>();
}

void SetSettingBool(const char *section, const char *key, bool value) {
	Settings::SetSetting(section, key, value);
}

void SetSettingInt(const char *section, const char *key, int value) {
	Settings::SetSetting(section, key, value);
}

bool GetSettingString(const char *section, const char *key, char *out,
                      size_t outSize, const char *defaultValue) {
	const auto value =
	    Settings::GetSetting(section, key, defaultValue ? defaultValue : "")
	        .get<std::string>();
	if (!out || outSize == 0) {
		return false;
	}
	strncpy(out, value.c_str(), outSize - 1);
	out[outSize - 1] = '\0';
	return true;
}

void SetSettingString(const char *section, const char *key, const char *value) {
	Settings::SetSetting(section, key, value ? value : "");
}

void PushBridge() {
	const auto setBridge = reinterpret_cast<SetEngineBridgeFn>(GetProcAddress(
	    EngineLoader::GetModule(), "MMOD_EngineSetCoreBridge"));
	if (setBridge) {
		setBridge(&g_bridge);
	}
}

} // namespace

void Install() {
	g_bridge.host = ModHost::Get();
	g_bridge.wrapRenderScene = ModHost::WrapRenderCallback;
	g_bridge.wrapMenuTab = ModHost::WrapTabCallback;
	g_bridge.ipcPump = &IpcPump;
	g_bridge.menuPoll = &MenuPoll;
	g_bridge.logWrite = &LogWrite;
	g_bridge.getSettingBool = &GetSettingBool;
	g_bridge.getSettingInt = &GetSettingInt;
	g_bridge.setSettingBool = &SetSettingBool;
	g_bridge.setSettingInt = &SetSettingInt;
	g_bridge.getSettingString = &GetSettingString;
	g_bridge.setSettingString = &SetSettingString;
	PushBridge();
}

void Clear() {
	g_bridge = {};
	PushBridge();
}

} // namespace CoreBridge
