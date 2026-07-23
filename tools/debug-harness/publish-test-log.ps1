#Requires -Version 5.1
<#
.SYNOPSIS
  Publish harness results to test-logs/ (per-machine history + CHANGELOG) and optionally git push.

.EXAMPLE
  .\publish-test-log.ps1 -Suite run-all-scenarios -Results $results -PassCount 4 -TotalCount 19
  .\publish-test-log.ps1 -Suite smoke-split -Pass -DurationMs 120000 -Push
#>
param(
    [Parameter(Mandatory)]
    [string]$Suite,
    [object[]]$Results = @(),
    [int]$PassCount = 0,
    [int]$TotalCount = 0,
    [switch]$Pass,
    [int]$DurationMs = 0,
    [datetime]$StartedAt,
    [object]$GitAtStart = $null,
    [string]$RepoRoot = "",
    [switch]$Push,
    [switch]$NoPush
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
Import-Module $lib -Force

if (-not $RepoRoot) {
    $RepoRoot = Get-RepoRoot
}

Initialize-HarnessTestLogGit -RepoRoot $RepoRoot -Quiet | Out-Null

if (-not $StartedAt) {
    $StartedAt = Get-Date
}

$shouldPush = -not $NoPush
if ($env:MMOD_HARNESS_LOG_PUSH -eq "0") {
    $shouldPush = $false
}
if ($Push) {
    $shouldPush = $true
}

$summary = @{
    passCount  = $PassCount
    totalCount = $TotalCount
    results    = @($Results)
}
if ($Pass) {
    $summary.passCount = 1
    $summary.totalCount = 1
    $summary.singlePass = $true
}
if ($DurationMs -gt 0) {
    $summary.durationMs = $DurationMs
}

$path = Publish-HarnessTestLog -Suite $Suite -StartedAt $StartedAt `
    -Summary $summary -GitAtStart $GitAtStart -RepoRoot $RepoRoot

if ($shouldPush) {
    Push-HarnessTestLog -RepoRoot $RepoRoot -Message $(Split-Path $path -Leaf)
} else {
    Write-Host "test-logs: published locally ($path); push skipped (use -Push or unset MMOD_HARNESS_LOG_PUSH=0)"
}
