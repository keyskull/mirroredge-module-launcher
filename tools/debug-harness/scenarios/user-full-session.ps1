#Requires -Version 5.1
<#
  Full real-player session (L3 / SendInput only for game actions):
    Launcher real mouse Launch -> boot logo wait -> blind intro skip ->
    grave console inject core -> adaptive intro to main menu ->
    Insert menu / WASD / focus round-trip ->
    optional Enter-level + movement ->
    Alt+F4 quit game -> real mouse Close launcher.

  Visual: keep the game window focused; watch intro skip, inject, menu, and quit.
#>
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [switch]$EnterLevel,
    [int]$PlaySeconds = 12,
    [int]$MinIntroBootSec = 3,
    [int]$BlindIntroRounds = 12,
    [int]$IntroSkipRounds = 15
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot

if ($SkipLaunch) {
    throw "user-full-session: requires live game session (no -SkipLaunch)"
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "user-full-session"

Test-RealUserFullSession -Context $ctx -KeepFocused `
    -MinIntroBootSec $MinIntroBootSec `
    -BlindIntroRounds $BlindIntroRounds `
    -IntroSkipRounds $IntroSkipRounds `
    -EnterLevel:$EnterLevel `
    -PlaySeconds $PlaySeconds | Out-Null

Write-Host "interaction log: $(Get-HarnessInteractionLogPath)"
Write-Host "user-full-session: PASS"
exit 0
