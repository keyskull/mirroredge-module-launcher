#Requires -Version 5.1
<#
  Real player playthrough with follower bots:
    Launch game -> inject core -> enter a level from main menu ->
    connect to local multiplayer-server -> spawn bots that chase the player ->
    simulate WASD movement so remote characters follow in-world.

  Visual check: keep the game window focused during the run; you should see
  other player models (Kate, Miller, …) trailing HarnessPlayer.
#>
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [int]$BotCount = 2,
    [int]$PlaySeconds = 25,
    [int]$MinIntroBootSec = 25,
    [int]$IntroSkipRounds = 15
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot

if ($SkipLaunch) {
    throw "mp-playthrough-bots: requires live game session (no -SkipLaunch)"
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "mp-playthrough-bots"

Test-MmultiplayerPlaythroughWithBots -Context $ctx -KeepFocused `
    -BotCount $BotCount -PlaySeconds $PlaySeconds `
    -MinIntroBootSec $MinIntroBootSec -IntroSkipRounds $IntroSkipRounds | Out-Null

Write-Host "interaction log: $(Get-HarnessInteractionLogPath)"
Write-Host "mp-playthrough-bots: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
