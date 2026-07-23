#Requires -Version 5.1
<#
  core real functionality test:
    core SDK/presentation, LIST_MODS/SET_MOD deprecation (split mode), gameplayHooks, settings IPC.
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
    throw "mp-functional: requires live game session (no -SkipLaunch)"
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "mp-functional"

Wait-ManagerHooksReady -KeepFocused | Out-Null

# Clean addon enable flags so each run starts from a known state.
$settingsPath = Join-Path $env:TEMP "core.settings"
if (Test-Path $settingsPath) {
    Remove-Item $settingsPath -Force
    Write-Host "mp-functional: cleared core.settings"
}

Invoke-EnsureCoreLoaded -LogPath $ctx.Session.LogPath | Out-Null
Write-Host "mp-functional: core auto-load OK"

Test-MmultiplayerFunctionalSuite -KeepFocused | Out-Null

Write-Host "mp-functional: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
