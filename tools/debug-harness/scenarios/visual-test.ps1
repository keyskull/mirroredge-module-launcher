#Requires -Version 5.1
<#
  Visual regression harness: image capture + pixel-delta asserts on Module Manager overlay.
  Complements pipe/state checks in ui-module-manager with PrintWindow screenshots.
#>
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [switch]$SkipVisual
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

Test-VisualHarnessPrimitives | Out-Null

$repo = Get-RepoRoot

if ($SkipLaunch) {
    $session = Initialize-DebugSession
    Write-Host "debug session: $($session.SessionId)"
    if (-not $SkipBuild) {
        Invoke-DebugBuild -RepoRoot $repo -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1")
    }
    Write-Host "visual-test: SKIP launch (primitives OK, session initialized)"
    exit 0
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting

Wait-ManagerHooksReady -KeepFocused | Out-Null

$uiParams = @{ KeepFocused = $true }
if ($SkipVisual) { $uiParams.SkipVisual = $true }
Test-ModuleManagerOverlayUi @uiParams | Out-Null

Write-Host "visual-test: PASS (artifacts -> $(Get-VisualArtifactsDir))"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
