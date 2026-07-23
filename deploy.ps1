#Requires -Version 5.1
# Build and deploy to the configured game Binaries folder (same as build.ps1 default).
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "build.ps1") @args
