#Requires -Version 5.1
<#
.SYNOPSIS
  Run an AI debug harness scenario.
  On exit writes a machine-parseable JSON result line to stdout:
    harness-result: {"scenario":"...","layer":"L0","pass":true,"durationMs":123}

.EXAMPLE
  .\run.ps1 verify-harness
  .\run.ps1 smoke-split
  .\run.ps1 smoke-split -SkipLaunch
  .\run.ps1 auto-loop -Scenarios verify-harness,ui-launcher -MaxRetries 1
#>
param(
    [Parameter(Position = 0)]
    [string]$Scenario = "verify-harness",
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [switch]$EnterLevel,
    [int]$PlaySeconds = -1,
    [string[]]$Scenarios,
    [int]$MaxRetries = -1,
    [switch]$RebuildOnFail,
    [switch]$ContinueOnFail,
    [string]$ReportRoot = ""
)

$ErrorActionPreference = "Stop"
$scriptPath = Join-Path $PSScriptRoot "scenarios\$Scenario.ps1"
if (-not (Test-Path $scriptPath)) {
    Import-Module (Join-Path $PSScriptRoot "lib\DebugHarness.psm1") -Force
    $available = Get-DebugScenarios -join ", "
    throw "Unknown scenario: $Scenario. Available: $available"
}

Import-Module (Join-Path $PSScriptRoot "lib\DebugHarness.psm1") -Force
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Initialize-HarnessTestMachine -RepoRoot $repo | Out-Null

# Drop old screenshots / empty session dirs before a scenario run.
$clearTemp = Join-Path $PSScriptRoot "tools\clear-harness-temp.ps1"
if (Test-Path -LiteralPath $clearTemp) {
    . $clearTemp
    $purge = Clear-HarnessTempArtifacts -RetainDays 2 -KeepNewestShots 40 -KeepNewestReflections 20
    Write-Host ("clear-harness-temp: files={0} dirs={1} freedMB={2}" -f `
        $purge.removedFiles, $purge.removedDirs, $purge.freedMB)
}

$forward = @{}
if ($SkipLaunch) { $forward.SkipLaunch = $true }
if ($SkipBuild) { $forward.SkipBuild = $true }
if ($EnterLevel) { $forward.EnterLevel = $true }
if ($PlaySeconds -ge 0) { $forward.PlaySeconds = $PlaySeconds }
if ($Scenarios) { $forward.Scenarios = $Scenarios }
if ($MaxRetries -ge 0) { $forward.MaxRetries = $MaxRetries }
if ($RebuildOnFail) { $forward.RebuildOnFail = $true }
if ($ContinueOnFail) { $forward.ContinueOnFail = $true }
if ($ReportRoot) { $forward.ReportRoot = $ReportRoot }

# Measure and wrap: every scenario → JSON result line + consistent exit.
$startTick = [System.Diagnostics.Stopwatch]::StartNew()
$pass = $false
$errorMsg = ""
$exitCode = 1

try {
    & $scriptPath @forward
    $exitCode = $LASTEXITCODE
    $pass = ($exitCode -eq 0)
    if (-not $pass) {
        $errorMsg = "scenario exited with code $exitCode"
    }
} catch {
    $exitCode = 1
    $errorMsg = $_.Exception.Message
    Write-Host "ERROR: $errorMsg"
} finally {
    $libPath = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
    Import-Module $libPath -Force -ErrorAction SilentlyContinue
    if (Get-Command Stop-HarnessGameSession -ErrorAction SilentlyContinue) {
        $resumeLib = Join-Path $PSScriptRoot "..\lib\Resume-ProcessThreads.ps1"
        $hasLiveGame = $false
        if (Test-Path $resumeLib) {
            . $resumeLib
            foreach ($proc in @(Get-Process MirrorsEdge -ErrorAction SilentlyContinue)) {
                if ((Get-ProcessThreadCount -ProcessId $proc.Id) -gt 0) {
                    $hasLiveGame = $true
                    break
                }
            }
        } else {
            $hasLiveGame = [bool](Get-Process MirrorsEdge -ErrorAction SilentlyContinue |
                Where-Object { $_.Threads.Count -gt 0 })
        }
        $launcherRunning = Get-Process ModuleLauncher -ErrorAction SilentlyContinue
        if ($hasLiveGame -or $launcherRunning) {
            Stop-HarnessGameSession -IncludeLauncher `
                -Reason "scenario '$Scenario' exit (code $exitCode)" | Out-Null
        }
    }
}

$startTick.Stop()
$importBlock = @{}
Import-Module (Join-Path $PSScriptRoot "lib\DebugHarness.psm1") -Force -ErrorAction SilentlyContinue `
    -PassThru | ForEach-Object { $outer = $_ } | Out-Null

# No $outer trick needed; just call the function after import.
if (-not (Get-Command Write-HarnessResult -ErrorAction SilentlyContinue)) {
    Import-Module (Join-Path $PSScriptRoot "lib\DebugHarness.psm1") -Force
}

Write-HarnessResult -Scenario $Scenario -Pass $pass `
    -DurationMs $startTick.ElapsedMilliseconds -Error $errorMsg

exit $exitCode
