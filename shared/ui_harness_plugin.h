#pragma once

#include "plugin_ui.h"

#include <Windows.h>
namespace HarnessUi {

typedef void(__stdcall *MmHarnessBeginFrameFn)();
typedef void(__stdcall *MmHarnessRecordRectFn)(const char *id, float minX, float minY,
                                               float maxX, float maxY);

inline MmHarnessBeginFrameFn ResolveBeginFrame() {
	static const auto fn = []() -> MmHarnessBeginFrameFn {
		const HMODULE mod = GetModuleHandleW(L"module_manager.dll");
		if (!mod) {
			return nullptr;
		}
		return reinterpret_cast<MmHarnessBeginFrameFn>(
		    GetProcAddress(mod, "MmHarnessBeginFrame"));
	}();
	return fn;
}

inline MmHarnessRecordRectFn ResolveRecordRect() {
	static const auto fn = []() -> MmHarnessRecordRectFn {
		const HMODULE mod = GetModuleHandleW(L"module_manager.dll");
		if (!mod) {
			return nullptr;
		}
		return reinterpret_cast<MmHarnessRecordRectFn>(
		    GetProcAddress(mod, "MmHarnessRecordRect"));
	}();
	return fn;
}

inline void BeginFrame() {
	if (const auto fn = ResolveBeginFrame()) {
		fn();
	}
}

inline void RecordClient(const char *id, float clientX, float clientY) {
	if (const auto fn = ResolveRecordRect()) {
		fn(id, clientX, clientY, clientX, clientY);
	}
}

inline void RecordRect(const char *id, float minX, float minY, float maxX,
                       float maxY) {
	if (const auto fn = ResolveRecordRect()) {
		fn(id, minX, minY, maxX, maxY);
	}
}

inline void Record(const char *id, HWND /*hwnd*/) {
	if (!id || !id[0]) {
		return;
	}

	ImVec2 min{};
	ImVec2 max{};
	if (!PluginUi::GetItemRectMin(&min) || !PluginUi::GetItemRectMax(&max) ||
	    max.x <= min.x || max.y <= min.y) {
		return;
	}

	RecordRect(id, min.x, min.y, max.x, max.y);
}

} // namespace HarnessUi
