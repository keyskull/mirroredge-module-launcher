#Requires -Version 5.1
<#
  Simulate real player input at the main menu:
    Insert menu toggle, grave console, typed "inject core",
    WASD + mouse look, focus round-trip, final menu via Escape.

  For full open-to-close lifecycle (launcher click, intro skip, Alt+F4 quit),
  use user-full-session.ps1 instead.
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
    throw "user-flow: requires live game session (no -SkipLaunch)"
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "user-flow"

Test-RealUserGameFlow -Context $ctx -KeepFocused | Out-Null

Write-Host "user-flow: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
