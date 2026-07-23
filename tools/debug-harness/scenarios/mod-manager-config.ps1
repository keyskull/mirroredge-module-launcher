#Requires -Version 5.1
<#
  Module Manager mod discovery, inject/unload, core pipe settings, and plugin
  config UI (Engine / Trainer / Multiplayer / Modules tab targets).

  L2 pass: boot-phase checks (mod discovery, pipe settings, inject UI).
  Menu phase (Engine tab at tdmainmenu) is best-effort; failures are WARN only.
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
    throw "mod-manager-config: requires live game session"
}

$bootPassed = $false
$menuPassed = $false
$menuWarning = ""

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting

try {
    Wait-ManagerHooksReady -KeepFocused -BootNudge -TimeoutSec 180 | Out-Null
    Invoke-EnsureCoreLoaded -LogPath $ctx.Session.LogPath | Out-Null
    Write-Host "mod-manager-config: core auto-load OK"

    Test-ModManagerConfigSuite -KeepFocused -BootOnly | Out-Null
    $bootPassed = $true
    Write-Host "mod-manager-config: boot-phase L2 pass"

    Enable-HarnessIntroHangImmunity -Seconds 180

    try {
        Close-ManagerOverlays
        Assert-GameProcessAlive -Label "before intro skip" -SkipHangCheck | Out-Null

        Invoke-GameIntroSkip -MinBootSec 3 -KeepFocused | Out-Null
        Invoke-GameIntroSkipBlind -SkipRounds 12 -KeepFocused | Out-Null
        Wait-GameMainMenuReady -KeepFocused -TimeoutSec 300 -MaxSkipRounds 28 -StablePolls 2 | Out-Null
        Write-Host "mod-manager-config: tdmainmenu ready"

        Test-ModManagerConfigSuite -KeepFocused -MenuOnly | Out-Null
        $menuPassed = $true
        Write-Host "mod-manager-config: menu-phase pass"
    } catch {
        $menuWarning = $_.Exception.Message
        Write-Host "mod-manager-config: WARN menu phase ($menuWarning)"
    }
} finally {
    try {
        Close-ManagerOverlays
    } catch {
        Write-Host "mod-manager-config: overlay close warning ($($_.Exception.Message))"
    }
    try {
        Complete-SplitInjectionSession -Context $ctx | Out-Null
    } catch {
        Write-Host "mod-manager-config: teardown warning ($($_.Exception.Message))"
    }
}

if (-not $bootPassed) {
    throw "mod-manager-config: boot phase failed"
}

$extra = @{
    bootPass = $true
    menuPass = [bool]$menuPassed
}
if ($menuWarning) {
    $extra.menuWarn = $menuWarning
}
Set-HarnessResultExtra -Extra $extra

if ($menuPassed) {
    Write-Host "mod-manager-config: PASS (boot + menu)"
} else {
    Write-Host "mod-manager-config: PASS (boot only; menu phase WARN)"
}
exit 0
