#Requires -Version 5.1
<#
.SYNOPSIS
  Download Microsoft.DXSDK.D3DX NuGet package into third_party/dxsdk (no admin, no full legacy SDK).
#>
$ErrorActionPreference = "Stop"

$Root = Split-Path $PSScriptRoot -Parent
$Dst = Join-Path $Root "third_party\dxsdk"
$IncludeDst = Join-Path $Dst "include"
$LibDst = Join-Path $Dst "lib\x86"
$Pkg = Join-Path $env:TEMP "Microsoft.DXSDK.D3DX.nupkg"
$Zip = Join-Path $env:TEMP "Microsoft.DXSDK.D3DX.zip"
$Extract = Join-Path $env:TEMP "dxsdk-nuget-extract"
$Src = Join-Path $Extract "build\native"

Write-Host "==> Downloading Microsoft.DXSDK.D3DX..." -ForegroundColor Cyan
Invoke-WebRequest -Uri "https://www.nuget.org/api/v2/package/Microsoft.DXSDK.D3DX/9.29.952.8" -OutFile $Pkg -UseBasicParsing
Copy-Item $Pkg $Zip -Force
if (Test-Path $Extract) { Remove-Item $Extract -Recurse -Force }
Expand-Archive -Path $Zip -DestinationPath $Extract -Force

New-Item -ItemType Directory -Force -Path $IncludeDst, $LibDst | Out-Null
Copy-Item (Join-Path $Src "include\*") $IncludeDst -Recurse -Force
Copy-Item (Join-Path $Src "release\lib\x86\*.lib") $LibDst -Force
Copy-Item (Join-Path $Src "debug\lib\x86\*.lib") $LibDst -Force

if (-not (Test-Path (Join-Path $IncludeDst "d3dx9.h"))) {
    throw "d3dx9.h not found after extract"
}

Write-Host "==> Installed D3DX build files to: $Dst" -ForegroundColor Green
