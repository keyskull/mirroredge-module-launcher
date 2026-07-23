#Requires -Version 5.1
<#
.SYNOPSIS
  Decode the engine freeze-breadcrumb ring (%TEMP%\mirroredge-phase.bin).

.DESCRIPTION
  engine.dll writes a rolling ring of "phase" markers into a memory-mapped file
  from the game/EndScene thread (EngineInternal::SetPhase). When the game hangs,
  the entry with the HIGHEST seq is the last thing the main thread ran before it
  stopped — i.e. where it is stuck, with no debugger or symbols required.

  Slot layout: 48 slots x 96 bytes, each an ASCII line:
    "seq=00000123 t=HH:MM:SS.mmm tid=1234 <phase>"

.PARAMETER Top
  How many most-recent entries to print (default 20).

.PARAMETER Path
  Override the ring file path (default %TEMP%\mirroredge-phase.bin).

.PARAMETER Raw
  Print entries in ring-slot order instead of sorted by seq.
#>
param(
    [int]$Top = 20,
    [string]$Path = (Join-Path $env:TEMP "mirroredge-phase.bin"),
    [switch]$Raw
)

function Read-PhaseRing {
    param([string]$RingPath)

    if (-not (Test-Path $RingPath)) {
        return $null
    }

    $slotSize = 96
    # Read with shared access — the game keeps the file mapped/open.
    $fs = [System.IO.File]::Open($RingPath, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $bytes = New-Object byte[] $fs.Length
        [void]$fs.Read($bytes, 0, $bytes.Length)
    } finally {
        $fs.Close()
    }

    $slots = [int]($bytes.Length / $slotSize)
    $entries = @()
    for ($i = 0; $i -lt $slots; $i++) {
        $text = [System.Text.Encoding]::ASCII.GetString($bytes, $i * $slotSize, $slotSize)
        $text = $text.Trim([char]0, ' ', "`r", "`n")
        if (-not $text) { continue }
        $seq = -1
        if ($text -match 'seq=(\d+)') { $seq = [int64]$Matches[1] }
        $entries += [pscustomobject]@{ Slot = $i; Seq = $seq; Text = $text }
    }
    return $entries
}

$entries = Read-PhaseRing -RingPath $Path
if ($null -eq $entries) {
    Write-Host "phase ring not found: $Path"
    Write-Host "(game not started with breadcrumb build, or never reached SetPhase)"
    exit 2
}
if ($entries.Count -eq 0) {
    Write-Host "phase ring is empty: $Path"
    exit 0
}

if ($Raw) {
    $ordered = $entries | Sort-Object Slot
} else {
    $ordered = $entries | Sort-Object Seq -Descending | Select-Object -First $Top
}

Write-Host "=== engine phase ring ($Path) ==="
Write-Host ("entries={0}  showing={1}  (highest seq = last main-thread phase before freeze)" -f $entries.Count, $ordered.Count)
Write-Host "----------------------------------------------------------------"
$first = $true
foreach ($e in $ordered) {
    $marker = if ($first -and -not $Raw) { " <== LAST" } else { "" }
    Write-Host ("{0}{1}" -f $e.Text, $marker)
    $first = $false
}
