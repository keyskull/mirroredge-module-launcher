#Requires -Version 5.1
<#
.SYNOPSIS
  Self-test for debug harness helpers (no game required).
#>
param()

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$session = Initialize-DebugSession -SessionId "harness-selftest"
$logPath = $session.LogPath

"{""sessionId"":""harness-selftest"",""message"":""direct3d_create9""}" | Add-Content $logPath
"{""sessionId"":""harness-selftest"",""message"":""hooks_installed""}" | Add-Content $logPath

$last = Get-LastDebugSession
if ($last.sessionId -ne "harness-selftest") {
    throw "last-session manifest mismatch"
}

$tail = Read-DebugLogTail -Lines 5
if ($tail.Count -lt 2) {
    throw "tail failed"
}

$contains = Test-LogContains -Patterns @("direct3d_create9", "hooks_installed") -LogPath $logPath
if (-not $contains.Pass) {
    throw "contains failed: $($contains.Missing -join ', ')"
}

$sequence = Test-LogSequence -Sequence @("direct3d_create9", "hooks_installed") -LogPath $logPath -TimeoutSec 1
if (-not $sequence.Pass) {
    throw "sequence failed"
}

Test-VisualHarnessPrimitives | Out-Null
Test-GameDialogPatternMatching | Out-Null
Test-HarnessTestLogMerge | Out-Null

Write-Host "verify-harness: PASS"
exit 0
