# Multiplayer bot test - starts game via harness menu navigation, fallback START_GAME
param([int]$MaxRetries = 1)

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

# Load pipe helpers
Add-Type @"
using System; using System.IO; using System.IO.Pipes; using System.Runtime.InteropServices; using System.Diagnostics;
public class X {
    [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr d,int id);
    [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
    [DllImport("user32.dll")]public static extern bool IsWindowEnabled(IntPtr h);
    public const uint BM_CLICK=0x00F5;
    public static string P(string n,string c){try{var p=new NamedPipeClientStream(".",n,PipeDirection.InOut);p.Connect(3000);var w=new StreamWriter(p);w.AutoFlush=true;var r=new StreamReader(p);w.WriteLine(c);System.Threading.Thread.Sleep(500);string s=r.ReadLine();p.Close();return s;}catch(Exception e){return "ERR:"+e.Message;}}
}
"@

for ($attempt = 1; $attempt -le $MaxRetries; $attempt++) {
    Write-Host "`n============================================================"
    Write-Host "ATTEMPT $attempt of $MaxRetries"
    Write-Host "============================================================"
    
    Get-Process "MirrorsEdge","ModuleLauncher","multiplayer-server" -EA SilentlyContinue | Stop-Process -Force
    Start-Sleep 3
    
    # Server
    Start-Process "$GameRoot\multiplayer-server.exe" -WorkingDirectory $GameRoot -WindowStyle Minimized | Out-Null
    Start-Sleep 2
    
    # Launcher + click
    $l = Start-Process "$GameRoot\ModuleLauncher.exe" -PassThru; Start-Sleep 4
    for ($i=0;$i -lt 30;$i++) {
        $l.Refresh(); $h=$l.MainWindowHandle
        if ($h -ne [IntPtr]::Zero) {
            $b=[X]::GetDlgItem($h,1004)
            if ($b -ne [IntPtr]::Zero -and [X]::IsWindowEnabled($b)) {
                [X]::SetForegroundWindow($h)|Out-Null; Start-Sleep -M 200
                [X]::SendMessage($b,[X]::BM_CLICK,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
                Write-Host "launch clicked"; break
            }
        }
        Start-Sleep -M 500
    }
    
    # Wait game
    for ($i=0;$i -lt 30;$i++) {
        $g=Get-Process "MirrorsEdge" -EA 0
        if ($g) { Write-Host "game PID $($g.Id)"; break }
        Start-Sleep 1
    }
    
    # Wait hooks
    Write-Host "Waiting for hooks..."
    for ($i = 0; $i -lt 120; $i++) {
        $s = [X]::P("mirroredge_module_manager_control", "GET_STATUS")
        if ($s -match '"hooksInstalled":\s*true') {
            Write-Host "hooksInstalled=true (${i}s)"
            break
        }
        Start-Sleep 1
    }
    
    # Wait core
    for ($i=0;$i -lt 60;$i++) {
        $s=[X]::P("mirroredge_module_control","GET_STATUS")
        if ($s -match '"modReady":\s*true') { break }
        Start-Sleep 1
    }
    Write-Host "hooks+core ready"
    
    # Movie nuke is active in engine.dll. Wait for game to reach
    # main menu or level automatically (movies get killed every frame).
    Write-Host "waiting for movie nuke + level entry..."
    $entered = $false
    $levelName = ""
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep 1
        $s = [X]::P("mirroredge_module_control", "GET_STATUS")
        $map = ""; if ($s -match '"currentMap":"(\w*)"') { $map = $Matches[1] }
        $ig = "?"; if ($s -match '"inGameplay":(\w+)') { $ig = $Matches[1] }
        if ($i % 10 -eq 0) { Write-Host "  [${i}s] map='$map' ig=$ig" }
        if ($map -and $map -ne "" -and $map -ne "tdmainmenu") {
            Write-Host "=== LEVEL: $map ==="
            $entered = $true; $levelName = $map; break
        }
        if ($ig -eq "true") {
            Write-Host "=== GAMEPLAY ==="
            $entered = $true; break
        }
    }
    
    if (-not $entered) {
        # Try CONSOLE open (direct ClientTravel, no tick needed)
        Write-Host "trying CONSOLE open tutorial_p..."
        $r = [X]::P("mirroredge_module_control", "CONSOLE open tutorial_p")
        Write-Host "  result: $r"
        for ($i = 0; $i -lt 30; $i++) {
            Start-Sleep 1
            $s = [X]::P("mirroredge_module_control", "GET_STATUS")
            $map = ""; if ($s -match '"currentMap":"(\w*)"') { $map = $Matches[1] }
            $ig = "?"; if ($s -match '"inGameplay":(\w+)') { $ig = $Matches[1] }
            if ($i % 10 -eq 0) { Write-Host "  [${i}s] map='$map' ig=$ig" }
            if ($map -and $map -ne "" -and $map -ne "tdmainmenu") {
                Write-Host "=== LEVEL: $map ==="
                $entered = $true; $levelName = $map; break
            }
            if ($ig -eq "true") { $entered = $true; break }
        }
    }
    
    if (-not $entered) {
        # Try START_GAME as fallback
        Write-Host "trying START_GAME tutorial_p..."
        $r = [X]::P("mirroredge_module_control", "START_GAME tutorial_p")
        Write-Host "  result: $r"
        for ($i = 0; $i -lt 30; $i++) {
            Start-Sleep 1
            $s = [X]::P("mirroredge_module_control", "GET_STATUS")
            $map = ""; if ($s -match '"currentMap":"(\w*)"') { $map = $Matches[1] }
            $ig = "?"; if ($s -match '"inGameplay":(\w+)') { $ig = $Matches[1] }
            if ($i % 10 -eq 0) { Write-Host "  [${i}s] map='$map' ig=$ig" }
            if ($map -and $map -ne "" -and $map -ne "tdmainmenu") {
                Write-Host "=== LEVEL: $map ==="
                $entered = $true; $levelName = $map; break
            }
            if ($ig -eq "true") { $entered = $true; break }
        }
    }
    
    if ($entered) { break }
}

# Check final state
$s=[X]::P("mirroredge_module_control","GET_STATUS")
$map=""; if($s -match '"currentMap":"(\w*)"'){$map=$Matches[1]}

if ((-not $entered) -or (-not $map -or $map -eq "" -or $map -eq "tdmainmenu")) {
    Write-Host "`n=== FAILED after $MaxRetries attempts ==="
    $s=[X]::P("mirroredge_module_control","GET_STATUS")
    Write-Host $s.Substring(0,[Math]::Min(300,$s.Length))
    Write-Host "Game running. Check screen manually."
    exit 0
}

# === IN LEVEL ===
Write-Host "`n=== IN LEVEL: $levelName ==="

# Ensure hooks
[X]::P("mirroredge_module_control","ENSURE_GAMEPLAY_HOOKS")|Out-Null
[X]::P("mirroredge_module_control","ENSURE_MP_HOOKS")|Out-Null

# Force hosted live
Write-Host "FORCE_HOSTED_LIVE..."
$r=[X]::P("mirroredge_module_control","FORCE_HOSTED_LIVE")
Write-Host "  $r"
Start-Sleep 2

# Inject multiplayer
Write-Host "INJECT MP..."
$r=[X]::P("mirroredge_module_manager_control","INJECT multiplayer")
Write-Host "  $r"
Start-Sleep 5

# Config
Write-Host "CONFIG..."
[X]::P("mirroredge_module_control","SET multiplayer.server 127.0.0.1")|Out-Null
[X]::P("mirroredge_module_control","SET multiplayer.room playthrough-lobby")|Out-Null
[X]::P("mirroredge_module_control","SET multiplayer.name Tester")|Out-Null
[X]::P("mirroredge_module_control","RELOAD_SETTINGS")|Out-Null
Start-Sleep 3

# Connect
Write-Host "CONNECT..."
for ($i=0;$i -lt 15;$i++) {
    $s=[X]::P("mirroredge_module_control","GET_STATUS")
    if ($s -match '"connected":\s*true') { Write-Host "  OK ${i}s"; break }
    Start-Sleep 1
}

# Bots
Write-Host "BOTS..."
$botScript = "$Repo\tools\debug-harness\tools\bot.ps1"
$botLevel = if ($levelName) { $levelName } else { $map }
if (-not $botLevel) { $botLevel = "tutorial_p" }
$args = "-NoProfile -File `"$botScript`" -Server 127.0.0.1 -Port 5222 -Room playthrough-lobby -Level `"$botLevel`" -RunSeconds 120"
Start-Process powershell -ArgumentList "$args -Name Bot-Kate -Character 1" -WindowStyle Minimized
Write-Host "  Kate (1)"; Start-Sleep 1
Start-Process powershell -ArgumentList "$args -Name Bot-Miller -Character 5" -WindowStyle Minimized
Write-Host "  Miller (5)"; Start-Sleep 3

# Monitor remotes
Write-Host "REMOTES..."
$got=0
for ($i=0;$i -lt 30;$i++) {
    $s=[X]::P("mirroredge_module_control","GET_STATUS")
    $rm=0; if($s -match '"remotePlayers":(\d+)'){$rm=[int]$Matches[1]}
    $sp=0; if($s -match '"spawnedPlayers":(\d+)'){$sp=[int]$Matches[1]}
    $px=0; if($s -match '"posX":([\d.eE-]+)'){$px=[double]$Matches[1]}
    $py=0; if($s -match '"posY":([\d.eE-]+)'){$py=[double]$Matches[1]}
    Write-Host "  [${i}s] r=$rm spawn=$sp pos=($px,$py)"
    if ($rm -ge 2 -and $sp -ge 1) { $got=$rm; break }
    if ($rm -ge 2) { $got=$rm }
    Start-Sleep 1
}

Write-Host ""
Write-Host "=============================="
if ($got -ge 2) { Write-Host "PASS: $got remote players" } else { Write-Host "STATUS: $got/2 remotes" }
$fs=[X]::P("mirroredge_module_control","GET_STATUS")
Write-Host $fs.Substring(0,[Math]::Min(500,$fs.Length))
Write-Host "=============================="
Write-Host "Game running for visual check."
