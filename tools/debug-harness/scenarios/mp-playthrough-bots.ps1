#Requires -Version 5.1
<#
  Real player playthrough with follower bots (KI-2026-005).

  Delegates to the verified mp-real-level-bots entry path:
    START_NEW_GAME -> tutorial_p -> ENSURE_GAMEPLAY_HOOKS -> MP inject ->
    FORCE_HOSTED_LIVE -> Follow bots -> SoftProbe / movement.

  Do NOT use pre-level ENSURE_GAMEPLAY_HOOKS or Enter-only menu navigation
  (KI-2026-005 Failed approaches).
#>
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [int]$BotCount = 2,
    [int]$PlaySeconds = 90,
    [int]$MinIntroBootSec = 25,
    [int]$IntroSkipRounds = 15
)

$ErrorActionPreference = "Stop"

if ($SkipLaunch) {
    throw "mp-playthrough-bots: requires live game session (no -SkipLaunch)"
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$realLevel = Join-Path $repoRoot "tools\mp-real-level-bots.ps1"
if (-not (Test-Path -LiteralPath $realLevel)) {
    throw "mp-playthrough-bots: missing $realLevel"
}

Write-Host "mp-playthrough-bots: delegating to mp-real-level-bots (KI-2026-005 verified path)"
& $realLevel -BotCount $BotCount -PlaySeconds $PlaySeconds -SkipBuild:$SkipBuild
$exit = $LASTEXITCODE
if ($null -eq $exit) { $exit = 0 }
if ($exit -ne 0) {
    throw "mp-playthrough-bots: mp-real-level-bots failed EXIT=$exit"
}
Write-Host "mp-playthrough-bots: PASS (via mp-real-level-bots)"
exit 0
