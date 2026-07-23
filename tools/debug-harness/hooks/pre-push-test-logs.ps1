#Requires -Version 5.1
param(
    [string]$RepoRoot = "",
    [string]$Stdin = ""
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force -WarningAction SilentlyContinue

if (-not $RepoRoot) {
    $RepoRoot = Get-RepoRoot
}

if (-not (Invoke-HarnessPrePushTestLogsCheck -RepoRoot $RepoRoot -Stdin $Stdin)) {
    exit 1
}

exit 0
