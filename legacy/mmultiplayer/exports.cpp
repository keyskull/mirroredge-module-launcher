#include "engine.h"

extern "C" __declspec(dllexport) void __stdcall MmOnDirect3D9Created(
    IDirect3D9 *d3d) {
	Engine::HookDirect3D9Interface(d3d);
}

extern "C" __declspec(dllexport) void __stdcall MmOnD3D9DeviceCreated(
    IDirect3DDevice9 *device) {
	Engine::OnProxyDeviceCreated(device);
}
