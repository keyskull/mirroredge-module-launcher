#Requires -Version 5.1
param([switch]$SkipBuild)
$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force
$ok = 0
$bad = 0
foreach ($name in @("verify-harness", "ui-launcher")) {
    $scriptPath = Join-Path $PSScriptRoot "$name.ps1"
    $p = @{}
    if ($SkipBuild -and $name -eq "ui-launcher") { $p.SkipBuild = $true }
    try {
        & $scriptPath @p
        if ($LASTEXITCODE -ne 0) { throw "exit $LASTEXITCODE" }
        Write-Host "ci-gate: $name OK"
        $ok++
    } catch {
        Write-Host "ci-gate: $name FAILED"
        $bad++
    }
}
if ($bad -gt 0) { Write-Host "ci-gate: FAIL"; exit 1 }
Write-Host "ci-gate: PASS"
exit 0
