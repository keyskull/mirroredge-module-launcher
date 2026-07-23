#Requires -Version 5.1
<#
.SYNOPSIS
  Bundle Mirror's Edge module launcher diagnostics from any machine.

.EXAMPLE
  .\tools\collect-diagnostics.ps1 -GameRoot "C:\Games\Mirror's Edge"
#>
[CmdletBinding()]
param(
    [string]$GameRoot = "",
    [string]$OutDir = "",
    [switch]$IncludeRunningPipeLogs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-GameRoot {
    param([string]$Hint)
    if ($Hint -and (Test-Path $Hint)) {
        $p = (Resolve-Path $Hint).Path
        if (Test-Path (Join-Path $p "Binaries\MirrorsEdge.exe")) { return $p }
        $parent = Split-Path $p -Parent
        if ($parent -and (Test-Path (Join-Path $parent "Binaries\MirrorsEdge.exe"))) { return $parent }
        return $p
    }
    $candidates = @(
        (Join-Path $env:USERPROFILE "EA Games\Mirrors Edge"),
        (Join-Path ${env:ProgramFiles(x86)} "Steam\steamapps\common\Mirror's Edge"),
        (Join-Path $env:ProgramFiles "EA Games\Mirror's Edge")
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "Binaries\MirrorsEdge.exe")) { return $c }
    }
    return $null
}

function Read-JsonFile {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $null }
    try {
        return Get-Content $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        Write-Warning "Failed to parse JSON: $Path"
        return $null
    }
}

function Copy-IfExists {
    param([string]$Source, [string]$DestDir, [string]$Label = "")
    if (-not (Test-Path $Source)) { return $false }
    $name = Split-Path $Source -Leaf
    $dest = Join-Path $DestDir $name
    if ($Label) {
        $dest = Join-Path $DestDir $Label
    }
    Copy-Item -LiteralPath $Source -Destination $dest -Force
    Write-Host "  + $name"
    return $true
}

function Get-RecentWerEvents {
    param([int]$Max = 8)
    $names = @("MirrorsEdge.exe", "ModuleLauncher.exe", "mirroredge-module-launcher.exe")
    $events = @()
    try {
        $raw = Get-WinEvent -FilterHashtable @{
            LogName   = "Application"
            Id        = 1000
            StartTime = (Get-Date).AddDays(-7)
        } -MaxEvents 200 -ErrorAction SilentlyContinue
        foreach ($e in $raw) {
            $msg = $e.Message
            foreach ($n in $names) {
                if ($msg -match [regex]::Escape($n)) {
                    $events += [pscustomobject]@{
                        time    = $e.TimeCreated.ToString("o")
                        id      = $e.Id
                        source  = $e.ProviderName
                        message = ($msg -replace "\s+", " ").Substring(0, [Math]::Min(500, $msg.Length))
                    }
                    break
                }
            }
            if ($events.Count -ge $Max) { break }
        }
    } catch {
        Write-Warning "WER query skipped: $($_.Exception.Message)"
    }
    return $events
}

function Try-ManagerPipeLog {
    param([string]$DestFile)
    $pipeName = "\\.\pipe\mirroredge_module_manager_control"
    try {
        $client = New-Object System.IO.Pipes.NamedPipeClientStream ".", "mirroredge_module_manager_control", [System.IO.Pipes.PipeDirection]::InOut
        $client.Connect(2000)
        $writer = New-Object System.IO.StreamWriter($client)
        $reader = New-Object System.IO.StreamReader($client)
        $writer.WriteLine("GET_LOG")
        $writer.Flush()
        $response = $reader.ReadLine()
        $client.Close()
        if ($response) {
            Set-Content -LiteralPath $DestFile -Value $response -Encoding UTF8
            Write-Host "  + manager-pipe-log.txt (live)"
            return $true
        }
    } catch {
        Write-Host "  (manager pipe unavailable)"
    }
    return $false
}

$resolvedRoot = Resolve-GameRoot -Hint $GameRoot
if (-not $resolvedRoot) {
    Write-Error "Game root not found. Pass -GameRoot pointing to the folder that contains Binaries\MirrorsEdge.exe"
}

$tempDebug = Join-Path $env:TEMP "mirroredge-debug"
$manifest = $null
$manifestPaths = @(
    (Join-Path $resolvedRoot "logs\last-session.json"),
    (Join-Path $tempDebug "last-session.json")
)
foreach ($mp in $manifestPaths) {
    $manifest = Read-JsonFile -Path $mp
    if ($manifest) {
        Write-Host "manifest: $mp"
        break
    }
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if (-not $OutDir) {
    $OutDir = Join-Path $tempDebug "collected\$stamp"
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host "collecting -> $OutDir"
Write-Host "game root: $resolvedRoot"

# Session artifacts
if ($manifest) {
    $manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutDir "last-session.json") -Encoding UTF8
    foreach ($key in @("sessionLog", "ndjsonLog", "logPath")) {
        $p = $null
        if ($manifest.PSObject.Properties.Name -contains $key) {
            $p = $manifest.$key
        }
        if ($p -and (Test-Path -LiteralPath $p)) {
            $leaf = Split-Path $p -Leaf
            Copy-IfExists -Source $p -DestDir $OutDir -Label $leaf
            $sessionDir = Split-Path $p -Parent
            $envJson = Join-Path $sessionDir "environment.json"
            Copy-IfExists -Source $envJson -DestDir $OutDir
        }
    }
}

# Fallback: latest session folder under logs/
$logsRoot = Join-Path $resolvedRoot "logs"
if (Test-Path $logsRoot) {
    $sessions = Get-ChildItem $logsRoot -Directory | Sort-Object LastWriteTime -Descending
    if ($sessions) {
        $latest = $sessions[0].FullName
        Get-ChildItem $latest -File | ForEach-Object {
            Copy-IfExists -Source $_.FullName -DestDir $OutDir
        }
    }
}

# Temp NDJSON sessions (harness)
if (Test-Path $tempDebug) {
    Get-ChildItem $tempDebug -Filter "*.ndjson" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 3 |
        ForEach-Object { Copy-IfExists -Source $_.FullName -DestDir $OutDir }
    Get-ChildItem $tempDebug -Filter "*-interactions.ndjson" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 |
        ForEach-Object { Copy-IfExists -Source $_.FullName -DestDir $OutDir }
    $hangRoot = Join-Path $tempDebug "hang-reports"
    if (Test-Path $hangRoot) {
        $latestHang = Get-ChildItem $hangRoot -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($latestHang) {
            $hangDest = Join-Path $OutDir "hang-report"
            Copy-Item -LiteralPath $latestHang.FullName -Destination $hangDest -Recurse -Force
            Write-Host "  + hang-report/"
        }
    }
}

# Settings
Copy-IfExists -Source (Join-Path $resolvedRoot "settings.json") -DestDir $OutDir
Copy-IfExists -Source (Join-Path $resolvedRoot "VERSION.json") -DestDir $OutDir
Copy-IfExists -Source (Join-Path $resolvedRoot "CHANGELOG.md") -DestDir $OutDir
$coreSettings = Join-Path $env:TEMP "core.settings"
Copy-IfExists -Source $coreSettings -DestDir $OutDir

# Module versions snapshot
$modulesDir = Join-Path $resolvedRoot "modules"
if (Test-Path $modulesDir) {
    $versions = Get-ChildItem $modulesDir -Recurse -Filter "*.dll" |
        Select-Object FullName, Length, LastWriteTime
    $versions | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $OutDir "modules-snapshot.json") -Encoding UTF8
    Write-Host "  + modules-snapshot.json"
}

# WER
$wer = Get-RecentWerEvents
if ($wer) {
    $wer | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutDir "wer-recent.json") -Encoding UTF8
    Write-Host "  + wer-recent.json ($($wer.Count) events)"
}

if ($IncludeRunningPipeLogs) {
    Try-ManagerPipeLog -DestFile (Join-Path $OutDir "manager-pipe-log.txt")
}

$readme = @"
Mirror's Edge Module Launcher — diagnostics bundle
Collected: $(Get-Date -Format o)
GameRoot: $resolvedRoot
See docs/diagnostics-logging.md in the repository.
"@
Set-Content -Path (Join-Path $OutDir "README.txt") -Value $readme -Encoding UTF8

$zipPath = Join-Path (Split-Path $OutDir -Parent) "mirroredge-diagnostics-$stamp.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -LiteralPath $OutDir -DestinationPath $zipPath -Force

Write-Host ""
Write-Host "done: $zipPath"
Write-Host "folder: $OutDir"

return $zipPath
