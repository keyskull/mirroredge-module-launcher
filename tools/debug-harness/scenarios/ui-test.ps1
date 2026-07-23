#Requires -Version 5.1
<#
  Full automatic UI test suite:
    1. Launcher Win32 dialog (no game)
    2. In-game Module Manager overlay (control pipe)
#>
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [switch]$LauncherOnly,
    [switch]$OverlayOnly
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot
$common = @{}
if ($SkipBuild) { $common.SkipBuild = $true }

if (-not $OverlayOnly) {
    & (Join-Path $PSScriptRoot "ui-launcher.ps1") @common
}

if (-not $LauncherOnly) {
    if ($SkipLaunch) {
        & (Join-Path $PSScriptRoot "ui-module-manager.ps1") -SkipLaunch @common
    } else {
        & (Join-Path $PSScriptRoot "ui-module-manager.ps1") @common
    }
}

Write-Host "ui-test: PASS"
exit 0
