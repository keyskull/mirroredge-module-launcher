#Requires -Version 5.1
<#
  Win32 launcher dialog UI test (no game required).
#>
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot

if (-not $SkipBuild) {
    Invoke-DebugBuild -RepoRoot $repo -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1")
}

Test-LauncherStatusDialogUi -RepoRoot $repo | Out-Null
Write-Host "ui-launcher: PASS"
exit 0
