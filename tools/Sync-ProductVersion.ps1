#Requires -Version 5.1
<#
.SYNOPSIS
  Sync shared/product_version.h from version.json (called by build.ps1).
#>
param(
    [string]$RepoRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = "Stop"

$versionFile = Join-Path $RepoRoot "version.json"
$headerPath = Join-Path $RepoRoot "shared\product_version.h"

if (-not (Test-Path $versionFile)) {
    throw "Missing version.json at $versionFile"
}

$meta = Get-Content $versionFile -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $meta.version) {
    throw "version.json must contain a 'version' field"
}

$parts = $meta.version -split '\.'
if ($parts.Count -lt 3) {
    throw "version must be semver major.minor.patch (got $($meta.version))"
}

$major = [int]$parts[0]
$minor = [int]$parts[1]
$patch = [int]$parts[2]
$product = if ($meta.product) { $meta.product } else { "mirroredge-module-launcher" }
$releaseDate = if ($meta.releaseDate) { $meta.releaseDate } else { "" }
$channel = if ($meta.channel) { $meta.channel } else { "stable" }

$header = @"
#pragma once

// Product (launcher release) version - generated from version.json by build.ps1.
// Do not edit by hand; run .\build.ps1 or tools\Sync-ProductVersion.ps1.

#define MMOD_PRODUCT_NAME "$product"
#define MMOD_PRODUCT_VERSION_MAJOR $major
#define MMOD_PRODUCT_VERSION_MINOR $minor
#define MMOD_PRODUCT_VERSION_PATCH $patch
#define MMOD_PRODUCT_VERSION_STRING "$($meta.version)"
#define MMOD_PRODUCT_RELEASE_DATE "$releaseDate"
#define MMOD_PRODUCT_CHANNEL "$channel"

"@

$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($headerPath, $header, $utf8NoBom)

Write-Host "==> product version $($meta.version) -> shared/product_version.h"

return [pscustomobject]@{
    Version     = $meta.version
    ReleaseDate = $releaseDate
    HeaderPath  = $headerPath
}
