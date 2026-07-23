#Requires -Version 5.1
<#
.SYNOPSIS
  MCP stdio entrypoint — resolves node.exe when it is not on PATH (common for GUI apps).
#>
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

$node = Find-NodeExe
if (-not $node) {
    [Console]::Error.WriteLine(
        "mirroredge-debug MCP: Node.js not found. Install from https://nodejs.org/ then run tools\debug-harness\setup.ps1"
    )
    exit 1
}

$indexJs = Join-Path $PSScriptRoot "index.js"
& $node $indexJs
exit $LASTEXITCODE
