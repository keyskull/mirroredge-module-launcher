#pragma once

#include <d3d9.h>

#include <functional>
#include <vector>

namespace Presentation {

using MainThreadTask = void (*)();

bool InstallBootstrap();
bool AreHooksInstalled();
bool IsOverlayReady();
int GetEndSceneCallCount();
int GetPresentCallCount();
HWND GetGameWindow();
HWND GetLayoutTargetWindow();
void NoteInjectTime();
void OnProxyDeviceCreated(IDirect3DDevice9 *device);
void Pump();
void PumpFromMessageThread();
void RequestWindowLayoutApply();
void SyncWindowLayoutIfNeeded();
void QueueMainThreadTask(MainThreadTask task);
void RenderOverlay(IDirect3DDevice9 *device);
void ShutdownOnProcessDetach();

} // namespace Presentation
