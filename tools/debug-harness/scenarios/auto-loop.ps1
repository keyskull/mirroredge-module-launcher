#Requires -Version 5.1
<#
  Automated scenario loop with retries, optional rebuild, and failure bundles
  for AI/human triage (NDJSON tail, GET_STATUS, manager log).

.EXAMPLE
  .\auto-loop.ps1
  .\auto-loop.ps1 -Scenarios verify-harness,ui-launcher -MaxRetries 1
  .\auto-loop.ps1 -RebuildOnFail -ContinueOnFail
#>
param(
    [string[]]$Scenarios = @("verify-harness", "ui-launcher", "smoke-split", "user-flow"),
    [int]$MaxRetries = 2,
    [switch]$RebuildOnFail,
    [switch]$ContinueOnFail,
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [string]$ReportRoot = ""
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

if ($Scenarios.Count -eq 1 -and $Scenarios[0] -match ',') {
    $Scenarios = $Scenarios[0] -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }
}

$loopParams = @{
    Scenarios      = $Scenarios
    MaxRetries     = $MaxRetries
    RebuildOnFail  = $RebuildOnFail
    ContinueOnFail = $ContinueOnFail
}
if ($SkipLaunch) { $loopParams.SkipLaunch = $true }
if ($SkipBuild) { $loopParams.SkipBuild = $true }
if ($ReportRoot) { $loopParams.ReportRoot = $ReportRoot }

$result = Invoke-HarnessAutoLoop @loopParams

if (-not $result.Pass) {
    Write-Host "auto-loop: FAIL (see $($result.ReportPath))"
    exit 1
}

Write-Host "auto-loop: PASS"
exit 0
