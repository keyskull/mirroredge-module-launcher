#include "safe_gui_invoke.h"

#include <d3d9.h>

#include "safe_seh.h"

namespace MeSdk {
namespace Safe {
namespace Gui {

void InvokeMenuTab(void (*callback)()) {
	if (!callback) {
		return;
	}

	__try {
		callback();
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gui_invoke")) {
	}
}

void InvokeRenderOverlay(void (*callback)(IDirect3DDevice9 *device),
                         IDirect3DDevice9 *device) {
	if (!callback) {
		return;
	}

	__try {
		callback(device);
	} __except (ME_SDK_SAFE_EXCEPT_FILTER("safe_gui_invoke")) {
	}
}

} // namespace Gui
} // namespace Safe
} // namespace MeSdk
