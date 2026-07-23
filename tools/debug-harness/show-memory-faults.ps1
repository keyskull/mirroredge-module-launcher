#Requires -Version 5.1
<#
.SYNOPSIS
  Show SEH-caught memory access faults recorded in-game (GET_STATUS memoryFaults).
#>
[CmdletBinding()]
param(
    [int]$TimeoutMs = 5000,
    [switch]$Json
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "lib\DebugHarness.psm1") -Force
Show-MemoryFaultList -TimeoutMs $TimeoutMs -Json:$Json
