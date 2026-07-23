#Requires -Version 5.1
<#
.SYNOPSIS
  Git merge driver for test-logs/index.json and test-logs/CHANGELOG.md.
#>
param(
    [string]$Ancestor = "",
    [string]$Current = "",
    [string]$Other = ""
)

if ($args.Count -ge 3) {
    $Ancestor = $args[0]
    $Current = $args[1]
    $Other = $args[2]
}

if (-not $Current) {
    Write-Error "git merge driver: missing %A (current) path"
    exit 1
}

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$modulePath = Join-Path $RepoRoot "tools\debug-harness\lib\DebugHarness.psm1"
Import-Module $modulePath -Force -WarningAction SilentlyContinue

$utf8NoBom = New-Object System.Text.UTF8Encoding $false
$inputs = @()
foreach ($path in @($Ancestor, $Current, $Other)) {
    if ($path -and (Test-Path $path)) {
        $inputs += [System.IO.File]::ReadAllText($path, $utf8NoBom)
    }
}

$fileName = [System.IO.Path]::GetFileName($Current)
if ($fileName -eq 'CHANGELOG.md') {
    $merged = Merge-HarnessTestLogChangelog -Texts $inputs
    [System.IO.File]::WriteAllText($Current, $merged, $utf8NoBom)
} else {
    $merged = Merge-HarnessTestLogIndexFromJson -JsonInputs $inputs
    Write-HarnessTestLogIndexFile -Index $merged -Path $Current | Out-Null
}

exit 0
