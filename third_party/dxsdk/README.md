# D3DX9 headers and import libraries (build only)

Vendored from the official NuGet package [Microsoft.DXSDK.D3DX](https://www.nuget.org/packages/Microsoft.DXSDK.D3DX) (D3DX9_43).

Used by `engine`, `module_manager`, and related targets for `d3dx9.h` / `d3dx9.lib` at compile time. Runtime ships `d3dx9_43.dll` via the game's DirectX redistributable or the NuGet runtime package.

To refresh from NuGet:

```powershell
.\scripts\setup-dxsdk.ps1
```

Alternatively install the legacy [DirectX SDK June 2010](https://www.microsoft.com/en-us/download/details.aspx?id=6812) system-wide.
