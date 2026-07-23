#Requires -Version 5.1
param([switch]$SkipLaunch, [switch]$SkipBuild)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force
$repo = Get-RepoRoot
if ($SkipLaunch) { throw "dolly-functional: requires live game session" }
$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild -StopExisting
Wait-ManagerHooksReady -KeepFocused -BootNudge -TimeoutSec 180 | Out-Null
Invoke-EnsureCoreLoaded -LogPath $ctx.Session.LogPath | Out-Null
Invoke-ModControlPipe -Command "INJECT dolly" -Target manager | Out-Null
Wait-ModuleManagerLoadLog -ModuleId "dolly" -TimeoutSec 120 | Out-Null
Test-MpDollyFunctionalSuite -KeepFocused | Out-Null
Write-Host "dolly-functional: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
