#include "engine_presentation_internal.h"

namespace EnginePresentationInternal {

RenderSceneState renderScene;
PresentSceneState presentScene;
WindowState window;

std::atomic<bool> presentationHooksInstalled{false};
std::mutex presentationHookMutex;
IDirect3DDevice9 *capturedDevice = nullptr;
IDirect3DDevice9 *bootstrapDevice = nullptr;
IDirect3DDevice9 *g_presentationHookDevice = nullptr;

Engine::MainThreadTask deferredInitTask = nullptr;
std::atomic<bool> deferredInitRan{false};
std::atomic<bool> deferredInitQueued{false};
std::atomic<int> presentStableFrames{0};
std::atomic<int> focusCooldownFrames{0};
std::atomic<int> preModFocusCooldown{0};
std::atomic<bool> gameForegroundSinceInject{false};
HRESULT lastCooperativeLevel = D3D_OK;
DWORD injectTick = 0;

IDirect3D9 *(WINAPI *Direct3DCreate9Original)(UINT) = nullptr;
std::atomic<bool> d3d9ExportHooked{false};
std::atomic<bool> rendererManagedByProxy{false};

std::mutex mainThreadTaskMutex;
std::vector<void (*)()> mainThreadTasks;

} // namespace EnginePresentationInternal
