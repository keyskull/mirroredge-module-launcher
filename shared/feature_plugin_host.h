#pragma once

#include "mod_host_api.h"

typedef void (*FeatureMenuTabCallback)();

namespace FeaturePluginHost {

void Attach(const ModHostApi *host, HMODULE self);
void Detach();
bool IsAttached();
const ModHostApi *Get();
void ForwardLog(const char *message);
void EnsureImGuiContext();
MMOD_MenuTabCallback WrapTabCallback(FeatureMenuTabCallback callback);
MMOD_RenderSceneCallback WrapRenderCallback(MMOD_RenderSceneCallback callback);
void AddTab(const char *name, FeatureMenuTabCallback callback);
void RemoveTab(const char *name);
void OnRenderScene(MMOD_RenderSceneCallback callback);
void ShowMenu();
void HideMenu();
bool IsMenuOpen();

} // namespace FeaturePluginHost
