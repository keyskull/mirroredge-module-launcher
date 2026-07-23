#include "presentation_internal.h"

namespace PresentationInternal {

WindowState g_window;
RenderState g_render;

std::atomic<bool> g_bootstrapInstalled{false};
std::atomic<bool> g_hooksInstalled{false};
std::atomic<bool> g_imguiReady{false};
std::atomic<int> g_focusCooldown{0};
std::atomic<int> g_preModFocusCooldown{0};
std::atomic<int> g_stableFrames{0};
std::atomic<DWORD> g_injectTick{0};
std::atomic<bool> g_gameForegroundSinceInject{false};

std::mutex g_presentationHookMutex;
std::mutex g_taskMutex;
std::vector<Presentation::MainThreadTask> g_tasks;
std::mutex g_imguiMutex;

IDirect3DDevice9 *g_cachedDevice = nullptr;
std::atomic<int> g_endSceneCalls{0};
std::atomic<int> g_presentCalls{0};
std::atomic<bool> g_lazyGateLogged{false};
std::atomic<bool> g_deviceMissLogged{false};
std::atomic<bool> g_proxyDeviceReceived{false};
std::atomic<DWORD> g_proxyDeviceTick{0};
std::atomic<int> g_postFocusWatch{0};
std::atomic<bool> g_pendingFocusSync{false};
std::atomic<bool> g_pendingMenuHide{false};
std::atomic<bool> g_pendingImGuiReset{false};
std::atomic<bool> g_pendingImGuiDeviceInvalidate{false};
std::atomic<DWORD> g_lastCharMessageTick{0};
std::mutex g_imguiEventMutex;
std::vector<MSG> g_pendingImGuiMessages;

HRESULT lastCoop = D3D_OK;
DWORD g_lastHookAttemptTick = 0;

} // namespace PresentationInternal
