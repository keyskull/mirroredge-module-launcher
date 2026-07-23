#Requires -Version 5.1
<#
  Cold-start modular refactor smoke: one scenario per fresh game launch.
  Stops MirrorsEdge / ModuleLauncher between runs to avoid pipe/WER flake.
#>
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

function Stop-GameSession {
    $resumeLib = Join-Path $PSScriptRoot "..\..\lib\Resume-ProcessThreads.ps1"
    if (Test-Path $resumeLib) {
        . $resumeLib
        Stop-MirrorsEdgeGameProcesses -WaitSec 5
        return
    }
    Get-Process MirrorsEdge, ModuleLauncher -ErrorAction SilentlyContinue |
        Stop-Process -Force
    Start-Sleep -Seconds 5
}

$scenarios = @(
    "mod-deps",
    "mp-core-functional",
    "multiplayer-functional"
)

$runRoot = Join-Path $PSScriptRoot "..\run.ps1"
$failed = @()

foreach ($name in $scenarios) {
    Stop-GameSession
    Write-Host "`n========== $name (cold) =========="
    & $runRoot $name -SkipBuild:$SkipBuild
    if ($LASTEXITCODE -ne 0) {
        $failed += $name
        Write-Host "STOP: $name failed (exit $LASTEXITCODE)"
        break
    }
    Write-Host "$name OK"
}

Stop-GameSession

if ($failed.Count -gt 0) {
    Write-Host "modular-cold-start: FAIL ($($failed -join ', '))"
    exit 1
}

Write-Host "modular-cold-start: PASS"
exit 0
