# Direct multiplayer bot visibility test
# Launches game, injects modules, loads level, starts bots, checks status

param(
    [int]$BotCount = 2,
    [int]$PlaySeconds = 30,
    [string]$Level = "tutorial_p"
)

$ErrorActionPreference = "Stop"
$GameRoot = $env:ME_DEPLOY_PATH
if (-not $GameRoot) {
  $cfg = Join-Path (Split-Path $PSScriptRoot -Parent) "deploy.config.json"
  if (Test-Path $cfg) {
    $GameRoot = (Get-Content $cfg -Raw | ConvertFrom-Json).deployPath
  }
}
if (-not $GameRoot) { throw "Set ME_DEPLOY_PATH or deploy.config.json deployPath" }
$ServerExe = "$GameRoot\multiplayer-server.exe"
$LauncherExe = "$GameRoot\ModuleLauncher.exe"
$BotScript = "$PSScriptRoot\debug-harness\tools\bot.ps1"

# Clean up
function Cleanup {
    Write-Host "Cleaning up..."
    Get-Process -Name "MirrorsEdge" -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-Process -Name "ModuleLauncher" -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-Process -Name "multiplayer-server" -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep 2
}

# Named pipe helpers
function Send-PipeCommand {
    param($PipeName, $Command, [int]$TimeoutMs = 5000)
    try {
        $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)
        $pipe.Connect($TimeoutMs)
        $writer = New-Object System.IO.StreamWriter($pipe)
        $reader = New-Object System.IO.StreamReader($pipe)
        $writer.WriteLine($Command)
        $writer.Flush()
        Start-Sleep -Milliseconds 500
        $result = $reader.ReadLine()
        $pipe.Close()
        return $result
    } catch {
        return $null
    }
}

function Get-ManagerStatus {
    $raw = Send-PipeCommand -PipeName "mirroredge_module_manager_control" -Command "GET_STATUS"
    if ($raw -match '\{.*\}') {
        try { return $raw -replace '^[^{]*', '' | ConvertFrom-Json } catch { return $null }
    }
    return $null
}

function Get-CoreStatus {
    $raw = Send-PipeCommand -PipeName "mirroredge_module_control" -Command "GET_STATUS"
    if ($raw -match '\{.*\}') {
        try { return $raw -replace '^[^{]*', '' | ConvertFrom-Json } catch { return $null }
    }
    return $null
}

function Wait-For {
    param($Condition, [int]$TimeoutMs = 60000, [int]$PollMs = 2000)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        $result = & $Condition
        if ($result) { return $result }
        Start-Sleep -Milliseconds $PollMs
    }
    return $null
}

# === MAIN ===
Cleanup

# 1. Start server
Write-Host "=== Starting multiplayer server ==="
$server = Start-Process -FilePath $ServerExe -WorkingDirectory $GameRoot -PassThru -WindowStyle Minimized
Start-Sleep 2
$portOk = Test-NetConnection -ComputerName 127.0.0.1 -Port 5222 -InformationLevel Quiet -WarningAction SilentlyContinue
if (-not $portOk) {
    Write-Host "ERROR: Server failed to start on port 5222"
    Cleanup
    exit 1
}
Write-Host "Server running (PID $($server.Id))"

# 2. Launch game via launcher
Write-Host "=== Launching game ==="
$sessionId = "mp-direct-" + (Get-Date -Format "yyyyMMdd-HHmmss")
$env:MMOD_DEBUG_SESSION = $sessionId
$env:MMOD_DEBUG_LOG = "$env:TEMP\mirroredge-debug\$sessionId.ndjson"

$launcher = Start-Process -FilePath $LauncherExe -PassThru
Write-Host "Launcher PID: $($launcher.Id)"
Start-Sleep 3

# Click Launch Game button via Win32
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Clicker {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern IntPtr GetDesktopWindow();
    [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr c, string cls, string win);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder t, int m);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
    public const uint D = 0x0002, U = 0x0004;
    public static void Click(int x, int y) { SetCursorPos(x,y); mouse_event(D,0,0,0,IntPtr.Zero); System.Threading.Thread.Sleep(50); mouse_event(U,0,0,0,IntPtr.Zero); }
    public static IntPtr FindWindowSlow() {
        foreach (var title in new[]{"Module Launcher", "Mirror's Edge Module Launcher", "Mirroredge Module Launcher"}) {
            var h = FindWindow(null, title);
            if (h != IntPtr.Zero) return h;
        }
        var desktop = GetDesktopWindow();
        var child = FindWindowEx(desktop, IntPtr.Zero, null, null);
        while (child != IntPtr.Zero) {
            var sb = new StringBuilder(256);
            GetWindowText(child, sb, 256);
            var t = sb.ToString();
            if (t.Contains("Module") && t.Contains("Launch")) return child;
            child = FindWindowEx(desktop, child, null, null);
        }
        return IntPtr.Zero;
    }
}
"@

$launcherHwnd = [Clicker]::FindWindowSlow()
if ($launcherHwnd -eq [IntPtr]::Zero) {
    Write-Host "ERROR: Could not find launcher window"
    Cleanup
    exit 1
}

$rect = New-Object Clicker+RECT
[Clicker]::GetWindowRect($launcherHwnd, [ref]$rect)
$cx = $rect.L + ($rect.R - $rect.L) / 2
$cy = $rect.T + ($rect.B - $rect.T) * 0.5
Write-Host "Clicking Launch Game at ($cx, $cy)..."
[Clicker]::SetForegroundWindow($launcherHwnd)
Start-Sleep -Milliseconds 500
[Clicker]::Click($cx, $cy)
Write-Host "Launch Game clicked"

# 3. Wait for module_manager hooks
Write-Host "=== Waiting for module_manager hooks ==="
$hooksReady = $false
for ($i = 0; $i -lt 60; $i++) {
    $status = Get-ManagerStatus
    if ($status -and $status.hooksInstalled -eq $true -and $status.overlayReady -eq $true) {
        Write-Host "Hooks ready after $($i*2)s"
        $hooksReady = $true
        break
    }
    if ($i % 15 -eq 14) { Write-Host "Still waiting for hooks... ($($i*2)s)" }
    Start-Sleep 2
}
if (-not $hooksReady) { Write-Host "ERROR: Hooks timeout"; Cleanup; exit 1 }

# 4. Wait for core auto-load
Write-Host "=== Waiting for core auto-load ==="
$coreReady = $false
for ($i = 0; $i -lt 90; $i++) {
    $coreStatus = Get-CoreStatus
    if ($coreStatus -and $coreStatus.engine -and $coreStatus.engine.modReady -eq $true) {
        Write-Host "Core ready after $($i*2)s"
        $coreReady = $true
        break
    }
    if ($i % 10 -eq 9) { Write-Host "Still waiting for core... ($($i*2)s)" }
    Start-Sleep 2
}
if (-not $coreReady) { Write-Host "WARNING: Core not ready, continuing anyway..." }

# 5. Load a level via console
Write-Host "=== Loading level: $Level ==="
# Try both console open and console_exec
$result = Send-PipeCommand -PipeName "mirroredge_module_manager_control" -Command "CONSOLE_EXEC open $Level"
Write-Host "CONSOLE_EXEC result: $result"
Start-Sleep 5

# Wait for level to load - poll for map changes
Write-Host "Waiting for level load..."
$levelLoaded = $false
for ($i = 0; $i -lt 60; $i++) {
    $s = Get-CoreStatus
    if ($s) {
        Write-Host "  [${i}s] map=$($s.currentMap) inGameplay=$($s.inGameplay) gameplayHooks=$($s.engine.gameplayHooks)"
        if ($s.currentMap -and $s.currentMap -ne "" -and $s.currentMap -ne "tdmainmenu") {
            Write-Host "Level loaded: $($s.currentMap)"
            $levelLoaded = $true
            break
        }
    }
    Start-Sleep 2
}
if (-not $levelLoaded) { Write-Host "WARNING: Could not confirm level load, continuing anyway..." }

# 6. Inject multiplayer
Write-Host "=== Injecting multiplayer ==="
$result = Send-PipeCommand -PipeName "mirroredge_module_manager_control" -Command "INJECT multiplayer"
Write-Host "INJECT multiplayer result: $result"
Start-Sleep 5

# 7. Configure and connect
Write-Host "=== Configuring multiplayer client ==="
Send-PipeCommand -PipeName "mirroredge_module_control" -Command "SET multiplayer.server 127.0.0.1" | Out-Null
Send-PipeCommand -PipeName "mirroredge_module_control" -Command "SET multiplayer.room playthrough-lobby" | Out-Null
Send-PipeCommand -PipeName "mirroredge_module_control" -Command "SET multiplayer.name TestHost" | Out-Null
Send-PipeCommand -PipeName "mirroredge_module_control" -Command "RELOAD_SETTINGS" | Out-Null
Start-Sleep 3

# Enable gameplay hooks  
Send-PipeCommand -PipeName "mirroredge_module_control" -Command "ENSURE_GAMEPLAY_HOOKS" | Out-Null
Send-PipeCommand -PipeName "mirroredge_module_control" -Command "ENSURE_MP_HOOKS" | Out-Null

# Check connection status
Write-Host "=== Checking multiplayer status ==="
for ($i = 0; $i -lt 30; $i++) {
    $s = Get-CoreStatus
    if ($s) {
        Write-Host "  [${i}s] connected=$($s.multiplayer.connected) remotes=$($s.multiplayer.remotePlayers) pos=($($s.multiplayer.posX),$($s.multiplayer.posY),$($s.multiplayer.posZ))"
        if ($s.multiplayer.connected -eq $true) {
            Write-Host "Connected to server!"
            break
        }
    }
    Start-Sleep 2
}

# 8. Start bots
Write-Host "=== Starting $BotCount bots ==="
$bots = @()
$characters = @(1, 5)  # Kate, Miller
for ($i = 0; $i -lt [Math]::Min($BotCount, $characters.Count); $i++) {
    $botName = "Bot-$($characters[$i])"
    $bot = Start-Process powershell -ArgumentList "-NoProfile -File `"$BotScript`" -Server 127.0.0.1 -Port 5222 -Name `"$botName`" -Room playthrough-lobby -Character $($characters[$i]) -Level `"$Level`" -RunSeconds $($PlaySeconds + 60)" -PassThru -WindowStyle Minimized
    $bots += $bot
    Write-Host "  Started $botName (PID $($bot.Id), char=$($characters[$i]))"
    Start-Sleep 1
}

# 9. Poll for remote players
Write-Host "=== Polling for remote players ($PlaySeconds s) ==="
for ($i = 0; $i -lt $PlaySeconds; $i++) {
    $s = Get-CoreStatus
    if ($s) {
        $mp = $s.multiplayer
        $pos = if ($mp.posX) { "pos=($([math]::Round($mp.posX,1)),$([math]::Round($mp.posY,1)),$([math]::Round($mp.posZ,1)))" } else { "pos=null" }
        Write-Host "  [${i}s] connected=$($mp.connected) remotes=$($mp.remotePlayers) map=$($s.currentMap) inGameplay=$($s.inGameplay) $pos"
        if ($mp.remotePlayers -ge $BotCount) {
            Write-Host "=== SUCCESS: All $BotCount remote players visible! ==="
            break
        }
    }
    Start-Sleep 1
}

# 10. Final status
Write-Host "=== FINAL STATUS ==="
$fs = Get-CoreStatus
if ($fs) {
    Write-Host "Connected: $($fs.multiplayer.connected)"
    Write-Host "Remote players: $($fs.multiplayer.remotePlayers)"
    Write-Host "Map: $($fs.currentMap)"
    Write-Host "InGameplay: $($fs.inGameplay)"
    Write-Host "GameplayHooks: $($fs.engine.gameplayHooks)"
}

Write-Host "=== Test complete ==="
Write-Host "Leaving game running for inspection. Press Ctrl+C to clean up."
try { Start-Sleep -Seconds 300 } catch {}
Cleanup
