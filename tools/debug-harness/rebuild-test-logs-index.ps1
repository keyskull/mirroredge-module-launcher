#Requires -Version 5.1
<#
.SYNOPSIS
  Regenerate test-logs/index.json from machines/*/latest.json (authoritative per-machine source).
#>
param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
Import-Module $lib -Force -WarningAction SilentlyContinue

if (-not $RepoRoot) {
    $RepoRoot = Get-RepoRoot
}

if (-not (Rebuild-HarnessTestLogIndex -RepoRoot $RepoRoot)) {
    throw "rebuild failed: no machines/*/latest.json found"
}

Test-HarnessTestLogIndexSchema -RepoRoot $RepoRoot | Out-Null
Write-Host "rebuild-test-logs-index: OK -> $(Join-Path $RepoRoot 'test-logs\index.json')"
