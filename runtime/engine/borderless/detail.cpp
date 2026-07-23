#include "detail.h"
#include "window_layout_settings.h"

#include <cstdio>

namespace EngineBorderless {
namespace Detail {

bool g_logged = false;
bool g_forceApply = true;
bool g_resetFailLogged = false;
float g_windowScale = 0.5f;
bool g_enabled = true;
int g_lastAppliedWidth = 0;
int g_lastAppliedHeight = 0;
int g_lastBackBufferWidth = 0;
int g_lastBackBufferHeight = 0;
bool g_d3dSynced = false;

void SaveSettings() {
	const auto path = WindowLayoutSettingsPathA();
	char buffer[32] = {};
	snprintf(buffer, sizeof(buffer), "%.2f", g_windowScale);
	WritePrivateProfileStringA("Window", "Scale", buffer, path);
	WritePrivateProfileStringA("Window", "Enabled", g_enabled ? "1" : "0", path);
}

void MarkApply() {
	g_forceApply = true;
	g_d3dSynced = false;
	g_logged = false;
	g_resetFailLogged = false;
}

} // namespace Detail
} // namespace EngineBorderless
