#Requires -Version 5.1
<#
  KI-2026-003 regression: menu open -> Alt+Tab to Notepad -> type in external app -> return.
  Default sample is ASCII "KI3" (keyboard dispatch); pass -SampleText for Unicode probes.
#>
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild,
    [string]$SampleText = "KI3"
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot
if ($SkipLaunch) {
    throw "ime-roundtrip: requires live game session"
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "ime-roundtrip"

Test-HarnessImeRoundtrip -KeepFocused -SampleText $SampleText | Out-Null

Write-Host "ime-roundtrip: PASS"
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
