#Requires -Version 5.1
<#
.SYNOPSIS
  One-shot onboarding for a new harness test machine (2号机, etc.).

.DESCRIPTION
  Verifies deploy.config.json + testMachine, runs setup.ps1 (MCP + merge driver + hooks),
  syncs test-logs from remote, and validates index consistency.

  Full checklist: docs/test-environments.md

.EXAMPLE
  git pull
  Copy deploy.config.json.example → deploy.config.json  # set deployPath + testMachine
  .\tools\debug-harness\setup-test-machine.ps1
#>
param(
    [string]$RepoRoot = "",
    [switch]$SkipPull
)

$ErrorActionPreference = "Stop"

function Get-TestMachineRegistryIds {
    param([string]$Root)
    $path = Join-Path $Root "test-environments.json"
    if (-not (Test-Path $path)) { return @() }
    try {
        $registry = Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json
        return @($registry.machines | ForEach-Object { [string]$_.id })
    } catch {
        return @()
    }
}

function Test-LocalGitMergeDriverConfigured {
    param([string]$Root)
    Push-Location $Root
    try {
        return [bool](git config --get merge.test-logs-index.driver 2>$null)
    } finally {
        Pop-Location
    }
}

function Test-LocalGitHooksConfigured {
    param([string]$Root)
    Push-Location $Root
    try {
        return ((git config --get core.hooksPath 2>$null) -eq '.githooks')
    } finally {
        Pop-Location
    }
}

$lib = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
Import-Module $lib -Force -WarningAction SilentlyContinue

if (-not $RepoRoot) {
    $RepoRoot = Get-RepoRoot
}

Write-Host "=== Harness test machine setup ===" -ForegroundColor Cyan
Write-Host "Repo: $RepoRoot"

$deployPath = Join-Path $RepoRoot "deploy.config.json"
if (-not (Test-Path $deployPath)) {
    throw "Missing deploy.config.json. Copy deploy.config.json.example, set deployPath and testMachine (see test-environments.json)."
}

$cfg = Get-Content $deployPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $cfg.testMachine) {
    throw "deploy.config.json missing testMachine. Use an id from test-environments.json (e.g. 2号机)."
}

$machineId = [string]$cfg.testMachine
$registryIds = Get-TestMachineRegistryIds -Root $RepoRoot
if ($registryIds.Count -gt 0 -and ($registryIds -notcontains $machineId)) {
    Write-Warning "testMachine '$machineId' is not listed in test-environments.json (known: $($registryIds -join ', '))."
}

if (-not $SkipPull) {
    Write-Host "git pull..."
    Push-Location $RepoRoot
    try {
        git pull 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) {
            throw "git pull failed (exit $LASTEXITCODE)"
        }
    } finally {
        Pop-Location
    }
}

Write-Host "Running setup.ps1 (MCP, merge driver, git hooks)..."
& (Join-Path $PSScriptRoot "setup.ps1") -RepoRoot $RepoRoot

if (-not (Test-LocalGitMergeDriverConfigured -Root $RepoRoot)) {
    throw "Git merge driver test-logs-index is not configured. Re-run setup.ps1 or check git config."
}

if (-not (Test-LocalGitHooksConfigured -Root $RepoRoot)) {
    Write-Warning "core.hooksPath is not .githooks; pre-push validation may be inactive."
}

Import-Module $lib -Force -WarningAction SilentlyContinue

Write-Host "Syncing test-logs from remote..."
Initialize-HarnessTestLogGit -RepoRoot $RepoRoot | Out-Null

Write-Host "Validating test-logs..."
& (Join-Path $PSScriptRoot "validate-test-logs.ps1") -RepoRoot $RepoRoot

Write-Host ""
& (Join-Path $PSScriptRoot "show-test-logs-status.ps1") -RepoRoot $RepoRoot

Write-Host ""
Write-Host "Done. testMachine=$machineId" -ForegroundColor Green
Write-Host "Next: reload Cursor, then .\build.ps1  and  .\tools\debug-harness\run-all-scenarios.ps1"
Write-Host "Docs: docs/test-environments.md  test-logs/README.md"
