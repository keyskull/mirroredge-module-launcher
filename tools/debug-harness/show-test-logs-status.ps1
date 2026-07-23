#Requires -Version 5.1
<#
.SYNOPSIS
  Print cross-machine harness status from test-logs/index.json.

.EXAMPLE
  .\show-test-logs-status.ps1
  .\show-test-logs-status.ps1 -Json
#>
param(
    [string]$RepoRoot = "",
    [switch]$Json
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
Import-Module $lib -Force -WarningAction SilentlyContinue

if (-not $RepoRoot) {
    $RepoRoot = Get-RepoRoot
}

Show-HarnessTestLogStatus -RepoRoot $RepoRoot -Json:$Json | Out-Null
