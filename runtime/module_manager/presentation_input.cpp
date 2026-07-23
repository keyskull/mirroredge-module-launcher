#include <Windows.h>

#include <atomic>

#include <d3d9.h>

#include "presentation_internal.h"
#include "debug_trace.h"
#include "host_api.h"
#include "menu.h"
#include "mod_console.h"
#include "presentation.h"
#include "win_input.h"
#include "window_layout_settings.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

namespace PresentationInternal {

void ReleaseGameCapture() {
	if (const HWND cap = GetCapture()) {
		DWORD pid = 0;
		GetWindowThreadProcessId(cap, &pid);
		if (pid == GetCurrentProcessId()) {
			ReleaseCapture();
		}
	}
	if (!WindowLayout_IsEnabled()) {
		ClipCursor(nullptr);
	}
}

void ProcessDeferredFocusLoss() {
	if (g_window.blockInput) {
		HostMenu_SetBlockInput(false);
	}
	if (HostMenu::IsOpen()) {
		HostMenu::Hide();
	}
	if (ModConsole::IsOpen()) {
		ModConsole::Hide();
	}
	g_pendingMenuHide = false;
	ClearPendingImGuiMessages();
	const HWND gameWnd = GetGameWindow();
	WinInput_CancelImeComposition(gameWnd);
	WinInput_ReleaseOverlayCapture(gameWnd);
	ApplyImGuiInputReset();
	InvalidateImGuiDeviceObjectsNow("imgui_focus_loss", "H-F82");
}

void PollUnfocusInputRelease() {
	if (!g_hooksInstalled.load()) {
		return;
	}
	if (IsOurProcessForeground()) {
		return;
	}
	if (!HostMenu::IsOpen() && !ModConsole::IsOpen() && !g_window.blockInput) {
		return;
	}
	ProcessDeferredFocusLoss();
}

void ProcessLostFrameSideEffects(IDirect3DDevice9 *device) {
	(void)device;
	PollUnfocusInputRelease();

	if (g_pendingMenuHide.exchange(false)) {
		if (HostMenu::IsOpen()) {
			HostMenu::Hide();
			InvalidateImGuiDeviceObjectsNow("imgui_menu_hide_lost", "H-F82");
		}
	}

	if (g_pendingImGuiReset.exchange(false)) {
		ApplyImGuiInputReset();
	}

	if (g_pendingImGuiDeviceInvalidate.load()) {
		InvalidateImGuiDeviceObjectsNow("imgui_pending_invalidate", "H-F82");
	}
}

void TryInvalidateImGuiBeforeDeviceReset(IDirect3DDevice9 *device) {
	if (!g_imguiReady || !device) {
		return;
	}

	__try {
		const HRESULT level = device->TestCooperativeLevel();
		if (level == D3DERR_DEVICENOTRESET || level == D3DERR_DEVICELOST) {
			InvalidateImGuiDeviceObjectsNow(
			    level == D3DERR_DEVICELOST ? "imgui_activate_lost" : "imgui_activate_reset",
			    "H-F82");
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		OutputDebugStringA("presentation_input: SEH in TestCooperativeLevel\n");
	}
}

static void QueueFocusSync() { g_pendingFocusSync = true; }

void SyncInputBlockWithForeground(IDirect3DDevice9 *device) {
	if (!g_hooksInstalled.load()) {
		return;
	}

	if (!IsOurProcessForeground()) {
		if (HostMenu::IsOpen()) {
			HostMenu::Hide();
			InvalidateImGuiDeviceObjectsNow("imgui_unfocus_hide", "H-F82");
		} else {
			g_pendingMenuHide = true;
		}
		if (g_window.blockInput) {
			HostMenu_SetBlockInput(false);
			WinInput_CancelImeComposition(GetGameWindow());
			WinInput_ReleaseOverlayCapture(GetGameWindow());
			ApplyImGuiInputReset();
		}
		ReleaseGameCapture();
		return;
	}

	IDirect3DDevice9 *activeDevice = device ? device : g_cachedDevice;
	const bool deviceReady = IsDeviceCooperativeReady(activeDevice);
	const bool wantBlock =
	    (HostMenu::IsOpen() || ModConsole::IsOpen()) && g_imguiReady.load() && deviceReady;
	if (g_window.blockInput != wantBlock) {
		HostMenu_SetBlockInput(wantBlock);
	}
}

void HandleFocus(LPMSG msg) {
	if (!msg) {
		return;
	}

	switch (msg->message) {
	case WM_ACTIVATEAPP:
		// #region agent log
		DebugTrace::Event("presentation_input.cpp:HandleFocus", "WM_ACTIVATEAPP", "H-F4",
		                  msg->wParam ? 1u : 0u, g_window.blockInput ? 1u : 0u,
		                  HostMenu::IsOpen() ? 1u : 0u, 0);
		// #endregion
		if (!msg->wParam) {
			ProcessDeferredFocusLoss();
		} else {
			g_gameForegroundSinceInject = true;
			TryInvalidateImGuiBeforeDeviceReset(g_cachedDevice);
		}
		QueueFocusSync();
		break;
	case WM_ACTIVATE:
		if (LOWORD(msg->wParam) != WA_INACTIVE) {
			g_gameForegroundSinceInject = true;
		} else {
			ProcessDeferredFocusLoss();
		}
		QueueFocusSync();
		break;
	case WM_SETFOCUS:
		g_gameForegroundSinceInject = true;
		QueueFocusSync();
		break;
	case WM_KILLFOCUS:
		ProcessDeferredFocusLoss();
		QueueFocusSync();
		break;
	case WM_CLOSE:
	case WM_DESTROY:
		Presentation::ShutdownOnProcessDetach();
		break;
	case WM_SIZE:
	case WM_ENTERSIZEMOVE:
	case WM_EXITSIZEMOVE:
		QueueFocusSync();
		break;
	default:
		break;
	}
}

static void ForwardImGuiWin32Message(LPMSG msg) {
	if (!msg || !g_imguiReady) {
		return;
	}
	ImGui_ImplWin32_WndProcHandler(msg->hwnd, msg->message, msg->wParam, msg->lParam);
}

void HandleInput(LPMSG msg) {
	if (!msg || !g_hooksInstalled.load() || !g_window.blockInput) {
		return;
	}

	const auto msgId = msg->message;
	if (msgId == WM_SIZE || WinInput_MustPassThrough(msgId)) {
		return;
	}

	if (g_imguiReady) {
		ForwardImGuiWin32Message(msg);
		if (ImGui::GetIO().WantCaptureKeyboard && WinInput_IsKeyboardInput(msgId)) {
			msg->message = WM_NULL;
		}
	}
	if (msgId == WM_CHAR || msgId == WM_SYSCHAR || msgId == WM_UNICHAR) {
		g_lastCharMessageTick = GetTickCount();
	}

	if (WinInput_ShouldSwallowForBlockInput(msgId)) {
		msg->message = WM_NULL;
	}
}

} // namespace PresentationInternal

void HostMenu_SetBlockInput(bool block) {
	using namespace PresentationInternal;
	// #region agent log
	if (g_window.blockInput != block) {
		DebugTrace::Event("presentation_input.cpp:HostMenu_SetBlockInput",
		                  "block_change", "H-F1", block ? 1u : 0u,
		                  HostMenu::IsOpen() ? 1u : 0u, 0, 0);
	}
	// #endregion
	if (block && g_hooksInstalled.load() && !g_bootstrapInstalled.load()) {
		Presentation::InstallBootstrap();
	}
	if (g_window.blockInput && !block) {
		WinInput_CancelImeComposition(GetGameWindow());
		WinInput_ReleaseOverlayCapture(GetGameWindow());
	}
	g_window.blockInput = block;
	if (g_imguiReady) {
		ImGui::GetIO().MouseDrawCursor = block;
		if (!block) {
			g_pendingImGuiReset = true;
		}
	}
}
