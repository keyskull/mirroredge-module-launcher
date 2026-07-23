#Requires -Version 5.1
<#
.SYNOPSIS
  Download and set up Ghidra for SDK verification.
  Ghidra is ~425MB; this script downloads it once and caches the result.

.DESCRIPTION
  - Downloads Ghidra 11.3 (PUBLIC) from GitHub releases if not cached
  - Requires JDK 17+ (checks java -version)
  - Extracts to tools/sdk-verify/ghidra/
  - Creates a ghidra project for MirrorsEdge.exe analysis
#>
param(
    [string]$GhidraVersion = "11.3",
    [string]$GhidraAssetDate = "20250205",
    [string]$JavaHome = "",
    [string]$GameExe = ""
)

$ErrorActionPreference = "Stop"
$ToolDir = $PSScriptRoot
# $ToolDir = .../tools/sdk-verify → go up 2 levels to repo root
$Root = Split-Path (Split-Path $ToolDir -Parent) -Parent
$GhidraDir = Join-Path $ToolDir "ghidra"
$GhidraAssetName = "ghidra_${GhidraVersion}_PUBLIC_${GhidraAssetDate}.zip"
$GhidraZip = Join-Path $ToolDir $GhidraAssetName

function Find-Java {
    if ($JavaHome -and (Test-Path (Join-Path $JavaHome "bin\java.exe"))) {
        return Join-Path $JavaHome "bin\java.exe"
    }
    $java = Get-Command java -ErrorAction SilentlyContinue
    if ($java) { return $java.Source }
    foreach ($base in @($env:JAVA_HOME, "C:\Program Files\Java\jdk-17",
                        "C:\Program Files\Java\jdk-21", "C:\Program Files\Eclipse Adoptium")) {
        if ($base) {
            $candidate = Join-Path $base "bin\java.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    return $null
}

function Test-JavaVersion {
    param([string]$JavaExe)
    $output = & $JavaExe -version 2>&1
    $versionStr = "$output"
    if ($versionStr -match 'version "(\d+)') {
        $major = [int]$Matches[1]
        return $major -ge 17
    }
    if ($versionStr -match 'version "1\.(\d+)') {
        $major = [int]$Matches[1]
        return $major -ge 17
    }
    return $false
}

function Find-GameExe {
    param([string]$Root)
    if ($GameExe -and (Test-Path $GameExe)) { return $GameExe }
    $deployConfig = Join-Path $Root "deploy.config.json"
    if (Test-Path $deployConfig) {
        $json = Get-Content $deployConfig -Raw | ConvertFrom-Json
        if ($json.deployPath) {
            $candidate = Join-Path $json.deployPath "Binaries\MirrorsEdge.exe"
            if (Test-Path $candidate) { return $candidate }
            $candidate = Join-Path $json.deployPath "MirrorsEdge.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    $candidate = Join-Path $Root "Binaries\MirrorsEdge.exe"
    if (Test-Path $candidate) { return $candidate }
    $candidate = Join-Path $Root "MirrorsEdge.exe"
    if (Test-Path $candidate) { return $candidate }
    return $null
}

Write-Host "=== Ghidra SDK Verification Setup ===" -ForegroundColor Cyan

$javaExe = Find-Java
if (-not $javaExe) {
    Write-Host "JDK 17+ not found. Install Eclipse Temurin JDK 17:" -ForegroundColor Red
    Write-Host "  winget install EclipseAdoptium.Temurin.17.JDK" -ForegroundColor Yellow
    Write-Host "Or download from: https://adoptium.net/" -ForegroundColor Yellow
    throw "JDK 17+ required"
}

if (-not (Test-JavaVersion $javaExe)) {
    throw "JDK 17+ required (found older version). Install JDK 17 or 21."
}
Write-Host "  Java: $javaExe" -ForegroundColor Green

if (-not (Test-Path $GhidraDir)) {
    if (-not (Test-Path $GhidraZip)) {
        $url = "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_${GhidraVersion}_build/$GhidraAssetName"
        Write-Host "  Downloading Ghidra $GhidraVersion (~425MB)..." -ForegroundColor Yellow
        Write-Host "  URL: $url" -ForegroundColor DarkGray
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $url -OutFile $GhidraZip -UseBasicParsing
        } catch {
            Write-Host "  Download failed: $_" -ForegroundColor Red
            Write-Host "  Manual download: https://github.com/NationalSecurityAgency/ghidra/releases" -ForegroundColor Yellow
            Write-Host "  Place $GhidraAssetName in: $ToolDir" -ForegroundColor Yellow
            throw
        }
    }
    Write-Host "  Extracting Ghidra..." -ForegroundColor Yellow
    Expand-Archive -Path $GhidraZip -DestinationPath $ToolDir -Force
    $extracted = Get-ChildItem $ToolDir -Directory | Where-Object { $_.Name -like "ghidra_*" } | Select-Object -First 1
    if ($extracted) {
        Rename-Item $extracted.FullName "ghidra"
    }
    Write-Host "  Ghidra extracted to: $GhidraDir" -ForegroundColor Green
}

$analyzeHeadless = Join-Path $GhidraDir "support\analyzeHeadless.bat"
if (-not (Test-Path $analyzeHeadless)) {
    throw "analyzeHeadless.bat not found at $analyzeHeadless. Ghidra extraction may be incomplete."
}

$gameExePath = Find-GameExe -Root $Root
if (-not $gameExePath) {
    Write-Host "MirrorsEdge.exe not found. Run from game directory or set deploy.config.json." -ForegroundColor Yellow
    Write-Host "Path resolution:" -ForegroundColor DarkGray
    Write-Host "  1. --gameExe parameter" -ForegroundColor DarkGray
    Write-Host "  2. deploy.config.json deployPath" -ForegroundColor DarkGray
    Write-Host "  3. <repo>/Binaries/MirrorsEdge.exe" -ForegroundColor DarkGray
    Write-Host "  4. <repo>/MirrorsEdge.exe" -ForegroundColor DarkGray
} else {
    Write-Host "  Game binary: $gameExePath" -ForegroundColor Green
    $fileInfo = Get-Item $gameExePath
    $sizeMB = [math]::Round($fileInfo.Length / 1MB, 1)
    Write-Host "  Size: ${sizeMB}MB" -ForegroundColor DarkGray
}

$env:JAVA_HOME = Split-Path (Split-Path $javaExe -Parent) -Parent

Write-Host ""
Write-Host "=== Setup complete ===" -ForegroundColor Green
Write-Host "  Ghidra: $GhidraDir" -ForegroundColor DarkGray
Write-Host "  Next: .\tools\sdk-verify\extract-sdk-data.ps1   (run Ghidra analysis)" -ForegroundColor DarkGray
Write-Host "  Or:   .\tools\sdk-verify\verify-sdk.ps1           (verify after extraction)" -ForegroundColor DarkGray
