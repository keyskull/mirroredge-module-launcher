#include <d3d9.h>

#include "presentation.h"

extern "C" __declspec(dllexport) void __stdcall
MmOnD3D9DeviceCreated(IDirect3DDevice9 *device) {
    Presentation::OnProxyDeviceCreated(device);
}
