#Requires -Version 5.1
<#
.SYNOPSIS
  One-time setup: npm install for MCP server + merge mirroredge-debug into .cursor/mcp.json
#>
param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"

function Find-NodeExe {
    $cmd = Get-Command node -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source) {
        return $cmd.Source
    }

    $candidates = @(
        "$env:ProgramFiles\nodejs\node.exe",
        "${env:ProgramFiles(x86)}\nodejs\node.exe",
        "$env:LOCALAPPDATA\Programs\node\node.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) {
            return $path
        }
    }
    return $null
}

if (-not $RepoRoot) {
    $RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
}

$mcpDir = Join-Path $RepoRoot "tools\mcp-debug-server"
if (-not (Test-Path $mcpDir)) {
    throw "MCP server not found: $mcpDir"
}

$node = Find-NodeExe
if (-not $node) {
    Write-Warning "Node.js not found. Install Node.js before using mirroredge-debug MCP tools."
} else {
    Write-Host "Node.js: $node"
    $npm = Join-Path (Split-Path $node -Parent) "npm.cmd"
    Push-Location $mcpDir
    try {
        if (Test-Path $npm) {
            & $npm install
        } else {
            npm install
        }
    } finally {
        Pop-Location
    }
}

$cursorDir = Join-Path $RepoRoot ".cursor"
if (-not (Test-Path $cursorDir)) {
    New-Item -ItemType Directory -Path $cursorDir -Force | Out-Null
}

$mcpJsonPath = Join-Path $cursorDir "mcp.json"
$existingServers = @{}
if (Test-Path $mcpJsonPath) {
    try {
        $parsed = Get-Content $mcpJsonPath -Raw | ConvertFrom-Json
        if ($parsed.mcpServers) {
            $parsed.mcpServers.PSObject.Properties | ForEach-Object {
                $existingServers[$_.Name] = $_.Value
            }
        }
    } catch {
        Write-Warning "Could not parse existing mcp.json; overwriting mirroredge-debug entry only."
    }
}

$runPs1 = (Join-Path $RepoRoot "tools\mcp-debug-server\run.ps1") -replace '\\', '/'
$existingServers["mirroredge-debug"] = @{
    command = "powershell.exe"
    args    = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $runPs1)
    env     = @{
        MMOD_REPO_ROOT = $RepoRoot
    }
}

@{ mcpServers = $existingServers } | ConvertTo-Json -Depth 8 | Set-Content -Path $mcpJsonPath -Encoding UTF8
Write-Host "Wrote $mcpJsonPath (merged mirroredge-debug; other servers preserved)"

$lib = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
if (Test-Path $lib) {
    Import-Module $lib -Force -WarningAction SilentlyContinue
    Install-HarnessGitMergeDriver -RepoRoot $RepoRoot | Out-Null
    Install-HarnessGitHooks -RepoRoot $RepoRoot | Out-Null
}

Write-Host "Reload Cursor to enable mirroredge-debug MCP tools."
