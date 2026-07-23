#Requires -Version 5.1
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot
if ($SkipLaunch) { throw "mod-deps: requires live game session" }

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild -StopExisting
Wait-ManagerHooksReady -KeepFocused -BootNudge -TimeoutSec 180 | Out-Null

$coreInject = Invoke-ModControlPipe -Command "INJECT core" -Target manager
if ($coreInject -eq "OK") {
    throw "mod-deps: expected INJECT core to be rejected (core is auto-loaded)"
}
if ($coreInject -notlike "ERR*core is auto-loaded*") {
    throw "mod-deps: unexpected INJECT core response: $coreInject"
}
Write-Host "mod-deps: core auto-load guard OK ($coreInject)"

Invoke-EnsureCoreLoaded | Out-Null

$mpInject = Invoke-ModControlPipe -Command "INJECT multiplayer" -Target manager
if ($mpInject -ne "OK") {
    throw "mod-deps: multiplayer inject failed with core present: $mpInject"
}
Write-Host "mod-deps: multiplayer inject OK with core auto-loaded"

try {
    Invoke-ManagerModuleUnload -ModuleId "multiplayer" | Out-Null
} catch {
    Write-Host "mod-deps: WARN multiplayer unload ($($_.Exception.Message))"
}

try {
    Complete-SplitInjectionSession -Context $ctx | Out-Null
} catch {
    Write-Host "mod-deps: teardown warning ($($_.Exception.Message))"
}
exit 0
