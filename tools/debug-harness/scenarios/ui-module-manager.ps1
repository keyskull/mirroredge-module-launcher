#Requires -Version 5.1
<#
  In-game Module Manager overlay UI test via control pipe.
#>
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot

if ($SkipLaunch) {
    $session = Initialize-DebugSession
    Write-Host "debug session: $($session.SessionId)"
    if (-not $SkipBuild) {
        Invoke-DebugBuild -RepoRoot $repo -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1")
    }
    Write-Host "ui-module-manager: SKIP launch (session initialized, build done)"
    exit 0
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting

Wait-ManagerHooksReady -KeepFocused | Out-Null
Test-ModuleManagerOverlayUi -KeepFocused | Out-Null

Write-Host "ui-module-manager: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
