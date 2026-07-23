#Requires -Version 5.1
<#
  core + multiplayer in-game GUI test (split injection / hosted mode):
    Module Manager overlay -> inject core -> multiplayer Modules tab ->
    toggle multiplayer mod via ImGui checkbox -> Multiplayer settings tab.

  Uses GET_UI_TARGETS on module_manager pipe; core pipe for status.
#>
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [switch]$SkipInject
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot

if ($SkipLaunch) {
    throw "mp-gui-test: requires live game session (no -SkipLaunch)"
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "mp-gui-test"

Test-MmultiplayerGuiSuite -Context $ctx -KeepFocused -SkipInject:$SkipInject | Out-Null

Write-Host "mp-gui-test: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
