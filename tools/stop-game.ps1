#Requires -Version 5.1
<#
.SYNOPSIS
  Resume suspended MirrorsEdge threads and force-stop the game (and optional launcher).
.EXAMPLE
  .\tools\stop-game.ps1
  .\tools\stop-game.ps1 -IncludeLauncher
  .\tools\stop-game.ps1 -Elevate
#>
param(
    [switch]$IncludeLauncher,
    [switch]$Elevate
)

if ($Elevate) {
    $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $PSCommandPath)
    if ($IncludeLauncher) { $argList += "-IncludeLauncher" }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList -Wait
    exit $LASTEXITCODE
}

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "lib\Resume-ProcessThreads.ps1"
if (-not (Test-Path $lib)) {
    throw "Missing $lib"
}
. $lib
Stop-MirrorsEdgeGameProcesses -IncludeLauncher:$IncludeLauncher
$zombieIds = @(Get-MirrorsEdgeZombieProcesses | ForEach-Object { $_.Id })
if ($zombieIds.Count -gt 0) {
    Write-Warning "stop-game: zombie EPROCESS remains (PID $($zombieIds -join ', ')). Reboot Windows to clear it."
    exit 2
}
Write-Host "stop-game: done"
