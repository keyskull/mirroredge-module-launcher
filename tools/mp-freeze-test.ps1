#Requires -Version 5.1
<#
.SYNOPSIS
  Multiplayer bot test with a freeze watchdog.

.DESCRIPTION
  Runs the multiplayer injection/bot sequence against an already-running
  MirrorsEdge process (the user manually reaches a level first), then watches
  the game for a hang. When the main window stops pumping messages the watchdog:
    1. decodes the engine breadcrumb ring (where the game-thread is stuck),
    2. snapshots core + manager GET_STATUS,
    3. optionally captures a procdump,
    4. copies all diagnostic logs into a timestamped report folder,
    5. kills MirrorsEdge so the machine recovers (no manual restart).

  This replaces the blind "launch -> freeze -> guess -> rebuild" loop: every run
  ends with a concrete "last phase before freeze" line.

.PARAMETER Level
  Level name for bots. Empty = auto-detect from core GET_STATUS currentMap.

.PARAMETER HangTimeoutSec
  Consecutive seconds of IsHungAppWindow before declaring a freeze (default 8).

.PARAMETER CaptureDump
  Download + run procdump -ma against the hung process for a full memory dump.
#>
param(
    [int]$BotCount = 2,
    [string]$Level = "",
    [int]$PlaySeconds = 90,
    [int]$HangTimeoutSec = 8,
    [string]$Server = "127.0.0.1",
    [int]$Port = 5222,
    [string]$Room = "playthrough-lobby",
    [string]$Name = "Tester",
    [switch]$NoBots,
    [switch]$NoInject,
    [switch]$NoKill,
    [switch]$CaptureDump
)

$ErrorActionPreference = "Continue"
$Repo = Split-Path $PSScriptRoot -Parent
$CoreP = "mirroredge_module_control"
$MgrP  = "mirroredge_module_manager_control"

Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
public class FZ {
    [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr hwnd);
    // Pipe command with a bounded connect; the module pipe server runs on its
    // own thread, so it can answer GET_STATUS even while the game thread hangs.
    public static string Pipe(string name, string cmd, int connectMs) {
        try {
            var p = new NamedPipeClientStream(".", name, PipeDirection.InOut);
            p.Connect(connectMs);
            var w = new StreamWriter(p); w.AutoFlush = true;
            var r = new StreamReader(p);
            w.WriteLine(cmd);
            System.Threading.Thread.Sleep(300);
            string s = r.ReadLine();
            p.Close();
            return s;
        } catch (Exception e) { return "ERR:" + e.Message; }
    }
}
"@

function Pipe($name, $cmd, $ms = 2000) { return [FZ]::Pipe($name, $cmd, $ms) }
function CoreCmd($cmd) { return Pipe $CoreP $cmd }
function MgrCmd($cmd)  { return Pipe $MgrP $cmd }

function Get-StatusField($status, $rx) {
    if ($status -and $status -match $rx) { return $Matches[1] }
    return $null
}

# freeze breadcrumb ring decoder (mirrors tools/decode-phase.ps1)
function Read-PhaseRing {
    $ringPath = Join-Path $env:TEMP "mirroredge-phase.bin"
    if (-not (Test-Path $ringPath)) { return @() }
    $slotSize = 96
    $fs = [System.IO.File]::Open($ringPath, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $bytes = New-Object byte[] $fs.Length
        [void]$fs.Read($bytes, 0, $bytes.Length)
    } finally { $fs.Close() }
    $slots = [int]($bytes.Length / $slotSize)
    $entries = @()
    for ($i = 0; $i -lt $slots; $i++) {
        $text = [System.Text.Encoding]::ASCII.GetString($bytes, $i * $slotSize, $slotSize)
        $text = $text.Trim([char]0, ' ', "`r", "`n")
        if (-not $text) { continue }
        $seq = -1
        if ($text -match 'seq=(\d+)') { $seq = [int64]$Matches[1] }
        $entries += [pscustomobject]@{ Seq = $seq; Text = $text }
    }
    return ($entries | Sort-Object Seq -Descending)
}

function Get-GameProcess {
    return (Get-Process -Name "MirrorsEdge" -ErrorAction SilentlyContinue | Select-Object -First 1)
}

# freeze / crash report collector
function Save-Report($reason, $game) {
    $ts = Get-Date -Format "yyyyMMdd-HHmmss"
    $dir = Join-Path $env:TEMP "mirroredge-freeze\$ts-$reason"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    $ring = Read-PhaseRing
    $lastPhase = if ($ring.Count -gt 0) { $ring[0].Text } else { "(ring empty/absent)" }

    $core = CoreCmd "GET_STATUS"
    $mgr  = MgrCmd  "GET_STATUS"

    $summary = @()
    $summary += "reason      : $reason"
    $summary += "time        : $(Get-Date -Format o)"
    $summary += "game pid    : $(if ($game) { $game.Id } else { 'exited' })"
    $summary += "LAST PHASE  : $lastPhase"
    $summary += ""
    $summary += "=== phase ring (newest first) ==="
    $summary += ($ring | Select-Object -First 25 | ForEach-Object { $_.Text })
    $summary += ""
    $summary += "=== core GET_STATUS ==="
    $summary += $core
    $summary += ""
    $summary += "=== manager GET_STATUS ==="
    $summary += $mgr
    $summary | Set-Content -Path (Join-Path $dir "summary.txt") -Encoding UTF8

    foreach ($fn in @("mirroredge-phase.bin", "engine_tick_diag.txt",
                      "spawn_drain_trace.txt", "mirroredge-engine-spawn.log",
                      "session.log")) {
        $src = Join-Path $env:TEMP $fn
        if (Test-Path $src) { Copy-Item $src $dir -ErrorAction SilentlyContinue }
    }

    if ($CaptureDump -and $game) {
        $procDump = Join-Path $env:TEMP "procdump.exe"
        if (-not (Test-Path $procDump)) {
            try {
                Invoke-WebRequest -Uri "https://live.sysinternals.com/procdump.exe" `
                    -OutFile $procDump -UseBasicParsing
            } catch { Write-Host "  procdump download failed: $($_.Exception.Message)" }
        }
        if (Test-Path $procDump) {
            Write-Host "  capturing full dump (procdump -ma pid $($game.Id))..."
            & $procDump -accepteula -ma $game.Id (Join-Path $dir "MirrorsEdge.dmp") 2>&1 |
                Out-File (Join-Path $dir "procdump.log")
        }
    }

    return @{ Dir = $dir; LastPhase = $lastPhase }
}

# ====================================================================
Write-Host "=== mp-freeze-test ==="
$game = Get-GameProcess
if (-not $game) {
    Write-Host "MirrorsEdge is not running. Launch the game, reach a level (Faith visible), then rerun."
    exit 1
}
Write-Host "attached to MirrorsEdge pid $($game.Id)"

# Confirm hooks + core are up (pipe server thread answers even mid-load).
$hooks = MgrCmd "GET_STATUS"
Write-Host "manager: $($hooks.Substring(0, [Math]::Min(120, $hooks.Length)))"

$status = CoreCmd "GET_STATUS"
$detectedMap = Get-StatusField $status '"currentMap":"(\w+)'
if (-not $Level) {
    $Level = $detectedMap
    if (-not $Level -or $Level -eq "tdmainmenu") {
        $Level = "tutorial_p"
    }
}
$inGameplay = Get-StatusField $status '"inGameplay":(true|false)'
if ($detectedMap -eq "tdmainmenu" -or $inGameplay -ne "true") {
    Write-Host "WARNING: Game appears to be at the main menu (map=$detectedMap)."
    Write-Host "  Multiplayer bots require a gameplay level."
    Write-Host "  Navigate to a level in-game first, then rerun this script."
    exit 2
}
Write-Host "level: $Level (detected: $detectedMap, inGameplay: $inGameplay)"

# setup sequence (each step non-fatal; watchdog runs regardless)
if (-not $NoInject) {
    Write-Host "ENSURE_GAMEPLAY_HOOKS / ENSURE_MP_HOOKS"
    CoreCmd "ENSURE_GAMEPLAY_HOOKS" | Out-Null
    CoreCmd "ENSURE_MP_HOOKS" | Out-Null

    Write-Host "INJECT multiplayer"
    Write-Host "  $(MgrCmd 'INJECT multiplayer')"
    Start-Sleep 3

    # FORCE_HOSTED_LIVE must run AFTER INJECT because it calls an export
    # in multiplayer.dll (MmMultiplayer_ForcePostLevelInit).  If the DLL
    # isn't loaded yet, it returns "ERR multiplayer not loaded" and the
    # hosted live flag is never set.
    Write-Host "FORCE_HOSTED_LIVE"
    Write-Host "  $(CoreCmd 'FORCE_HOSTED_LIVE')"
    Start-Sleep 1

    CoreCmd "SET multiplayer.server $Server" | Out-Null
    CoreCmd "SET multiplayer.room $Room" | Out-Null
    CoreCmd "SET multiplayer.name $Name" | Out-Null
    CoreCmd "RELOAD_SETTINGS" | Out-Null
    Start-Sleep 2
}

if (-not $NoBots) {
    $botScript = Join-Path $Repo "tools\debug-harness\tools\bot.ps1"
    if (Test-Path $botScript) {
        Write-Host "starting $BotCount bots on level $Level"
        $chars = @(1, 5, 2, 6)
        for ($i = 0; $i -lt $BotCount; $i++) {
            $ch = $chars[$i % $chars.Count]
            $runFor = $PlaySeconds + 30
            $a = "-NoProfile -File `"$botScript`" -Server $Server -Port $Port " +
                 "-Room $Room -Character $ch -Level `"$Level`" -Name Bot-$ch -RunSeconds $runFor"
            Start-Process powershell -ArgumentList $a -WindowStyle Minimized
            Write-Host "  Bot-$ch"
            Start-Sleep 1
        }
    } else {
        Write-Host "bot script not found: $botScript"
    }
}

# watchdog loop
Write-Host ""
Write-Host "=== watchdog (hang > $HangTimeoutSec s -> capture + kill) for $PlaySeconds s ==="
$hungSince = $null
$deadline = (Get-Date).AddSeconds($PlaySeconds)
$outcome = "timeout"

while ((Get-Date) -lt $deadline) {
    $game = Get-GameProcess
    if (-not $game) {
        $outcome = "crash"
        break
    }
    $game.Refresh()
    $hwnd = $game.MainWindowHandle

    $hung = $false
    if ($hwnd -ne [IntPtr]::Zero) {
        $hung = [FZ]::IsHungAppWindow($hwnd)
    }

    if ($hung) {
        if (-not $hungSince) { $hungSince = Get-Date }
        $elapsed = [int]((Get-Date) - $hungSince).TotalSeconds
        Write-Host "  [HUNG $elapsed s] window not responding..."
        if ($elapsed -ge $HangTimeoutSec) {
            $outcome = "freeze"
            break
        }
    } else {
        $hungSince = $null
        $status = CoreCmd "GET_STATUS"
        $map = Get-StatusField $status '"currentMap":"(\w*)'
        $rm  = Get-StatusField $status '"remotePlayers":(\d+)'
        $sp  = Get-StatusField $status '"spawnedPlayers":(\d+)'
        $ring = Read-PhaseRing
        $lp = "-"
        if ($ring.Count -gt 0) {
            $parts = $ring[0].Text -split ' '
            $lp = $parts[$parts.Count - 1]
        }
        Write-Host ("  map={0} remote={1} spawned={2} phase={3}" -f $map, $rm, $sp, $lp)
        if ($sp -and [int]$sp -ge 1) {
            $outcome = "spawned"
            break
        }
    }
    Start-Sleep 1
}

# outcome handling
Write-Host ""
switch ($outcome) {
    "spawned" {
        Write-Host "=== SUCCESS: spawnedPlayers >= 1 (bots visible) ==="
        Write-Host (CoreCmd "GET_STATUS")
    }
    "freeze" {
        Write-Host "=== FREEZE DETECTED ==="
        $r = Save-Report "freeze" (Get-GameProcess)
        Write-Host "LAST PHASE BEFORE FREEZE: $($r.LastPhase)"
        Write-Host "report: $($r.Dir)"
        if (-not $NoKill) {
            Get-Process -Name "MirrorsEdge" -ErrorAction SilentlyContinue | Stop-Process -Force
            Write-Host "killed MirrorsEdge (recovered)"
        }
    }
    "crash" {
        Write-Host "=== GAME PROCESS EXITED (crash) ==="
        $r = Save-Report "crash" $null
        Write-Host "LAST PHASE BEFORE EXIT: $($r.LastPhase)"
        Write-Host "report: $($r.Dir)"
    }
    default {
        Write-Host "=== TIMEOUT after $PlaySeconds s, no freeze, no spawn ==="
        $ring = Read-PhaseRing
        if ($ring.Count -gt 0) { Write-Host "last phase: $($ring[0].Text)" }
        Write-Host (CoreCmd "GET_STATUS")
    }
}
