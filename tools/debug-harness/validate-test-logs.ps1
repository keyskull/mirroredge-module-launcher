#Requires -Version 5.1
<#
.SYNOPSIS
  Validate test-logs merge helpers, index schema, and index/latest.json consistency (CI/local).

.EXAMPLE
  .\validate-test-logs.ps1
  .\validate-test-logs.ps1 -FixIndex
#>
param(
    [string]$RepoRoot = "",
    [switch]$FixIndex
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
Import-Module $lib -Force -WarningAction SilentlyContinue

if (-not $RepoRoot) {
    $RepoRoot = Get-RepoRoot
}

Write-Host "validate-test-logs: merge self-test"
Test-HarnessTestLogMerge | Out-Null

$indexPath = Join-Path $RepoRoot "test-logs\index.json"
if (-not (Test-Path $indexPath)) {
    Write-Host "validate-test-logs: skip index checks (no test-logs/index.json)"
    exit 0
}

Write-Host "validate-test-logs: schema"
Test-HarnessTestLogIndexSchema -Path $indexPath -RepoRoot $RepoRoot | Out-Null

if ($FixIndex) {
    Write-Host "validate-test-logs: rebuilding index.json from machines/*/latest.json"
    if (-not (Rebuild-HarnessTestLogIndex -RepoRoot $RepoRoot)) {
        throw "Rebuild-HarnessTestLogIndex failed"
    }
    Test-HarnessTestLogIndexSchema -Path $indexPath -RepoRoot $RepoRoot | Out-Null
    Write-Host "validate-test-logs: index rebuilt"
    exit 0
}

Write-Host "validate-test-logs: index matches latest.json"
Test-HarnessTestLogIndexMatchesLatest -RepoRoot $RepoRoot | Out-Null

Write-Host "validate-test-logs: PASS"
exit 0
