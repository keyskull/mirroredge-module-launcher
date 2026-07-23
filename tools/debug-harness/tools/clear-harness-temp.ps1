#Requires -Version 5.1
<#
.SYNOPSIS
  Delete old harness screenshots, reflections, empty session dirs, and stale TEMP logs.

.DESCRIPTION
  Targets under %TEMP%:
    mirroredge-debug\     (shots, session dirs, ndjson)
    mirroredge-reflections\
    mirroredge-freeze\
  Plus loose TEMP files: *.bak client logs, mp-real-level-bots-run-*.log, soft-collision.ok

  Policy (defaults):
    - Delete files older than RetainDays
    - Keep at most KeepNewestShots PNGs in real-level-bots (newest win)
    - Keep at most KeepNewestReflections reflection session dirs
    - Always remove empty directories under mirroredge-debug

.EXAMPLE
  .\clear-harness-temp.ps1
  .\clear-harness-temp.ps1 -RetainDays 1 -KeepNewestShots 30
  . .\clear-harness-temp.ps1; Clear-HarnessTempArtifacts -RetainDays 2
#>
param(
    [int]$RetainDays = 2,
    [int]$KeepNewestShots = 40,
    [int]$KeepNewestReflections = 20,
    [switch]$WhatIf
)

function Clear-HarnessTempArtifacts {
    [CmdletBinding()]
    param(
        [int]$RetainDays = 2,
        [int]$KeepNewestShots = 40,
        [int]$KeepNewestReflections = 20,
        [switch]$WhatIf
    )

    if ($RetainDays -lt 0) { $RetainDays = 0 }
    if ($KeepNewestShots -lt 1) { $KeepNewestShots = 1 }
    if ($KeepNewestReflections -lt 1) { $KeepNewestReflections = 1 }

    $cutoff = (Get-Date).AddDays(-$RetainDays)
    $state = @{
        removedFiles = 0
        removedDirs  = 0
        freedBytes   = [int64]0
    }

    function Remove-HarnessItem {
        param([string]$Path, [switch]$Recurse)
        if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return }
        try {
            $item = Get-Item -LiteralPath $Path -Force -EA Stop
            $size = [int64]0
            if (-not $item.PSIsContainer) {
                $size = [int64]$item.Length
            } elseif ($Recurse) {
                $size = [int64]((Get-ChildItem -LiteralPath $Path -Recurse -File -Force -EA SilentlyContinue |
                    Measure-Object Length -Sum).Sum)
            }
            if ($WhatIf) {
                Write-Host ("WhatIf remove: {0}" -f $Path)
            } else {
                Remove-Item -LiteralPath $Path -Recurse:$Recurse -Force -EA Stop
            }
            $state.freedBytes += $size
            if ($item.PSIsContainer) { $state.removedDirs++ } else { $state.removedFiles++ }
        } catch {
            # locked / racing - ignore
        }
    }

    $debugRoot = Join-Path $env:TEMP "mirroredge-debug"
    $reflectRoot = Join-Path $env:TEMP "mirroredge-reflections"
    $freezeRoot = Join-Path $env:TEMP "mirroredge-freeze"

    # --- age-based file purge under debug / freeze ---
    foreach ($root in @($debugRoot, $freezeRoot)) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        Get-ChildItem -LiteralPath $root -Recurse -File -Force -EA SilentlyContinue |
            Where-Object { $_.LastWriteTime -lt $cutoff } |
            ForEach-Object { Remove-HarnessItem -Path $_.FullName }
    }

    # --- real-level-bots: keep newest N shots even within RetainDays ---
    $shotDir = Join-Path $debugRoot "real-level-bots"
    if (Test-Path -LiteralPath $shotDir) {
        $shots = @(Get-ChildItem -LiteralPath $shotDir -File -Force -EA SilentlyContinue |
            Where-Object { $_.Extension -match '\.(png|jpg|jpeg|bmp)$' } |
            Sort-Object LastWriteTime -Descending)
        if ($shots.Count -gt $KeepNewestShots) {
            $shots | Select-Object -Skip $KeepNewestShots | ForEach-Object {
                Remove-HarnessItem -Path $_.FullName
            }
        }
    }

    # --- reflection session dirs: age + keep newest N ---
    if (Test-Path -LiteralPath $reflectRoot) {
        $dirs = @(Get-ChildItem -LiteralPath $reflectRoot -Directory -Force -EA SilentlyContinue |
            Sort-Object LastWriteTime -Descending)
        $i = 0
        foreach ($d in $dirs) {
            $i++
            $tooOld = ($d.LastWriteTime -lt $cutoff)
            $overCap = ($i -gt $KeepNewestReflections)
            if ($tooOld -or $overCap) {
                Remove-HarnessItem -Path $d.FullName -Recurse
            }
        }
    }

    # --- empty session dirs under mirroredge-debug (deepest first) ---
    if (Test-Path -LiteralPath $debugRoot) {
        Get-ChildItem -LiteralPath $debugRoot -Directory -Recurse -Force -EA SilentlyContinue |
            Sort-Object { $_.FullName.Length } -Descending |
            ForEach-Object {
                $hasFile = [bool](Get-ChildItem -LiteralPath $_.FullName -Recurse -File -Force -EA SilentlyContinue |
                    Select-Object -First 1)
                if (-not $hasFile) {
                    Remove-HarnessItem -Path $_.FullName -Recurse
                }
            }
        # empty top-level leftover after nested deletes
        Get-ChildItem -LiteralPath $debugRoot -Directory -Force -EA SilentlyContinue |
            ForEach-Object {
                $hasFile = [bool](Get-ChildItem -LiteralPath $_.FullName -Recurse -File -Force -EA SilentlyContinue |
                    Select-Object -First 1)
                if (-not $hasFile) {
                    Remove-HarnessItem -Path $_.FullName -Recurse
                }
            }
    }

    # --- loose TEMP harness logs (stale only; never wipe live client.log here) ---
    $loosePatterns = @(
        "mirroredge-multiplayer-client.log.bak*",
        "mp-real-level-bots-run-*.log",
        "mirroredge-multiplayer-server-stdout.log",
        "mirroredge-soft-collision.ok"
    )
    foreach ($pat in $loosePatterns) {
        Get-ChildItem -LiteralPath $env:TEMP -Filter $pat -File -Force -EA SilentlyContinue |
            Where-Object {
                # soft-collision flag: always ok to drop between runs
                $_.Name -eq "mirroredge-soft-collision.ok" -or $_.LastWriteTime -lt $cutoff
            } |
            ForEach-Object { Remove-HarnessItem -Path $_.FullName }
    }

    [pscustomobject]@{
        retainDays = $RetainDays
        keepNewestShots = $KeepNewestShots
        keepNewestReflections = $KeepNewestReflections
        removedFiles = $state.removedFiles
        removedDirs = $state.removedDirs
        freedMB = [math]::Round($state.freedBytes / 1MB, 1)
        whatIf = [bool]$WhatIf
        cutoff = $cutoff.ToString("o")
    }
}

$isDotSourced = ($MyInvocation.InvocationName -eq '.') -or
    ($MyInvocation.Line -match '^\s*\.\s+')
if (-not $isDotSourced) {
    $stats = Clear-HarnessTempArtifacts -RetainDays $RetainDays `
        -KeepNewestShots $KeepNewestShots `
        -KeepNewestReflections $KeepNewestReflections `
        -WhatIf:$WhatIf
    Write-Host ("clear-harness-temp: removed files={0} dirs={1} freedMB={2} retainDays={3} keepShots={4}" -f `
        $stats.removedFiles, $stats.removedDirs, $stats.freedMB, $stats.retainDays, $stats.keepNewestShots)
}
