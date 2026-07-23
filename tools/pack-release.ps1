# Pack dist/ into a GitHub Release zip for ModuleLauncher auto-update.
# Asset name: mirroredge-module-launcher-<semver>-win32.zip
#
# Ships only runtime files (no MSVC .pdb/.exp/.lib, no bat/alias).
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

function Remove-NonRuntimeArtifacts {
    param([Parameter(Mandatory = $true)][string]$Root)

    if (-not (Test-Path $Root)) { return }

    $patterns = @(
        "*.exp", "*.lib", "*.pdb", "*.ilk", "*.obj", "*.iobj", "*.ipdb",
        "*.map", "*.idb", "*.tlog", "*.lastbuildstate", "*.recipe",
        "*.log", "*.vgdbsettings"
    )
    Get-ChildItem -Path $Root -Recurse -File -Include $patterns -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    foreach ($stale in @(
            "mirroredge-module-launcher.exe",
            "ModuleLauncher.bat",
            "ModuleLauncher.pdb",
            "d3d9.pdb",
            "d3d9.exp",
            "d3d9.lib"
        )) {
        $p = Join-Path $Root $stale
        if (Test-Path $p) {
            Remove-Item $p -Force -ErrorAction SilentlyContinue
        }
    }
}

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

# PhysX pack: only when it contains runtime DLLs (skip README-only tree).
$physxSrc = Join-Path $RepoRoot "physx"
if (-not (Test-Path $physxSrc)) {
    $physxSrc = Join-Path $DistPath "physx"
}
if (Test-Path $physxSrc) {
    $physxDlls = @(Get-ChildItem $physxSrc -Recurse -Filter *.dll -File -ErrorAction SilentlyContinue)
    if ($physxDlls.Count -gt 0) {
        Copy-Item $physxSrc (Join-Path $staging "physx") -Recurse -Force
    }
}

Remove-NonRuntimeArtifacts -Root $staging

$zipName = "mirroredge-module-launcher-$Version-win32.zip"
$zipPath = Join-Path $OutDir $zipName
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zipPath -Force
Remove-Item $staging -Recurse -Force

Write-Host "Packed: $zipPath" -ForegroundColor Green
Write-Host "Upload as GitHub Release asset (tag v$Version)." -ForegroundColor DarkGray
