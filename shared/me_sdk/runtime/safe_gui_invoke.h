#pragma once

#include <d3d9.h>

namespace MeSdk {
namespace Safe {
namespace Gui {

void InvokeMenuTab(void (*callback)());
void InvokeRenderOverlay(void (*callback)(IDirect3DDevice9 *device),
                         IDirect3DDevice9 *device);

} // namespace Gui
} // namespace Safe
} // namespace MeSdk
