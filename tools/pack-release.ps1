# Pack dist/ into a GitHub Release zip for ModuleLauncher auto-update.
# Asset name: mirroredge-module-launcher-<semver>-win32.zip
#
# Usage (after build):
#   .\tools\pack-release.ps1
#   .\tools\pack-release.ps1 -DistPath .\dist -OutDir .\artifacts

[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$DistPath = "",
    [string]$OutDir = "",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

if (-not $DistPath) {
    $DistPath = Join-Path $RepoRoot "dist"
}
if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot "artifacts"
}

$versionJson = Join-Path $RepoRoot "version.json"
if (-not $Version) {
    if (-not (Test-Path $versionJson)) {
        throw "version.json not found: $versionJson"
    }
    $vj = Get-Content $versionJson -Raw | ConvertFrom-Json
    $Version = [string]$vj.version
}
if (-not $Version) {
    throw "Version is empty"
}

$exe = Join-Path $DistPath "ModuleLauncher.exe"
if (-not (Test-Path $exe)) {
    throw "Missing ModuleLauncher.exe under $DistPath - run .\build.ps1 -NoDeploy first"
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$staging = Join-Path $OutDir ("pack-staging-" + $Version)
if (Test-Path $staging) {
    Remove-Item $staging -Recurse -Force
}
New-Item -ItemType Directory -Path $staging -Force | Out-Null

$copyNames = @(
    "ModuleLauncher.exe",
    "mirroredge-module-launcher.exe",
    "VERSION.json",
    "CHANGELOG.md",
    "settings.json"
)
foreach ($name in $copyNames) {
    $src = Join-Path $DistPath $name
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $staging $name) -Force
    }
}

$modulesSrc = Join-Path $DistPath "modules"
if (Test-Path $modulesSrc) {
    Copy-Item $modulesSrc (Join-Path $staging "modules") -Recurse -Force
}

$d3d9 = Join-Path $DistPath "d3d9.dll"
if (Test-Path $d3d9) {
    New-Item -ItemType Directory -Path (Join-Path $staging "dist") -Force | Out-Null
    Copy-Item $d3d9 (Join-Path $staging "dist\d3d9.dll") -Force
    Copy-Item $d3d9 (Join-Path $staging "d3d9.dll") -Force
}

$bat = "@echo off`r`nstart `"`" `"%~dp0ModuleLauncher.exe`" %*`r`n"
[System.IO.File]::WriteAllText((Join-Path $staging "ModuleLauncher.bat"), $bat)

$zipName = "mirroredge-module-launcher-$Version-win32.zip"
$zipPath = Join-Path $OutDir $zipName
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zipPath -Force
Remove-Item $staging -Recurse -Force

Write-Host "Packed: $zipPath" -ForegroundColor Green
Write-Host "Upload as GitHub Release asset (tag v$Version)." -ForegroundColor DarkGray
