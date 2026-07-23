# Run AFTER you manually enter a level in Mirror's Edge
# This auto-injects multiplayer and spawns bots
param([string]$Level = "")

$ErrorActionPreference = "Continue"
$Repo = Split-Path $PSScriptRoot -Parent
$GameRoot = $env:ME_DEPLOY_PATH
if (-not $GameRoot) {
  $cfg = Join-Path (Split-Path $PSScriptRoot -Parent) "deploy.config.json"
  if (Test-Path $cfg) {
    $GameRoot = (Get-Content $cfg -Raw | ConvertFrom-Json).deployPath
  }
}
if (-not $GameRoot) { throw "Set ME_DEPLOY_PATH or deploy.config.json deployPath" }

# Pipe helper
Add-Type @"
using System; using System.IO; using System.IO.Pipes;
public class PM {
    public static string Send(string pipe, string cmd) {
        try {
            var p = new NamedPipeClientStream(".", pipe, PipeDirection.InOut);
            p.Connect(3000);
            var w = new StreamWriter(p); w.AutoFlush = true;
            var r = new StreamReader(p);
            w.WriteLine(cmd);
            System.Threading.Thread.Sleep(400);
            string s = r.ReadLine();
            p.Close();
            return s;
        } catch (Exception e) { return "ERR:" + e.Message; }
    }
}
"@

# Check core status to detect current map
Write-Host "Checking game status..."
$status = [PM]::Send("mirroredge_module_control", "GET_STATUS")
Write-Host $status.Substring(0, [Math]::Min(400, $status.Length))

# Determine level
if (-not $Level) {
    if ($status -match '"currentMap"\s*:\s*"(\w*)"' -and $Matches[1] -and $Matches[1] -ne "tdmainmenu") {
        $Level = $Matches[1]
    } else {
        $Level = "tutorial_p"
        Write-Host "WARNING: Could not detect level. Using default: $Level"
    }
}
Write-Host "`nLevel: $Level"

# Start server if not running
$srv = Get-Process "multiplayer-server" -ErrorAction SilentlyContinue
if (-not $srv) {
    Write-Host "Starting multiplayer server..."
    Start-Process "$GameRoot\multiplayer-server.exe" -WorkingDirectory $GameRoot -WindowStyle Minimized
    Start-Sleep 2
} else {
    Write-Host "Server already running (PID $($srv.Id))"
}

# Ensure hooks
Write-Host "`nEnsuring gameplay hooks..."
[PM]::Send("mirroredge_module_control", "ENSURE_GAMEPLAY_HOOKS") | Out-Null
[PM]::Send("mirroredge_module_control", "ENSURE_MP_HOOKS") | Out-Null

# Inject multiplayer
Write-Host "Injecting multiplayer..."
$result = [PM]::Send("mirroredge_module_manager_control", "INJECT multiplayer")
Write-Host "  $result"
Start-Sleep 5

# Configure
Write-Host "Configuring..."
[PM]::Send("mirroredge_module_control", "SET multiplayer.server 127.0.0.1") | Out-Null
[PM]::Send("mirroredge_module_control", "SET multiplayer.room playthrough-lobby") | Out-Null
[PM]::Send("mirroredge_module_control", "SET multiplayer.name Player") | Out-Null
[PM]::Send("mirroredge_module_control", "RELOAD_SETTINGS") | Out-Null
Start-Sleep 3

# Wait for connection
Write-Host "Waiting for server connection..."
$connected = $false
for ($i = 0; $i -lt 15; $i++) {
    $s = [PM]::Send("mirroredge_module_control", "GET_STATUS")
    if ($s -match '"connected"\s*:\s*true') {
        Write-Host "  Connected (${i}s)"
        $connected = $true
        break
    }
    Start-Sleep 1
}
if (-not $connected) {
    Write-Host "  WARNING: Could not confirm connection."
}

# Start bots
Write-Host "`nStarting bots..."
$botScript = Join-Path $Repo "tools\debug-harness\tools\bot.ps1"
$baseArgs = "-NoProfile -File `"$botScript`" -Server 127.0.0.1 -Port 5222 -Room playthrough-lobby -Level `"$Level`" -RunSeconds 120"

Start-Process powershell -ArgumentList "$baseArgs -Name Bot-Kate -Character 1" -WindowStyle Minimized
Write-Host "  Bot-Kate (character 1)"
Start-Sleep 1

Start-Process powershell -ArgumentList "$baseArgs -Name Bot-Miller -Character 5" -WindowStyle Minimized
Write-Host "  Bot-Miller (character 5)"
Start-Sleep 3

# Watch for remote players and spawns
Write-Host "`nWatching for spawned players (up to 30s)..."
$got = 0
for ($i = 0; $i -lt 30; $i++) {
    $s = [PM]::Send("mirroredge_module_control", "GET_STATUS")
    
    $rm = 0
    $sp = 0
    $cm = ""
    if ($s -match '"remotePlayers"\s*:\s*(\d+)') { $rm = [int]$Matches[1] }
    if ($s -match '"spawnedPlayers"\s*:\s*(\d+)') { $sp = [int]$Matches[1] }
    if ($s -match '"currentMap"\s*:\s*"(\w*)"') { $cm = $Matches[1] }
    
    Write-Host "  [${i}s] remotes=$rm spawned=$sp map='$cm'"
    if ($rm -ge 2) { $got = $rm; break }
    Start-Sleep 1
}

Write-Host "`n=============================="
if ($got -ge 2) {
    Write-Host "PASS: $got remote players detected."
    Write-Host "Bot character models should now be visible in-game."
    Write-Host "They should move and animate around the level."
    Write-Host "(Press Insert to open the overlay for status details)"
} else {
    Write-Host "STATUS: $got remote players (expected 2)."
    Write-Host "If spawns = 0, try pressing Insert in game to open overlay,"
    Write-Host "go to the Multiplayer tab, and verify connection/settings."
}
Write-Host "=============================="
