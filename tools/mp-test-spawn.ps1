$ErrorActionPreference = "Stop"

# ── Helpers ──
function CoreQ($c) {
    $r = "ERR"; $cl = $null; $wr = $null; $rd = $null
    try {
        $cl = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'mirroredge_module_control', [System.IO.Pipes.PipeDirection]::InOut)
        $cl.Connect(5000)
        $wr = New-Object System.IO.StreamWriter($cl); $wr.AutoFlush = $true; $wr.WriteLine($c)
        Start-Sleep -Milliseconds 400
        $rd = New-Object System.IO.StreamReader($cl); $r = $rd.ReadLine()
        if ($null -eq $r) { $r = "null" }
    } catch { $r = "ERR" }
    finally {
        if ($rd) { try { $rd.Dispose() } catch {} }
        if ($wr) { try { $wr.Dispose() } catch {} }
        if ($cl) { try { $cl.Dispose() } catch {} }
    }
    return $r
}

function MgrQ($c) {
    $r = "ERR"; $cl = $null; $wr = $null; $rd = $null
    try {
        $cl = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'mirroredge_module_manager_control', [System.IO.Pipes.PipeDirection]::InOut)
        $cl.Connect(5000)
        $wr = New-Object System.IO.StreamWriter($cl); $wr.AutoFlush = $true; $wr.WriteLine($c)
        Start-Sleep -Milliseconds 400
        $rd = New-Object System.IO.StreamReader($cl); $r = $rd.ReadLine()
        if ($null -eq $r) { $r = "null" }
    } catch { $r = "ERR" }
    finally {
        if ($rd) { try { $rd.Dispose() } catch {} }
        if ($wr) { try { $wr.Dispose() } catch {} }
        if ($cl) { try { $cl.Dispose() } catch {} }
    }
    return $r
}

# ── Phase 0: Clean ──
Get-Process "MirrorsEdge", "ModuleLauncher", "multiplayer-server" -EA 0 | Stop-Process -Force -EA 0
Get-Process powershell -EA 0 | Where-Object { $_.WS -gt 40MB -and $_.Id -ne $pid } | Stop-Process -Force -EA 0
Start-Sleep 2

$coreSettings = "$env:TEMP\core.settings"
@"
[client]
server = "127.0.0.1"
room = "playthrough-lobby"

[multiplayer]
look = "Kate"
"@ | Out-File $coreSettings -Encoding ascii

Remove-Item "$env:TEMP\spawn_drain_trace.txt" -EA 0
Remove-Item "$env:TEMP\mmultiplayer-server.log" -EA 0
"Phase 0: clean done"

# ── Phase 1: Server ──
$gameRoot = "C:\Program Files (x86)\Steam\steamapps\common\mirrors edge"
$servDir = "$gameRoot\modules\multiplayer"
$serv = Start-Process -FilePath "$servDir\multiplayer-server.exe" -WorkingDirectory $servDir -PassThru -WindowStyle Minimized
"Phase 1: server PID=$($serv.Id)"

# ── Phase 2: Launch ──
Set-Location $gameRoot
$launcher = Start-Process -FilePath ".\ModuleLauncher.exe" -WorkingDirectory "." -PassThru -WindowStyle Minimized
Start-Sleep 3

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class WB {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string w);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte s, uint f, IntPtr e);
}
'@

$hwnd = [WB]::FindWindow($null, "Module Launcher")
if ($hwnd) {
    [WB]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 500
    # Click Launch (Tab * 4 then Enter)
    for ($i = 0; $i -lt 4; $i++) {
        [WB]::keybd_event(9, 0, 0, 0)
        Start-Sleep -Milliseconds 50
        [WB]::keybd_event(9, 0, 2, 0)
        Start-Sleep -Milliseconds 50
    }
    [WB]::keybd_event(13, 0, 0, 0)
    Start-Sleep -Milliseconds 50
    [WB]::keybd_event(13, 0, 2, 0)
    "Phase 2: LAUNCHED"
} else {
    "Phase 2: Launcher window not found"
}

# ── Phase 3: Wait for game + pipe ──
for ($i = 0; $i -lt 90; $i++) {
    Start-Sleep 2
    $g = Get-Process "MirrorsEdge" -EA 0
    if (-not $g) { continue }
    try {
        $cl = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'mirroredge_module_control', [System.IO.Pipes.PipeDirection]::InOut)
        $cl.Connect(2000)
        $wr = New-Object System.IO.StreamWriter($cl); $wr.AutoFlush = $true; $wr.WriteLine("PING")
        Start-Sleep -Milliseconds 300
        $rd = New-Object System.IO.StreamReader($cl); $s = $rd.ReadLine()
        $rd.Dispose(); $wr.Dispose(); $cl.Dispose()
        if ($s) {
            "Phase 3: PIPE WS=$([math]::Round($g.WorkingSet64/1MB))MB"
            break
        }
    } catch { }
    if ($i -eq 89) { "Phase 3: Pipe timeout"; exit 1 }
}

# ── Phase 4: Hooks + Inject MP ──
$r = CoreQ "ENSURE_GAMEPLAY_HOOKS"
"Phase 4: HOOKS=${r}"
$r = MgrQ "INJECT multiplayer"
"Phase 4: INJECT=${r}"
Start-Sleep 2

# Wait for connected
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep 2
    $s = CoreQ "GET_STATUS"
    $ct = if ($s -match '"connected":(\w+)') { $Matches[1] } else { "?" }
    if ($ct -eq "true") {
        "Phase 4: connected=true"
        break
    }
    if ($i -eq 29) { "Phase 4: connect timeout"; exit 1 }
}

# ── Phase 5: START_GAME ──
$r = CoreQ "START_GAME tutorial_p"
"Phase 5: START_GAME=${r}"

for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep 2
    $g = Get-Process "MirrorsEdge" -EA 0
    if (-not $g) { "Phase 5: Game crashed"; exit 1 }
    $s = CoreQ "GET_STATUS"
    if ($s -match '"clientMap":"([^"]*)"') { $cm = $Matches[1] }
    if ($s -match '"connected":(\w+)') { $ct = $Matches[1] }
    if ($s -match '"inGameplay":(\w+)') { $ig = $Matches[1] }
    if ($cm -eq "tutorial_p") {
        "Phase 5: map=${cm} ig=${ig} connected=${ct}"
        break
    }
    if ($i -eq 59) { "Phase 5: Level load timeout"; exit 1 }
}
"Phase 5: LEVEL WS=$([math]::Round($g.WorkingSet64/1MB))MB"

# ── Phase 6: Start bots ──
$Repo = Split-Path $PSScriptRoot -Parent
$bs = "$repo\tools\debug-harness\tools\bot.ps1"
$bd = Split-Path $bs -Parent
$args = "-Server 127.0.0.1 -Port 5222 -Room playthrough-lobby -Level tutorial_p -RunSeconds 0 -KeepAlive"
Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -Command `"& '$bs' $args -Name Kate -Character 1`"" -WorkingDirectory $bd -PassThru -WindowStyle Minimized | Out-Null
Start-Sleep 1
Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -Command `"& '$bs' $args -Name Miller -Character 5`"" -WorkingDirectory $bd -PassThru -WindowStyle Minimized | Out-Null
"Phase 6: bots started"

# ── Phase 7: Wait for remotes ──
for ($i = 0; $i -lt 15; $i++) {
    Start-Sleep 2
    $s = CoreQ "GET_STATUS"
    if ($s -match '"remotePlayers":(\d+)') { $rm = [int]$Matches[1] }
    "  [${i}s] remote=${rm}"
    if ($rm -ge 2) { break }
}
"Phase 7: remotes=${rm}"

# ── Phase 8: FORCE_HOSTED_LIVE + Spawn poll ──
CoreQ "FORCE_HOSTED_LIVE" | Out-Null
"Phase 8: FORCE_HOSTED_LIVE sent"

for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep 3
    $s = CoreQ "GET_STATUS"
    $rm = 0; $sp = 0
    if ($s -match '"remotePlayers":(\d+)') { $rm = [int]$Matches[1] }
    if ($s -match '"spawnedPlayers":(\d+)') { $sp = [int]$Matches[1] }
    if ($i % 4 -eq 0) { "Phase 8: [${i}s] remote=${rm} spawned=${sp}" }
    if ($sp -ge 1) {
        "Phase 8: >>> SPAWNED ${sp} players!"
        break
    }
}

$s = CoreQ "GET_STATUS"
$rm = 0; $sp = 0
if ($s -match '"remotePlayers":(\d+)') { $rm = [int]$Matches[1] }
if ($s -match '"spawnedPlayers":(\d+)') { $sp = [int]$Matches[1] }
"Phase 8: FINAL remote=${rm} spawned=${sp}"

if ($sp -ge 1) {
    ">>> SUCCESS - bots visible!"
} else {
    "--- FAIL ---"
    # Dump debug info
    "`n=== spawn_drain_trace last 10 ==="
    $dt = "$env:TEMP\spawn_drain_trace.txt"
    if (Test-Path $dt) { Get-Content $dt -Tail 10 | ForEach-Object { $_ } }
}
