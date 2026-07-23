#Requires -Version 5.1
<#
.SYNOPSIS
  Show cross-machine alerts from test-logs/alerts/ (see test-logs/README.md).
#>
[CmdletBinding()]
param(
    [string]$ForMachine = "",
    [int]$Limit = 20,
    [switch]$Json,
    [switch]$UnreadOnly,
    [switch]$MarkRead,
    [switch]$All
)

$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Import-Module (Join-Path $PSScriptRoot "lib\DebugHarness.psm1") -Force

$machine = if ($ForMachine) { $ForMachine } else { Resolve-TestMachine -RepoRoot $root }
$alerts = if ($All) {
    Get-MachineAlerts -IncludeAll -Limit $Limit -RepoRoot $root
} else {
    Get-MachineAlerts -ForMachine $machine -Limit $Limit -RepoRoot $root -UnreadOnly:$UnreadOnly
}

if ($Json) {
    $alerts | ConvertTo-Json -Depth 8
    if ($MarkRead -and $machine) {
        Clear-MachineAlertsUnread -Machine $machine -RepoRoot $root
    }
    return
}

Write-Host "=== Machine alerts (for $machine) ===" -ForegroundColor Cyan
if (-not @($alerts).Count) {
    Write-Host "(none)"
    if ($MarkRead -and $machine) {
        Clear-MachineAlertsUnread -Machine $machine -RepoRoot $root
    }
    return
}

foreach ($a in $alerts) {
    $to = if ($a.toMachine) { " -> $($a.toMachine)" } else { "" }
    Write-Host ""
    Write-Host "[$($a.severity)] $($a.title)" -ForegroundColor Yellow
    Write-Host "  from: $($a.fromMachine)$to  at $($a.createdAt)  commit: $($a.git.shortCommit)"
    Write-Host "  $($a.body)"
}

if ($MarkRead -and $machine) {
    Clear-MachineAlertsUnread -Machine $machine -RepoRoot $root
}
