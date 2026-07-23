#include <Windows.h>

#include <atomic>
#include <mutex>
#include <vector>

#include <d3d9.h>

#include "presentation_internal.h"
#include "debug_trace.h"
#include "host_api.h"
#include "menu.h"
#include "mod_console.h"
#include "mod_log.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

namespace PresentationInternal {

namespace {

bool IsTextInputKey(int vk) {
	if (vk == VK_SPACE) {
		return true;
	}
	if (vk >= '0' && vk <= '9') {
		return true;
	}
	if (vk >= 'A' && vk <= 'Z') {
		return true;
	}
	return (vk >= VK_OEM_1 && vk <= VK_OEM_8) || vk == VK_OEM_102;
}

} // namespace

void SyncImGuiDisplaySize() {
	if (!g_imguiReady.load()) {
		return;
	}

	HWND hwnd = GetGameWindow();
	if (!hwnd) {
		return;
	}

	RECT rect = {};
	if (!GetClientRect(hwnd, &rect)) {
		return;
	}

	auto &io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<float>(rect.right - rect.left),
	                        static_cast<float>(rect.bottom - rect.top));
}

void EnsureImGui(IDirect3DDevice9 *device) {
	if (g_imguiReady || !device) {
		return;
	}

	std::lock_guard<std::mutex> lock(g_imguiMutex);
	if (g_imguiReady) {
		return;
	}

	HWND hwnd = GetGameWindow();
	if (!hwnd) {
		D3DDEVICE_CREATION_PARAMETERS params = {};
		if (SUCCEEDED(device->GetCreationParameters(&params)) && params.hFocusWindow) {
			hwnd = g_window.hwnd = params.hFocusWindow;
		}
	}
	if (!hwnd || FAILED(device->TestCooperativeLevel())) {
		return;
	}

	ImGui::CreateContext();
	auto &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdana.ttf", 16.0f)) {
		io.Fonts->AddFontDefault();
	}

	RECT rect = {};
	if (GetClientRect(hwnd, &rect)) {
		io.DisplaySize = ImVec2(static_cast<float>(rect.right - rect.left),
		                        static_cast<float>(rect.bottom - rect.top));
	}

	ImGui_ImplWin32_Init(hwnd);
	// #region agent log
	DebugTrace::Event("presentation_imgui.cpp:EnsureImGui", "before_dx9_init", "E",
	                  reinterpret_cast<uintptr_t>(device),
	                  reinterpret_cast<uintptr_t>(hwnd), 0,
	                  static_cast<int>(device->TestCooperativeLevel()));
	// #endregion
	ImGui_ImplDX9_Init(device);
	g_imguiReady = true;
	ModLog::Write("module_manager: imgui initialized");
}

void ClearPendingImGuiMessages() {
	std::lock_guard<std::mutex> lock(g_imguiEventMutex);
	g_pendingImGuiMessages.clear();
}

void ApplyImGuiInputReset() {
	if (!g_imguiReady) {
		return;
	}

	auto &io = ImGui::GetIO();
	io.MouseDrawCursor = false;
	io.WantCaptureMouse = false;
	io.WantCaptureKeyboard = false;
	io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
	for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); ++i) {
		io.MouseDown[i] = false;
	}
	for (int i = 0; i < IM_ARRAYSIZE(io.KeysDown); ++i) {
		io.KeysDown[i] = false;
	}
	// #region agent log
	DebugTrace::Event("presentation_imgui.cpp:ApplyImGuiInputReset", "imgui_reset",
	                  "H-F44", HostMenu::IsOpen() ? 1u : 0u,
	                  g_window.blockInput ? 1u : 0u, 0, 0);
	// #endregion
}

void ProcessImGuiRenderThreadEvents(IDirect3DDevice9 *device) {
	if (g_pendingImGuiDeviceInvalidate.load() && g_imguiReady && device &&
	    device->TestCooperativeLevel() == D3D_OK) {
		g_pendingImGuiDeviceInvalidate = false;
		ImGui_ImplDX9_InvalidateDeviceObjects();
		// #region agent log
		DebugTrace::Event("presentation_imgui.cpp:ProcessImGuiRenderThreadEvents",
		                  "imgui_invalidate", "H-F75", 0, 0, 0, 0);
		// #endregion
	}

	if (!g_imguiReady || !HostApi_WantsOverlay()) {
		ClearPendingImGuiMessages();
		return;
	}

	const HRESULT level = device ? device->TestCooperativeLevel() : D3DERR_INVALIDCALL;
	if (level == D3DERR_DEVICELOST || level == D3DERR_DEVICENOTRESET) {
		// #region agent log
		static std::atomic<int> deferredLog{0};
		if (++deferredLog % 30 == 0) {
			size_t pending = 0;
			{
				std::lock_guard<std::mutex> lock(g_imguiEventMutex);
				pending = g_pendingImGuiMessages.size();
			}
			DebugTrace::Event("presentation_imgui.cpp:ProcessImGuiRenderThreadEvents",
			                  "imgui_deferred", "H-F60",
			                  static_cast<uintptr_t>(level),
			                  static_cast<uintptr_t>(pending), 0, 0);
		}
		// #endregion
	}
}

void PollImGuiMouseButtons() {
	if (!g_imguiReady || (!HostMenu::IsOpen() && !ModConsole::IsOpen())) {
		return;
	}

	auto &io = ImGui::GetIO();
	POINT pos = {};
	if (GetCursorPos(&pos)) {
		HWND hwnd = GetGameWindow();
		if (hwnd && ScreenToClient(hwnd, &pos)) {
			io.MousePos =
			    ImVec2(static_cast<float>(pos.x), static_cast<float>(pos.y));
		}
	}
	io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
}

void PollImGuiKeyboardInput() {
	if (!g_imguiReady) {
		return;
	}

	static bool previousDown[256] = {};
	if (!HostMenu::IsOpen() && !ModConsole::IsOpen()) {
		for (int vk = 0; vk < 256; ++vk) {
			previousDown[vk] = false;
		}
		return;
	}

	auto &io = ImGui::GetIO();
	BYTE keyboardState[256] = {};
	for (int vk = 0; vk < 256; ++vk) {
		if (GetAsyncKeyState(vk) & 0x8000) {
			keyboardState[vk] = 0x80;
		}
	}
	if (GetKeyState(VK_CAPITAL) & 0x0001) {
		keyboardState[VK_CAPITAL] = 0x01;
	}

	const DWORD now = GetTickCount();
	const bool recentCharMessage =
	    g_lastCharMessageTick.load() != 0 && now - g_lastCharMessageTick.load() < 250;

	for (int vk = 0; vk < 256; ++vk) {
		const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
		io.KeysDown[vk] = down;

		if (!down || previousDown[vk] || recentCharMessage || !IsTextInputKey(vk)) {
			previousDown[vk] = down;
			continue;
		}

		wchar_t chars[8] = {};
		const UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
		const int count = ToUnicode(static_cast<UINT>(vk), scan, keyboardState, chars,
		                            static_cast<int>(_countof(chars)), 0);
		if (count > 0) {
			for (int i = 0; i < count; ++i) {
				if (chars[i] >= 0x20) {
					io.AddInputCharacter(static_cast<unsigned int>(chars[i]));
				}
			}
		}

		previousDown[vk] = down;
	}
}

void InvalidateImGuiDeviceObjects() { g_pendingImGuiDeviceInvalidate = true; }

void InvalidateImGuiDeviceObjectsNow(const char *reason, const char *hypothesisId) {
	if (!g_imguiReady) {
		return;
	}

	__try {
		ImGui_ImplDX9_InvalidateDeviceObjects();
		g_pendingImGuiDeviceInvalidate = false;
		// #region agent log
		DebugTrace::Event("presentation_imgui.cpp:InvalidateImGuiDeviceObjectsNow",
		                  reason, hypothesisId, 0, 0, 0, 0);
		// #endregion
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		OutputDebugStringA("presentation_imgui: SEH in InvalidateDeviceObjects\n");
	}
}

bool RenderImGuiDrawDataSafe(ImDrawData *drawData) {
	if (!drawData) {
		return false;
	}
	__try {
		ImGui_ImplDX9_RenderDrawData(drawData);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

} // namespace PresentationInternal
