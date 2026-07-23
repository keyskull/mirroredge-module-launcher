#Requires -Version 5.1
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot

if ($SkipLaunch) {
    $session = Initialize-DebugSession
    Write-Host "debug session: $($session.SessionId)"
    if (-not $SkipBuild) {
        Invoke-DebugBuild -RepoRoot $repo -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1")
    }
    Write-Host "smoke-split: SKIP launch (session initialized, build done)"
    exit 0
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "smoke-split"

Wait-ManagerHooksReady -KeepFocused | Out-Null
Assert-GameBootProgress -KeepFocused -TimeoutSec 120 -StaticFrameSec 25 | Out-Null

$logPath = Get-SafeContextLogPath -Context $ctx
$logSeq = Test-LogSequence -Sequence @(
    "direct3d_create9",
    "create_device_ok",
    "hooks_installed"
) -LogPath $logPath -TimeoutSec 90

if (-not $logSeq.Pass) {
    Write-Host "WARN: NDJSON sequence incomplete"
    $logContains = Test-LogContains -Patterns @("hooks_installed") `
        -LogPath $logPath -TimeoutSec 30
    if (-not $logContains.Pass) {
        throw "missing hooks_installed in debug log"
    }
}

Write-Host "smoke-split: PASS"
Stop-HarnessGameSession -IncludeLauncher -Reason "scenario 'smoke-split' pass"
exit 0
