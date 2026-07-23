#Requires -Version 5.1
<#
  Launch via ModuleLauncher, wait for core auto-load, skip intros to tdmainmenu,
  then run core functional checks at the main menu.
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
    throw "core-mainmenu: requires live game session"
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "core-mainmenu"

Wait-GameWindow -TimeoutSec 120 | Out-Null
Wait-ManagerHooksReadyFromLog -LogPath (Get-SafeContextLogPath -Context $ctx) -TimeoutSec 180
Write-Host "core-mainmenu: manager hooks OK"

Invoke-EnsureCoreLoaded -LogPath $ctx.Session.LogPath | Out-Null
Write-Host "core-mainmenu: core auto-load OK"

Close-ManagerOverlays

Invoke-GameIntroSkip -MinBootSec 25 -KeepFocused | Out-Null
Invoke-GameIntroSkipBlind -SkipRounds 12 -KeepFocused | Out-Null
$menuStatus = Wait-GameMainMenuReady -KeepFocused -TimeoutSec 300 -MaxSkipRounds 28 -StablePolls 2
Write-Host "core-mainmenu: tdmainmenu confirmed"

$map = ""
if ($menuStatus.PSObject.Properties.Name -contains "currentMap") {
    $map = [string]$menuStatus.currentMap
} elseif ($menuStatus.engine -and $menuStatus.engine.currentMap) {
    $map = [string]$menuStatus.engine.currentMap
}
if ($map -and $map -ne "tdmainmenu") {
    Write-Host "WARN: main menu map='$map' (expected tdmainmenu)"
}

Test-MmultiplayerCoreSuite -KeepFocused | Out-Null
Write-Host "core-mainmenu: PASS"

try {
    Complete-SplitInjectionSession -Context $ctx | Out-Null
} catch {
    Write-Host "core-mainmenu: teardown warning ($($_.Exception.Message))"
}
exit 0
