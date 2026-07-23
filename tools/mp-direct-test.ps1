# Direct MP test v3 - launches game via launcher, uses pipes for everything else
param([int]$BotCount = 2, [int]$PlaySeconds = 25, [string]$Level = "tutorial_p")

$ErrorActionPreference = "Continue"
$GameRoot = $env:ME_DEPLOY_PATH
if (-not $GameRoot) {
  $cfg = Join-Path (Split-Path $PSScriptRoot -Parent) "deploy.config.json"
  if (Test-Path $cfg) {
    $GameRoot = (Get-Content $cfg -Raw | ConvertFrom-Json).deployPath
  }
}
if (-not $GameRoot) { throw "Set ME_DEPLOY_PATH or deploy.config.json deployPath" }

# Clean up first
Get-Process -Name "MirrorsEdge" -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process -Name "ModuleLauncher" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 2

# === C# helpers ===
Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
public class MpTool {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr d, int id);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    public const uint BM_CLICK = 0x00F5;

    public static string Pipe(string name, string cmd, int timeoutMs = 5000) {
        try {
            var p = new NamedPipeClientStream(".", name, PipeDirection.InOut);
            p.Connect(timeoutMs);
            var w = new StreamWriter(p);
            w.AutoFlush = true;
            var r = new StreamReader(p);
            w.WriteLine(cmd);
            System.Threading.Thread.Sleep(300);
            var result = r.ReadLine();
            p.Close();
            return result;
        } catch (Exception ex) { return "ERR:" + ex.Message; }
    }
}
"@

# === Start server ===
Write-Host "=== Starting server ==="
$serverExe = "$GameRoot\multiplayer-server.exe"
if (-not (Test-Path $serverExe)) {
    Write-Host "ERROR: server not found"
    exit 1
}
$server = Start-Process -FilePath $serverExe -WorkingDirectory $GameRoot -PassThru -WindowStyle Minimized
Start-Sleep 2
Write-Host "Server PID: $($server.Id)"

# === Launch launcher ===
Write-Host "=== Launching launcher ==="
$launcherExe = "$GameRoot\ModuleLauncher.exe"
$launcherProc = Start-Process -FilePath $launcherExe -PassThru
Write-Host "Launcher PID: $($launcherProc.Id)"

# === Wait for Launch Game button and click it ===
Write-Host "=== Finding and clicking Launch Game ==="
$launcherHwnd = [IntPtr]::Zero
$clicked = $false
for ($i = 0; $i -lt 60; $i++) {
    try {
        $launcherProc.Refresh()
        $hwnd = $launcherProc.MainWindowHandle
        if ($hwnd -ne [IntPtr]::Zero) {
            $btn = [MpTool]::GetDlgItem($hwnd, 1004)
            if ($btn -ne [IntPtr]::Zero -and [MpTool]::IsWindowEnabled($btn)) {
                Write-Host "Found Launch Game button at iteration $i"
                $launcherHwnd = $hwnd
                [MpTool]::SetForegroundWindow($hwnd) | Out-Null
                Start-Sleep -Milliseconds 200
                [MpTool]::SendMessage($btn, [MpTool]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
                Write-Host "Clicked Launch Game"
                $clicked = $true
                break
            }
        }
    } catch { }
    if ($i % 10 -eq 9) { Write-Host "  Still waiting for button... ($($i/2)s)" }
    Start-Sleep -Milliseconds 500
}

if (-not $clicked) {
    Write-Host "ERROR: Could not click Launch Game"
    exit 1
}

# === Wait for MirrorsEdge.exe ===
Write-Host "=== Waiting for game process ==="
$gameProc = $null
for ($i = 0; $i -lt 60; $i++) {
    $gameProc = Get-Process -Name "MirrorsEdge" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($gameProc) { Write-Host "Game PID: $($gameProc.Id)"; break }
    Start-Sleep 1
}
if (-not $gameProc) { Write-Host "ERROR: Game not started"; exit 1 }

# === Wait for module_manager hooks ===
Write-Host "=== Waiting for hooks ==="
for ($i = 0; $i -lt 120; $i++) {
    $s = [MpTool]::Pipe("mirroredge_module_manager_control", "GET_STATUS")
    if ($s -and $s -match '"hooksInstalled":\s*true') {
        Write-Host "Hooks ready at ${i}s"
        break
    }
    if ($i % 15 -eq 14) { Write-Host "  Waiting hooks... (${i}s): $($s -replace '`n',' ')" }
    Start-Sleep 1
}

# === Wait for core ===
Write-Host "=== Waiting for core auto-load ==="
for ($i = 0; $i -lt 120; $i++) {
    $s = [MpTool]::Pipe("mirroredge_module_control", "GET_STATUS")
    if ($s -and $s -match '"modReady":\s*true') {
        Write-Host "Core ready at ${i}s"
        break
    }
    if ($i % 15 -eq 14) { Write-Host "  Waiting core... (${i}s)" }
    Start-Sleep 1
}

# === Load level via console ===
Write-Host "=== Loading level: $Level ==="
[MpTool]::Pipe("mirroredge_module_control", "ENSURE_GAMEPLAY_HOOKS") | Out-Null
[MpTool]::Pipe("mirroredge_module_control", "ENSURE_MP_HOOKS") | Out-Null
Start-Sleep 1

$r = [MpTool]::Pipe("mirroredge_module_manager_control", "CONSOLE_EXEC open $Level")
Write-Host "CONSOLE_EXEC open $Level -> $r"

# Wait for level
Write-Host "=== Waiting for level load ==="
for ($i = 0; $i -lt 90; $i++) {
    $s = [MpTool]::Pipe("mirroredge_module_control", "GET_STATUS")
    if ($s) {
        $map = ""
        $ig = "?"
        if ($s -match '"currentMap"\s*:\s*"(\w*)"') { $map = $Matches[1] }
        if ($s -match '"inGameplay"\s*:\s*(\w+)') { $ig = $Matches[1] }
        Write-Host "  [${i}s] map='$map' inGameplay=$ig"
        if ($map -and $map -ne "tdmainmenu" -and $map -ne "") {
            Write-Host "Level loaded: $map"
            break
        }
    }
    if ($i % 10 -eq 9) { Write-Host "  Still waiting... (${i}s)" }
    Start-Sleep 1
}

# === Inject multiplayer ===
Write-Host "=== Injecting multiplayer ==="
$r = [MpTool]::Pipe("mirroredge_module_manager_control", "INJECT multiplayer")
Write-Host "Inject result: $r"
Start-Sleep 5

# === Configure ===
Write-Host "=== Configuring client ==="
[MpTool]::Pipe("mirroredge_module_control", "SET multiplayer.server 127.0.0.1") | Out-Null
[MpTool]::Pipe("mirroredge_module_control", "SET multiplayer.room playthrough-lobby") | Out-Null
[MpTool]::Pipe("mirroredge_module_control", "SET multiplayer.name TestHost") | Out-Null
[MpTool]::Pipe("mirroredge_module_control", "RELOAD_SETTINGS") | Out-Null
[MpTool]::Pipe("mirroredge_module_control", "ENSURE_GAMEPLAY_HOOKS") | Out-Null
[MpTool]::Pipe("mirroredge_module_control", "ENSURE_MP_HOOKS") | Out-Null
Start-Sleep 3

# === Poll connection ===
Write-Host "=== Polling multiplayer connection ==="
for ($i = 0; $i -lt 30; $i++) {
    $s = [MpTool]::Pipe("mirroredge_module_control", "GET_STATUS")
    if ($s -and $s -match '"connected":\s*true') {
        Write-Host "Connected at ${i}s"
        break
    }
    if ($i % 5 -eq 4) { Write-Host "  [${i}s] status: $($s.Substring(0, [Math]::Min(200, $s.Length)))" }
    Start-Sleep 1
}

# === Start bots ===
Write-Host "=== Starting $BotCount bots ==="
$botScript = Join-Path $PSScriptRoot "tools\debug-harness\tools\bot.ps1"
$chars = @(1, 5)  # Kate, Miller
for ($i = 0; $i -lt [Math]::Min($BotCount, $chars.Count); $i++) {
    $args = "-NoProfile -File `"$botScript`" -Server 127.0.0.1 -Port 5222 -Name Bot-$($chars[$i]) -Room playthrough-lobby -Character $($chars[$i]) -Level `"$Level`" -RunSeconds $($PlaySeconds+60)"
    Start-Process powershell -ArgumentList $args -WindowStyle Minimized
    Write-Host "  Started Bot-$($chars[$i]) (char=$($chars[$i]))"
    Start-Sleep 1
}

# === Poll remote players ===
Write-Host "=== Polling remote players ==="
$foundRemotes = 0
for ($i = 0; $i -lt $PlaySeconds; $i++) {
    $s = [MpTool]::Pipe("mirroredge_module_control", "GET_STATUS")
    if ($s) {
        $remotes = 0; $map = "?"; $ig = "?"
        if ($s -match '"remotePlayers"\s*:\s*(\d+)') { $remotes = [int]$Matches[1] }
        if ($s -match '"currentMap"\s*:\s*"(\w*)"') { $map = $Matches[1] }
        if ($s -match '"inGameplay"\s*:\s*(\w+)') { $ig = $Matches[1] }
        Write-Host "  [${i}s] remotes=$remotes map='$map' gameplay=$ig"
        if ($remotes -ge $BotCount) {
            Write-Host "=== SUCCESS: $remotes remote players visible! ==="
            $foundRemotes = $remotes
            break
        }
    }
    Start-Sleep 1
}

# === Final ===
Write-Host "=== FINAL STATUS ==="
$s = [MpTool]::Pipe("mirroredge_module_control", "GET_STATUS")
Write-Host $s
Write-Host "=== Test complete. Game left running ==="
