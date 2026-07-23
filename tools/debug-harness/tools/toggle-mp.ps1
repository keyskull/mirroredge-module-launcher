#Requires -Version 5.1
<#
  Toggle core.dll between enabled and disabled under modules\core\.

  When DISABLED: the game launches without core (Module Manager only).
  When ENABLED: harness tests can load core via control pipe or Modules tab.

  Usage:
    .\toggle-mp.ps1            # toggle between enabled/disabled
    .\toggle-mp.ps1 -Enable    # force enable (for harness)
    .\toggle-mp.ps1 -Disable   # force disable (for safe play)
#>
param(
    [switch]$Enable,
    [switch]$Disable
)

$ErrorActionPreference = "Stop"

function Resolve-DeployRoot {
    $repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
    $deployRoot = $null

    $configPath = Join-Path $repoRoot "deploy.config.json"
    if (Test-Path $configPath) {
        $json = Get-Content $configPath -Raw | ConvertFrom-Json
        if ($json.deployPath) {
            $deployRoot = [string]$json.deployPath
        } elseif ($json.gameBinaries) {
            $deployRoot = [string]$json.gameBinaries
        }
    }

    if ($env:ME_DEPLOY_PATH) {
        $deployRoot = $env:ME_DEPLOY_PATH
    } elseif ($env:ME_GAME_BINARIES) {
        $deployRoot = $env:ME_GAME_BINARIES
    }

    if (-not $deployRoot) {
        $deployRoot = Split-Path $repoRoot -Parent
    }

    return $deployRoot
}

$coreDir = Join-Path (Resolve-DeployRoot) "modules\core"
$dllPath = Join-Path $coreDir "core.dll"
$disabledPath = Join-Path $coreDir "core.dll.disabled"

if ($Enable) {
    if (Test-Path $disabledPath) {
        Rename-Item $disabledPath "core.dll"
        Write-Host "core.dll ENABLED (ready for harness tests)"
    } else {
        Write-Host "core.dll already enabled"
    }
    exit 0
}

if ($Disable) {
    if (Test-Path $dllPath) {
        Rename-Item $dllPath "core.dll.disabled"
        Write-Host "core.dll DISABLED (safe to play)"
    } else {
        Write-Host "core.dll already disabled"
    }
    exit 0
}

# Toggle
if (Test-Path $disabledPath) {
    Rename-Item $disabledPath "core.dll"
    Write-Host "core.dll -> ENABLED"
} elseif (Test-Path $dllPath) {
    Rename-Item $dllPath "core.dll.disabled"
    Write-Host "core.dll -> DISABLED"
} else {
    Write-Host "Neither file found under $coreDir - core not deployed?"
}
