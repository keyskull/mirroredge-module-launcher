# Single-script multiplayer bot test
# Kills everything, starts fresh, tests end-to-end
param([int]$BotCount=2, [int]$PlaySec=25, [string]$Level="tutorial_p")

$ErrorActionPreference = "Continue"
$GameRoot = $env:ME_DEPLOY_PATH
if (-not $GameRoot) {
  $cfg = Join-Path (Split-Path $PSScriptRoot -Parent) "deploy.config.json"
  if (Test-Path $cfg) {
    $GameRoot = (Get-Content $cfg -Raw | ConvertFrom-Json).deployPath
  }
}
if (-not $GameRoot) { throw "Set ME_DEPLOY_PATH or deploy.config.json deployPath" }

# === Clean ===
Get-Process -Name "MirrorsEdge","ModuleLauncher","multiplayer-server" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 2

# === C# pipe helper ===
Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
public class T {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr d, int id);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    public static string Pipe(string n, string c) {
        try { var p = new NamedPipeClientStream(".", n, PipeDirection.InOut); p.Connect(3000);
              var w = new StreamWriter(p); w.AutoFlush = true; var r = new StreamReader(p);
              w.WriteLine(c); System.Threading.Thread.Sleep(500); var s = r.ReadLine(); p.Close(); return s; }
        catch (Exception e) { return "ERR:"+e.Message; }
    }
}
"@

# === Start server ===
Write-Host "SERVER: Starting..."
$s = Start-Process "$GameRoot\multiplayer-server.exe" -WorkingDirectory $GameRoot -PassThru -WindowStyle Minimized
Start-Sleep 2; Write-Host "SERVER: PID $($s.Id)"

# === Launch launcher & click game ===
Write-Host "LAUNCHER: Starting..."
$l = Start-Process "$GameRoot\ModuleLauncher.exe" -PassThru; Start-Sleep 3
for ($i=0;$i -lt 60;$i++) {
    $l.Refresh(); if ($l.MainWindowHandle -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 500; continue }
    $btn = [T]::GetDlgItem($l.MainWindowHandle, 1004)
    if ($btn -ne [IntPtr]::Zero -and [T]::IsWindowEnabled($btn)) {
        [T]::SetForegroundWindow($l.MainWindowHandle) | Out-Null
        [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")  # Try Enter first
        Write-Host "LAUNCHER: Clicked via Enter"; break
    }
    Start-Sleep -Milliseconds 500
}

# Wait for game
for ($i=0;$i -lt 60;$i++) {
    $g = Get-Process -Name "MirrorsEdge" -ErrorAction SilentlyContinue | select -First 1
    if ($g) { Write-Host "GAME: PID $($g.Id)"; break }; Start-Sleep 1
}
if (-not $g) { Write-Host "FAIL: Game not started"; Get-Process -Name "ModuleLauncher" | Stop-Process -Force; exit 1 }

# === 5-min loop: continuously try to inject, load level, spawn bots ===
Write-Host "LOOP: Starting main test loop..."
$allDone = $false; $start = Get-Date
while (-not $allDone -and ((Get-Date)-$start).TotalMinutes -lt 5) {
    # Try manager pipe
    $ms = [T]::Pipe("mirroredge_module_manager_control", "GET_STATUS")
    if ($ms -notmatch '"hooksInstalled":\s*true') {
        Write-Host "  Waiting for hooks..."; Start-Sleep 3; continue
    }
    
    # Try core pipe  
    $cs = [T]::Pipe("mirroredge_module_control", "GET_STATUS")
    if ($cs -notmatch '"modReady":\s*true') {
        Write-Host "  Waiting for core..."; Start-Sleep 3; continue
    }
    
    # Ensure gameplay hooks
    if ($cs -notmatch '"gameplayHooks":\s*true') {
        Write-Host "  Installing gameplay hooks..."
        [T]::Pipe("mirroredge_module_control", "ENSURE_GAMEPLAY_HOOKS") | Out-Null
        [T]::Pipe("mirroredge_module_control", "ENSURE_MP_HOOKS") | Out-Null
        Start-Sleep 3; continue
    }
    
    # Try to load level if not yet
    if ($cs -notmatch '"currentMap"\s*:\s*"\w\w') {
        Write-Host "  Loading level: $Level"
        [T]::Pipe("mirroredge_module_control", "CONSOLE open $Level") | Out-Null
        Start-Sleep 5
        # Also try CONSOLE_EXEC in case CONSOLE doesn't work
        Write-Host "  Also trying CONSOLE_EXEC"
        $r = [T]::Pipe("mirroredge_module_manager_control", "CONSOLE_EXEC", 2000)
        if ($r -notmatch "ERR") {
            [T]::Pipe("mirroredge_module_manager_control", "CONSOLE_EXEC open $Level") | Out-Null
        }
        Start-Sleep 5; continue
    }
    
    # Inject multiplayer
    if ($cs -notmatch '"connected":\s*true') {
        # Inject if needed
        Write-Host "  Injecting multiplayer..."
        [T]::Pipe("mirroredge_module_manager_control", "INJECT multiplayer") | Out-Null
        Start-Sleep 5
        
        # Configure
        [T]::Pipe("mirroredge_module_control", "SET multiplayer.server 127.0.0.1") | Out-Null
        [T]::Pipe("mirroredge_module_control", "SET multiplayer.room playthrough-lobby") | Out-Null
        [T]::Pipe("mirroredge_module_control", "SET multiplayer.name TestHost") | Out-Null
        [T]::Pipe("mirroredge_module_control", "RELOAD_SETTINGS") | Out-Null
        Start-Sleep 3; continue
    }
    
    # Connected! Start bots if needed
    if ($cs -match '"remotePlayers"\s*:\s*0') {
        Write-Host "  Starting bots..."
        $botScript = Join-Path $PSScriptRoot "tools\debug-harness\tools\bot.ps1"
        for ($i=0;$i -lt $BotCount;$i++) {
            $ch = @(1,5)[$i]
            Start-Process powershell -ArgumentList "-NoProfile -File `"$botScript`" -Server 127.0.0.1 -Port 5222 -Name Bot-$ch -Room playthrough-lobby -Character $ch -Level `"$Level`" -RunSeconds 120" -WindowStyle Minimized
            Write-Host "    Bot-$ch started"; Start-Sleep 1
        }
        Start-Sleep 5; continue
    }
    
    # Check if bots visible
    if ($cs -match '"remotePlayers"\s*:\s*(\d+)') {
        $r = [int]$Matches[1]
        Write-Host "  Remote players: $r"
        if ($r -ge $BotCount) {
            Write-Host "`n=== SUCCESS: $r remote players visible! ==="
            # Print full status
            Write-Host ([T]::Pipe("mirroredge_module_control", "GET_STATUS"))
            $allDone = $true
        }
    }
    
    Start-Sleep 3
}

if (-not $allDone) {
    Write-Host "`n=== Timeout reached. Final status: ==="
    Write-Host "Manager: $([T]::Pipe('mirroredge_module_manager_control','GET_STATUS'))"
    Write-Host "Core: $([T]::Pipe('mirroredge_module_control','GET_STATUS'))"
}

Write-Host "`n=== Done. Game still running. ==="