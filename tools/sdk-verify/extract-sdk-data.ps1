#Requires -Version 5.1
<#
.SYNOPSIS
  Run Ghidra headless analysis on MirrorsEdge.exe and export SDK reference data.

.DESCRIPTION
  Creates a Ghidra project, imports the binary, runs auto-analysis, then executes
  the Python export script to produce tools/sdk-verify/reference/sdk-reference.json.
  This takes 5-15 minutes depending on CPU. Run once per game binary update.

.PARAMETER GameExe
  Path to MirrorsEdge.exe (default: auto-detect from deploy.config.json or workspace).

.PARAMETER TimeoutMinutes
  Max analysis time in minutes before killing (default: 30).
#>
param(
    [string]$GameExe = "",
    [int]$TimeoutMinutes = 30
)

$ErrorActionPreference = "Stop"
$ToolDir = $PSScriptRoot
# $ToolDir = .../tools/sdk-verify → go up 2 levels to repo root
$Root = Split-Path (Split-Path $ToolDir -Parent) -Parent
$GhidraDir = Join-Path $ToolDir "ghidra"
$GhidraProjectDir = Join-Path $ToolDir "ghidra_project"
$ExportScript = Join-Path $ToolDir "ghidra-export.py"
$ReferenceDir = Join-Path $ToolDir "reference"
$ReferenceJson = Join-Path $ReferenceDir "sdk-reference.json"

# Find game executable
if (-not $GameExe) {
    $deployConfig = Join-Path $Root "deploy.config.json"
    if (Test-Path $deployConfig) {
        $json = Get-Content $deployConfig -Raw | ConvertFrom-Json
        if ($json.deployPath) {
            $candidate = Join-Path $json.deployPath "Binaries\MirrorsEdge.exe"
            if (Test-Path $candidate) { $GameExe = $candidate }
            if (-not $GameExe) {
                $candidate = Join-Path $json.deployPath "MirrorsEdge.exe"
                if (Test-Path $candidate) { $GameExe = $candidate }
            }
        }
    }
    if (-not $GameExe) {
        $candidate = Join-Path $Root "Binaries\MirrorsEdge.exe"
        if (Test-Path $candidate) { $GameExe = $candidate }
    }
    if (-not $GameExe) {
        $candidate = Join-Path $Root "MirrorsEdge.exe"
        if (Test-Path $candidate) { $GameExe = $candidate }
    }
}

if (-not $GameExe -or -not (Test-Path $GameExe)) {
    throw "MirrorsEdge.exe not found. Specify -GameExe or configure deploy.config.json."
}

Write-Host "=== Ghidra SDK Data Extraction ===" -ForegroundColor Cyan
Write-Host "  Binary: $GameExe" -ForegroundColor DarkGray

$analyzeHeadless = Join-Path $GhidraDir "support\analyzeHeadless.bat"
if (-not (Test-Path $analyzeHeadless)) {
    throw "Ghidra not found. Run .\tools\sdk-verify\setup-ghidra.ps1 first."
}

if (-not (Test-Path $ExportScript)) {
    throw "Export script not found: $ExportScript"
}

# Ensure reference directory exists
New-Item -ItemType Directory -Force -Path $ReferenceDir | Out-Null

# Clean previous project if exists
if (Test-Path $GhidraProjectDir) {
    Write-Host "  Removing previous project..." -ForegroundColor DarkGray
    Remove-Item $GhidraProjectDir -Recurse -Force -ErrorAction SilentlyContinue
}

$projectName = "MirrorsEdge_SDK"
$binaryName = [System.IO.Path]::GetFileName($GameExe)

Write-Host "  Creating Ghidra project..." -ForegroundColor Yellow
Write-Host "  Running auto-analysis (this takes 5-15 minutes)..." -ForegroundColor Yellow
Write-Host "  Timeout: ${TimeoutMinutes} minutes" -ForegroundColor DarkGray

# Build Ghidra headless command
# -import: import binary
# -scriptPath: where our Python script lives
# -postScript: run our export script after auto-analysis
# -analysisTimeoutPerFile: seconds per analysis file
$analysisTimeoutSec = $TimeoutMinutes * 60
$args = @(
    $GhidraProjectDir,
    $projectName,
    "-import", $GameExe,
    "-scriptPath", $ToolDir,
    "-postScript", "ghidra-export.py", $ReferenceJson,
    "-analysisTimeoutPerFile", $analysisTimeoutSec,
    "-max-cpu", "0",  # use all CPUs
    "-noanalysis"      # we'll control analysis in the script
) -replace '-noanalysis', ''

# Actually we want auto-analysis, so let's not use -noanalysis
$ghidraArgs = @(
    $GhidraProjectDir,
    $projectName,
    "-import", $GameExe,
    "-scriptPath", $ToolDir,
    "-postScript", "ghidra-export.py", $ReferenceJson,
    "-analysisTimeoutPerFile", "$analysisTimeoutSec",
    "-max-cpu", "0"
)

Write-Host "  Command: $analyzeHeadless $($ghidraArgs -join ' ')" -ForegroundColor DarkGray

$proc = Start-Process -FilePath $analyzeHeadless `
    -ArgumentList $ghidraArgs `
    -NoNewWindow -Wait -PassThru

if ($proc.ExitCode -ne 0) {
    Write-Host "  Ghidra exited with code $($proc.ExitCode)" -ForegroundColor Red
    throw "Ghidra analysis failed"
}

if (-not (Test-Path $ReferenceJson)) {
    throw "Export script did not produce $ReferenceJson"
}

$refSize = (Get-Item $ReferenceJson).Length
Write-Host ""
Write-Host "=== Extraction complete ===" -ForegroundColor Green
Write-Host "  Reference data: $ReferenceJson ($([math]::Round($refSize/1KB, 1))KB)" -ForegroundColor Green
Write-Host "  Next: .\tools\sdk-verify\verify-sdk.ps1   (validate against current headers)" -ForegroundColor DarkGray
Write-Host "  Next: .\tools\sdk-verify\generate-static-asserts.ps1   (generate static_assert code)" -ForegroundColor DarkGray
