#Requires -Version 5.1
<#
.SYNOPSIS
  Compute FNV-1a code probe hash from MirrorsEdge.exe and optionally update
  game_signature.h with known values.

.DESCRIPTION
  Reads 4096 bytes at file offset 0x1000 from MirrorsEdge.exe, computes the
  FNV-1a hash, and inserts it into shared/me_sdk/runtime/game_signature.h
  so the runtime code probe gate actually rejects unknown binaries.

  Run this whenever the game binary is updated to a new build.

.PARAMETER GameExe
  Path to MirrorsEdge.exe.

.PARAMETER NoUpdate
  Compute and display the hash but do not update game_signature.h.
#>
param(
    [string]$GameExe = "",
    [switch]$NoUpdate
)

$ErrorActionPreference = "Stop"
$ToolDir = $PSScriptRoot
# $ToolDir = .../tools/sdk-verify → go up 2 levels to repo root
$Root = Split-Path (Split-Path $ToolDir -Parent) -Parent
$SignatureHeader = Join-Path $Root "shared\me_sdk\runtime\game_signature.h"

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

function Compute-Fnv1a32 {
    param([byte[]]$Bytes)
    return [FnvHash]::Compute($Bytes)
}

# Embedded C# FNV-1a implementation — PowerShell integer arithmetic is lossy with uint32 wrapping
Add-Type -TypeDefinition @'
using System;
public static class FnvHash {
    public static uint Compute(byte[] data) {
        uint hash = 2166136261;
        foreach (byte b in data) {
            hash ^= b;
            hash *= 16777619;
        }
        return hash;
    }
}
'@ -ErrorAction SilentlyContinue

function Get-PeImageSize {
    param([string]$ExePath)
    $fs = [System.IO.File]::OpenRead($ExePath)
    $reader = [System.IO.BinaryReader]::new($fs)
    $fs.Seek(0x3C, [System.IO.SeekOrigin]::Begin) | Out-Null
    $peOffsetBytes = $reader.ReadBytes(4)
    $peOffset = [BitConverter]::ToInt32($peOffsetBytes, 0)
    $fs.Seek($peOffset + 0x50, [System.IO.SeekOrigin]::Begin) | Out-Null
    $sizeBytes = $reader.ReadBytes(4)
    $imageSize = [BitConverter]::ToUInt32($sizeBytes, 0)
    $reader.Close()
    $fs.Close()
    return $imageSize
}

$gameExePath = Find-GameExe -Root $Root
if (-not $gameExePath -or -not (Test-Path $gameExePath)) {
    throw "MirrorsEdge.exe not found. Specify -GameExe or configure deploy.config.json."
}

Write-Host "=== Compute Code Probe FNV ===" -ForegroundColor Cyan
Write-Host "  Binary: $gameExePath" -ForegroundColor DarkGray

# Read 4096 bytes at offset 0x1000
$fs = [System.IO.File]::OpenRead($gameExePath)
$reader = [System.IO.BinaryReader]::new($fs)
$fs.Seek(0x1000, [System.IO.SeekOrigin]::Begin) | Out-Null
$probeData = $reader.ReadBytes(4096)
$reader.Close()
$fs.Close()

if ($probeData.Length -ne 4096) {
    throw "Failed to read 4096 bytes at offset 0x1000 (read $($probeData.Length) bytes)"
}

$fnv = Compute-Fnv1a32 $probeData
$imageSize = Get-PeImageSize $gameExePath

Write-Host "  Image size: 0x$($imageSize.ToString('X')) ($([math]::Round($imageSize/1MB, 1))MB)" -ForegroundColor Green
Write-Host "  FNV-1a hash: 0x$($fnv.ToString('X8'))" -ForegroundColor Green

if ($NoUpdate) {
    Write-Host ""
    Write-Host "  Use -NoUpdate to display only. Remove to update game_signature.h." -ForegroundColor DarkGray
    exit 0
}

if (-not (Test-Path $SignatureHeader)) {
    throw "Signature header not found: $SignatureHeader"
}

$currentContent = Get-Content $SignatureHeader -Raw
$utf8 = [System.Text.UTF8Encoding]::new($false)

# Check if FNV is already in the list
$expectedFnv = "0x$($fnv.ToString('X8'))"
if ($currentContent -match [regex]::Escape($expectedFnv)) {
    Write-Host "  FNV 0x$($fnv.ToString('X8')) already in game_signature.h" -ForegroundColor Yellow
    exit 0
}

# Update the known FNV array and count
# Pattern: replace the storage array with our known values
$oldStorage = 'static const uint32_t kKnownCodeProbeFnvStorage[] = {0u};'
$newStorage = "static const uint32_t kKnownCodeProbeFnvStorage[] = {${expectedFnv}u};"

if ($currentContent.Contains($oldStorage)) {
    $updated = $currentContent.Replace($oldStorage, $newStorage)
} else {
    Write-Warning "Could not find kKnownCodeProbeFnvStorage array pattern in game_signature.h"
    Write-Host "  Manual update required. Add $expectedFnv to kKnownCodeProbeFnvStorage." -ForegroundColor Yellow
    exit 1
}

# Update count from 0 to 1
$oldCount = 'static const size_t kKnownCodeProbeFnvCount = 0;'
$newCount = 'static const size_t kKnownCodeProbeFnvCount = 1;'
if ($updated.Contains($oldCount)) {
    $updated = $updated.Replace($oldCount, $newCount)
}

[System.IO.File]::WriteAllText($SignatureHeader, $updated, $utf8)
Write-Host ""
Write-Host "  Updated game_signature.h:" -ForegroundColor Green
Write-Host "    kKnownCodeProbeFnvStorage[] = {$expectedFnv}u" -ForegroundColor Green
Write-Host "    kKnownCodeProbeFnvCount = 1" -ForegroundColor Green
Write-Host ""
Write-Host "  NOTE: Rebuild all modules after this change." -ForegroundColor Yellow
Write-Host "  The runtime will now reject game binaries with different FNV hashes." -ForegroundColor Yellow
