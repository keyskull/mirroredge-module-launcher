#Requires -Version 5.1
<#
  Full mod functionality test:
    launcher UI, module_manager pipes/UI, core inject + pipes, viewport alignment.
#>
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [switch]$LauncherOnly
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot

if ($SkipLaunch) {
    if (-not $LauncherOnly) {
        throw "mod-full: requires live game session (no -SkipLaunch)"
    }
    if (-not $SkipBuild) {
        Invoke-DebugBuild -RepoRoot $repo -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1")
    }
    Test-LauncherStatusDialogUi -RepoRoot $repo | Out-Null
    Write-Host "mod-full: launcher-only PASS"
    exit 0
}

Test-LauncherStatusDialogUi -RepoRoot $repo | Out-Null
Write-Host "mod-full: launcher UI OK"

if ($LauncherOnly) {
    Write-Host "mod-full: PASS (launcher only)"
    exit 0
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting

Test-ModFullSuite -Context $ctx | Out-Null

Write-Host "mod-full: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
