#Requires -Version 5.1
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot
if ($SkipLaunch) { throw "multiplayer-functional: requires live game session" }

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild -StopExisting
Wait-ManagerHooksReady -KeepFocused -BootNudge -TimeoutSec 180 | Out-Null
Invoke-EnsureCoreLoaded -LogPath $ctx.Session.LogPath | Out-Null

$mpResult = Invoke-ModControlPipe -Command "INJECT multiplayer" -Target manager
if ($mpResult -ne "OK") {
    throw "INJECT multiplayer failed: $mpResult"
}
Wait-ModuleManagerLoadLog -ModuleId "multiplayer" -TimeoutSec 120 | Out-Null
Invoke-EnsurePlaythroughRuntimeHooks -TimeoutSec 45 -KeepFocused

Test-MultiplayerFunctionalSuite -KeepFocused | Out-Null
Write-Host "multiplayer-functional: PASS"
try {
    Complete-SplitInjectionSession -Context $ctx | Out-Null
} catch {
    Write-Host "multiplayer-functional: teardown warning ($($_.Exception.Message))"
}
exit 0
