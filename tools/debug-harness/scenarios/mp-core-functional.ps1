#Requires -Version 5.1
<#
  core smoke: wait for core auto-load, PING, presentation hooks, Engine + World tab (no crash).
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
    throw "mp-core-functional: requires live game session"
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "mp-core-functional"

Wait-ManagerHooksReady -TimeoutSec 180 | Out-Null

Invoke-EnsureCoreLoaded -LogPath $ctx.Session.LogPath | Out-Null
Test-MmultiplayerCoreSuite -KeepFocused | Out-Null

Write-Host "mp-core-functional: PASS"
try {
    Complete-SplitInjectionSession -Context $ctx | Out-Null
} catch {
    Write-Host "mp-core-functional: teardown warning ($($_.Exception.Message))"
}
exit 0
