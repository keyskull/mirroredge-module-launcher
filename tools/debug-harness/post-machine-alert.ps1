#Requires -Version 5.1
<#
.SYNOPSIS
  Publish a cross-machine alert to test-logs/alerts/ (git) for peer test machines.

.EXAMPLE
  .\post-machine-alert.ps1 -Title "core boot hang on 2号机" -Body "..." -ToMachine "1号机" -Severity blocker -Push
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Title,
    [Parameter(Mandatory)]
    [string]$Body,
    [ValidateSet("info", "warning", "blocker", "resolved")]
    [string]$Severity = "warning",
    [string[]]$RelatedCommits = @(),
    [string[]]$RelatedFiles = @(),
    [string]$ToMachine = "",
    [switch]$Push
)

$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Import-Module (Join-Path $PSScriptRoot "lib\DebugHarness.psm1") -Force

Publish-MachineAlert -Title $Title -Body $Body -Severity $Severity `
    -RelatedCommits $RelatedCommits -RelatedFiles $RelatedFiles `
    -ToMachine $ToMachine -RepoRoot $root -Push:$Push
