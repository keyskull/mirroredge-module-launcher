#pragma once

namespace EngineBorderless {
namespace Detail {

extern bool g_enabled;
extern float g_windowScale;
extern bool g_forceApply;
extern bool g_d3dSynced;
extern bool g_logged;
extern bool g_resetFailLogged;
extern int g_lastAppliedWidth;
extern int g_lastAppliedHeight;
extern int g_lastBackBufferWidth;
extern int g_lastBackBufferHeight;

void SaveSettings();
void MarkApply();

} // namespace Detail
} // namespace EngineBorderless
