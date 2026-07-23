#pragma once

#include "mod_host_api.h"

const ModHostApi *HostApi_Get();
void HostApi_RenderScene(IDirect3DDevice9 *device);
void HostApi_PresentationTick(IDirect3DDevice9 *device);
void HostApi_PresentationInputSync();
bool HostApi_WantsOverlay();
