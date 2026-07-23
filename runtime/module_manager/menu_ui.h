#pragma once

#include <d3d9.h>

#include "menu.h"

namespace HostMenuUi {

void RenderOverlay(IDirect3DDevice9 *device);
HostMenuTabCallback ModulesTabCallback();

} // namespace HostMenuUi
