#Requires -Version 5.1
<#
.SYNOPSIS
  Merge test-logs/index.json inputs (accept both machines; normalize legacy array format).
.EXAMPLE
  .\merge-test-logs-index.ps1 -InputPath .mine.json, .theirs.json -OutputPath test-logs\index.json
#>
param(
    [Parameter(Mandatory)]
    [string[]]$InputPath,
    [string]$OutputPath = "",
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"

if (-not $RepoRoot) {
    $RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
}

$modulePath = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
Import-Module $modulePath -Force

$jsonInputs = @()
foreach ($path in $InputPath) {
    if (-not (Test-Path $path)) {
        throw "Input not found: $path"
    }
    $jsonInputs += [System.IO.File]::ReadAllText(
        (Resolve-Path $path), (New-Object System.Text.UTF8Encoding $false))
}

$merged = Merge-HarnessTestLogIndexFromJson -JsonInputs $jsonInputs
if (-not $OutputPath) {
    $OutputPath = Join-Path $RepoRoot "test-logs\index.json"
}

Write-HarnessTestLogIndexFile -Index $merged -Path $OutputPath | Out-Null
Write-Host "Wrote merged index ($($merged.machines.Count) machine(s)) -> $OutputPath"
