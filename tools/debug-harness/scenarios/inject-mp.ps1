#Requires -Version 5.1
<#
.SYNOPSIS
  After split injection hooks are ready, wait for core auto-load via control pipe.
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
    Write-Host "inject-mp: requires live game session (no -SkipLaunch)"
    exit 1
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "inject-mp"

Wait-ManagerHooksReady -KeepFocused | Out-Null

$ready = Invoke-EnsureCoreLoaded -LogPath (Get-SafeContextLogPath -Context $ctx)
Write-Host "core auto-load: OK"
Assert-GameBootProgress -KeepFocused -TimeoutSec 120 -StaticFrameSec 25 | Out-Null

$logContains = Test-LogContains -Patterns @("init_ready") `
    -LogPath $ctx.Session.LogPath -TimeoutSec 30
if (-not $logContains.Pass) {
    Write-Host "WARN: init_ready not in NDJSON (ready source=$($ready.source))"
}

try {
    $status = Invoke-ModControlPipe -Command "GET_STATUS" -Target core -TimeoutMs 8000 | ConvertFrom-Json
    if (-not (Get-EngineModReadyFromStatus $status)) {
        throw "core GET_STATUS engine.modReady=false"
    }
    Write-Host ("inject-mp: GET_STATUS engine.modReady=true hosted={0}" -f $status.hostedMode)
} catch {
    if ($ready.modReady -eq $true) {
        Write-Host "WARN: GET_STATUS unavailable after ready event: $($_.Exception.Message)"
    } else {
        throw $_
    }
}

Write-Host "inject-mp: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
