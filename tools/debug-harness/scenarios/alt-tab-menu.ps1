#Requires -Version 5.1
<#
  KI-2026-002 regression: Module Manager menu open -> Alt+Tab away/back -> device/render recovery.
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
    throw "alt-tab-menu: requires live game session"
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "alt-tab-menu"

Test-HarnessAltTabMenuRecovery -KeepFocused | Out-Null

Write-Host "alt-tab-menu: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
