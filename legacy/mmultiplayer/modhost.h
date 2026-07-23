#pragma once

#include "mod_host_api.h"

typedef void (*MenuTabCallback)();

namespace ModHost {
void Attach(const ModHostApi *host);
void SetSelfModule(HMODULE module);
bool IsAttached();
const ModHostApi *Get();
void ForwardLog(const char *message);
void EnsureImGuiContext();
MMOD_MenuTabCallback WrapTabCallback(MenuTabCallback callback);
MMOD_RenderSceneCallback WrapRenderCallback(MMOD_RenderSceneCallback callback);
} // namespace ModHost
