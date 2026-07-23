#Requires -Version 5.1
<#
.SYNOPSIS
  Real-level MP bot visibility test (Steam path).

Verified entry path (do NOT use CONSOLE open / START_GAME):
  INJECT_KEY 0x0D -> START_NEW_GAME -> wait tutorial_p -> ENSURE_GAMEPLAY_HOOKS
  -> inject multiplayer -> FORCE_HOSTED_LIVE -> Follow bots near host

Success criteria:
  1) currentMap=tutorial_p
  2) client.log has spawn ok + remote pose applied
  3) GET_STATUS multiplayer.posZ != 0 (host pawn seed)
  4) spawnedPlayers/posedPlayers >= BotCount OR client.log proves both
  5) screenshot under %TEMP%\mirroredge-debug\real-level-bots\
  6) soft collision: SoftProbe bot overlaps host; client.log has
     "soft collision engaged" (B2 fake push; KI-2026-012-safe path)
  7) Tag/Interact (B0/B1): SoftProbe -StartTag -SendInteract; client.log has
     "tag mode live" / "tagged id=" and "interact recv" (or interact sent)
  8) Physics world clamp: SoftProbe -PhysicsFallDrop [-PhysicsWallSlam];
     client.log "world clamp floor" (required) / "world clamp wall" (if slam)
#>
param(
    [int]$BotCount = 2,
    [int]$PlaySeconds = 90,
    [string]$Level = "tutorial_p",
    [string]$GameRoot = "C:\Program Files (x86)\Steam\steamapps\common\mirrors edge",
    [string]$Room = "playthrough-lobby",
    # After visual bots spawn/pose, start an overlap probe (FollowDistance << radius).
    # Also drives Tag + Interact gates unless skipped.
    [switch]$SkipSoftCollisionProbe,
    # Skip SoftProbe PhysicsFallDrop/WallSlam + world clamp floor/wall gates.
    [switch]$SkipPhysicsProbe
)

$ErrorActionPreference = "Continue"
$Repo = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path (Join-Path $Repo "tools\debug-harness\tools\bot.ps1"))) {
    $Repo = $PSScriptRoot
    if (-not (Test-Path (Join-Path $Repo "tools\debug-harness\tools\bot.ps1"))) {
        $Repo = Split-Path $PSScriptRoot -Parent
    }
}
$BotScript = Join-Path $Repo "tools\debug-harness\tools\bot.ps1"
$ReflectDir = Join-Path $env:TEMP "mirroredge-reflections\real-level-bots-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
$ShotDir = Join-Path $env:TEMP "mirroredge-debug\real-level-bots"
$TargetFile = Join-Path $env:TEMP "mirroredge-bot-target.json"
$ClientLog = Join-Path $env:TEMP "mirroredge-multiplayer-client.log"
New-Item -ItemType Directory -Force -Path $ReflectDir, $ShotDir | Out-Null

function Get-BoneCycleFrameCount {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return 0 }
    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -lt 10) { return 0 }
        if ([Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'MEBC') { return 0 }
        return [int][BitConverter]::ToUInt16($bytes, 8)
    } catch {
        return 0
    }
}

$script:Evidence = [ordered]@{
    startedAt = (Get-Date).ToString("o")
    gameRoot = $GameRoot
    steps = New-Object System.Collections.Generic.List[string]
    errors = New-Object System.Collections.Generic.List[string]
    finalStatus = ""
    clientLogTail = ""
    pass = $false
}

function Note([string]$msg) {
    Write-Host $msg
    $script:Evidence.steps.Add("$((Get-Date).ToString('HH:mm:ss')) $msg") | Out-Null
}

function FailNote([string]$msg) {
    Write-Host "FAIL: $msg" -ForegroundColor Red
    $script:Evidence.errors.Add($msg) | Out-Null
}

if (-not ("MpPipe" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
public static class MpPipe {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr d, int id);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int nIndex);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hWnd, EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public const uint BM_CLICK = 0x00F5;
    public const uint WM_KEYDOWN = 0x0100;
    public const uint WM_KEYUP = 0x0101;
    public const int SM_REMOTESESSION = 0x1000;
    // IMPORTANT: do not use StreamWriter(UTF8) - it emits a BOM so GET_STATUS
    // becomes EF BB BF G E T... and the server returns ERR unknown.
    public static string Q(string pipe, string cmd, int timeoutMs = 8000) {
        try {
            var p = new NamedPipeClientStream(".", pipe, PipeDirection.InOut);
            p.Connect(Math.Min(timeoutMs, 5000));
            try { p.ReadTimeout = timeoutMs; } catch {}
            var payload = Encoding.ASCII.GetBytes(cmd + "\n");
            p.Write(payload, 0, payload.Length);
            p.Flush();
            var sb = new StringBuilder();
            var buf = new byte[8192];
            var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (DateTime.UtcNow < deadline) {
                int n = 0;
                try { n = p.Read(buf, 0, buf.Length); }
                catch (TimeoutException) { break; }
                catch { if (sb.Length > 0) break; System.Threading.Thread.Sleep(5); continue; }
                if (n <= 0) { if (sb.Length > 0) break; System.Threading.Thread.Sleep(5); continue; }
                sb.Append(Encoding.ASCII.GetString(buf, 0, n));
                if (sb.ToString().IndexOf('\n') >= 0) break;
            }
            p.Close();
            return sb.ToString().Replace("\r", "").Trim();
        } catch (Exception ex) { return "ERR:" + ex.Message; }
    }
}
"@
}

function Test-RemoteDesktopSession {
    try { return ([MpPipe]::GetSystemMetrics([MpPipe]::SM_REMOTESESSION) -ne 0) }
    catch { return $false }
}

# KI-2026-011: ME refuses CreateDevice under RDP — MessageBox title "Message".
function Test-MeRemoteDesktopBlockDialog {
    $me = Get-LiveMirrorsEdgeProcess
    if (-not $me) { return $false }
    try {
        if ($me.MainWindowTitle -eq 'Message') { return $true }
    } catch {}
    return $false
}

if (-not ("MeHung" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class MeHung {
    [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr hWnd);
}
"@
}

function Test-GameHung {
    $p = Get-LiveMirrorsEdgeProcess
    if (-not $p -or $p.MainWindowHandle -eq [IntPtr]::Zero) { return $false }
    return [MeHung]::IsHungAppWindow($p.MainWindowHandle)
}

# Pitch camera toward rooftop stand-offs before live-mesh shot (host often
# leaves looking at skyline so remotes sit below FOV).
if (-not ("MeMouseLook" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class MeMouseLook {
  [StructLayout(LayoutKind.Sequential)] struct INPUT {
    public uint type; public MOUSEINPUT mi;
  }
  [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT {
    public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo;
  }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int cb);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  const uint INPUT_MOUSE = 0, MOUSEEVENTF_MOVE = 0x0001;
  public static void Rel(int dx, int dy) {
    INPUT i = new INPUT(); i.type = INPUT_MOUSE;
    i.mi.dx = dx; i.mi.dy = dy; i.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, new INPUT[] { i }, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
}

function Look-TowardMeshStandOff {
    $p = Get-LiveMirrorsEdgeProcess
    if (-not $p -or $p.MainWindowHandle -eq [IntPtr]::Zero) { return }
    try { [MeMouseLook]::SetForegroundWindow($p.MainWindowHandle) | Out-Null } catch {}
    Start-Sleep -Milliseconds 200
    # SoftProbe settle often leaves host looking at skyline. Mild pitch only —
    # heavy look-down hit feet (V6) or empty mid-roof with remotes off-axis.
    for ($k = 0; $k -lt 2; $k++) {
        [MeMouseLook]::Rel(0, 36)
        Start-Sleep -Milliseconds 40
    }
    Start-Sleep -Milliseconds 250
}

# Zombie EPROCESS (0 threads) survives Stop-Process and blocks Steam relaunch + DLL replace.
function Get-LiveMirrorsEdgeProcess {
    Get-Process MirrorsEdge -EA SilentlyContinue | Where-Object {
        try { $_.Threads.Count -gt 0 } catch { $false }
    } | Select-Object -First 1
}

function Stop-HarnessBots {
    param($Procs)
    if (-not $Procs) { return }
    foreach ($bp in $Procs) {
        if ($bp -and -not $bp.HasExited) {
            try { Stop-Process -Id $bp.Id -Force -EA SilentlyContinue } catch {}
        }
    }
    # Child powershell bot.ps1 trees
    Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" -EA SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine -match 'bot\.ps1' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -EA SilentlyContinue }
}

function CoreQ([string]$cmd) { [MpPipe]::Q("mirroredge_module_control", $cmd) }
function MgrQ([string]$cmd) { [MpPipe]::Q("mirroredge_module_manager_control", $cmd) }

function Parse-Status([string]$s) {
    $o = [ordered]@{
        raw = $s; map = ""; ig = "?"; connected = $false
        remotes = 0; spawned = 0; posed = 0
        posX = 0.0; posY = 0.0; posZ = 0.0; yaw = 0
        gameplayHooks = $false; modReady = $false; gameHwnd = [int64]0
    }
    if (-not $s) { return [pscustomobject]$o }
    if ($s -match '"currentMap"\s*:\s*"([^"]*)"') { $o.map = $Matches[1] }
    if ($s -match '"inGameplay"\s*:\s*(true|false)') { $o.ig = $Matches[1] }
    if ($s -match '"modReady"\s*:\s*true' -or $s -match '"component"\s*:\s*"core"') { $o.modReady = $true }
    if ($s -match '"gameplayHooks"\s*:\s*true') { $o.gameplayHooks = $true }
    if ($s -match '"connected"\s*:\s*true') { $o.connected = $true }
    if ($s -match '"remotePlayers"\s*:\s*(\d+)') { $o.remotes = [int]$Matches[1] }
    if ($s -match '"spawnedPlayers"\s*:\s*(\d+)') { $o.spawned = [int]$Matches[1] }
    if ($s -match '"posedPlayers"\s*:\s*(\d+)') { $o.posed = [int]$Matches[1] }
    if ($s -match '"posX"\s*:\s*(-?[0-9.]+)') { $o.posX = [double]$Matches[1] }
    if ($s -match '"posY"\s*:\s*(-?[0-9.]+)') { $o.posY = [double]$Matches[1] }
    if ($s -match '"posZ"\s*:\s*(-?[0-9.]+)') { $o.posZ = [double]$Matches[1] }
    if ($s -match '"yaw"\s*:\s*(\d+)') { $o.yaw = [int]$Matches[1] }
    if ($s -match '"gameHwnd"\s*:\s*(\d+)') { $o.gameHwnd = [int64]$Matches[1] }
    return [pscustomobject]$o
}

function Write-TargetFromStatus($st) {
    if ([Math]::Abs($st.posX) -lt 1 -and [Math]::Abs($st.posY) -lt 1 -and [Math]::Abs($st.posZ) -lt 1) {
        return $false
    }
    if ([Math]::Abs($st.posX) + [Math]::Abs($st.posY) + [Math]::Abs($st.posZ) -lt 100.0) {
        return $false
    }
    $yaw = 0
    if ($null -ne $st.yaw) { $yaw = [int]$st.yaw }
    $payload = (@{ x = $st.posX; y = $st.posY; z = $st.posZ; yaw = $yaw } | ConvertTo-Json -Compress)
    # Bots hold TargetFile with FileShare.ReadWrite - Set-Content still fails
    # when another writer races; use WriteAllText.
    try {
        [System.IO.File]::WriteAllText($TargetFile, $payload)
        return $true
    } catch {
        return $false
    }
}

function Wait-HostPoseReady {
    param(
        [int]$MaxSeconds = 120,
        [string]$Label = "host pose",
        [switch]$SkipKeys
    )
    for ($i = 0; $i -lt $MaxSeconds; $i++) {
        $st = Parse-Status (CoreQ "GET_STATUS")
        if (Write-TargetFromStatus $st) {
            Note ("{0} ready at {1}s ({2:n0},{3:n0},{4:n0}) yaw={5}" -f `
                $Label, $i, $st.posX, $st.posY, $st.posZ, $st.yaw)
            return $st
        }
        if (-not $SkipKeys -and ($i -gt 0) -and ($i % 3 -eq 0)) {
            # Space dismisses tutorial prompts; Shift/W nudge Faith so pawn/camera
            # resolve sooner while TdEngine warm finishes (pre-live warm helps too).
            CoreQ "INJECT_KEY 0x20" | Out-Null
            Start-Sleep -Milliseconds 40
            CoreQ "INJECT_KEY 0x20 UP" | Out-Null
            if ($i -ge 8) {
                CoreQ "INJECT_KEY 0x10" | Out-Null
                Start-Sleep -Milliseconds 40
                CoreQ "INJECT_KEY 0x10 UP" | Out-Null
                CoreQ "INJECT_KEY 0x57" | Out-Null
                Start-Sleep -Milliseconds 50
                CoreQ "INJECT_KEY 0x57 UP" | Out-Null
            }
        }
        if ($i % 10 -eq 0) {
            Note ("Waiting {0} [{1}s] map={2} ig={3} pos=({4:n0},{5:n0},{6:n0})" -f `
                $Label, $i, $st.map, $st.ig, $st.posX, $st.posY, $st.posZ)
        }
        Start-Sleep 1
    }
    return $null
}

# Hold W+Shift so host Mesh3p fills Walking clip; optional Space jump for Falling.
function Drive-HostMeshClipCapture {
    param(
        [int]$WalkMs = 4000,
        [switch]$AlsoJump
    )
    Note ("Drive host Mesh3p clip capture W+Shift {0}ms jump={1}" -f $WalkMs, [bool]$AlsoJump)
    CoreQ "INJECT_KEY 0x10" | Out-Null
    CoreQ "INJECT_KEY 0x57" | Out-Null
    Start-Sleep -Milliseconds $WalkMs
    CoreQ "INJECT_KEY 0x57 UP" | Out-Null
    CoreQ "INJECT_KEY 0x10 UP" | Out-Null
    if ($AlsoJump) {
        Start-Sleep -Milliseconds 250
        CoreQ "INJECT_KEY 0x20" | Out-Null
        Start-Sleep -Milliseconds 140
        CoreQ "INJECT_KEY 0x20 UP" | Out-Null
        Start-Sleep -Milliseconds 900
    }
}

function Test-ScreenLit {
    param([int]$MinLuma = 8)
    $proc = Get-LiveMirrorsEdgeProcess
    if (-not $proc -or $proc.MainWindowHandle -eq [IntPtr]::Zero) { return $false }
    Add-Type -AssemblyName System.Drawing -EA SilentlyContinue
    $code = @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public static class Scr {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  public static int MaxLuma(IntPtr hwnd) {
    RECT r; if (!GetWindowRect(hwnd, out r)) return 0;
    int w = Math.Max(1, r.R - r.L), h = Math.Max(1, r.B - r.T);
    using (var bmp = new Bitmap(w, h))
    using (var g = Graphics.FromImage(bmp)) {
      g.CopyFromScreen(r.L, r.T, 0, 0, new Size(w, h));
      int max = 0;
      for (int i = 0; i < 25; i++) {
        int x = (i % 5) * (w / 5) + w / 10;
        int y = (i / 5) * (h / 5) + h / 10;
        Color c = bmp.GetPixel(Math.Min(w-1,x), Math.Min(h-1,y));
        int luma = (c.R + c.G + c.B) / 3;
        if (luma > max) max = luma;
      }
      return max;
    }
  }
}
"@
    if (-not ("Scr" -as [type])) { Add-Type -TypeDefinition $code -ReferencedAssemblies System.Drawing }
    try {
        $luma = [Scr]::MaxLuma($proc.MainWindowHandle)
        return ($luma -ge $MinLuma)
    } catch { return $false }
}

function Capture-GameShot([string]$tag) {
    $proc = Get-LiveMirrorsEdgeProcess
    if (-not $proc -or $proc.MainWindowHandle -eq [IntPtr]::Zero) { return $null }
    Add-Type -AssemblyName System.Drawing -EA SilentlyContinue
    if (-not ("Scr" -as [type])) { [void](Test-ScreenLit) }
    try {
        $hwnd = $proc.MainWindowHandle
        $path = Join-Path $ShotDir ("{0}-{1}.png" -f (Get-Date -Format "HHmmss"), $tag)
        # Prefer desktop copy (D3D content) over PrintWindow
        $codeSave = @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class ScrShot {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  public static string Save(IntPtr hwnd, string path) {
    RECT r; if (!GetWindowRect(hwnd, out r)) return null;
    int w = Math.Max(1, r.R - r.L), h = Math.Max(1, r.B - r.T);
    using (var bmp = new Bitmap(w, h))
    using (var g = Graphics.FromImage(bmp)) {
      g.CopyFromScreen(r.L, r.T, 0, 0, new Size(w, h));
      bmp.Save(path, ImageFormat.Png);
    }
    return path;
  }
}
"@
        if (-not ("ScrShot" -as [type])) { Add-Type -TypeDefinition $codeSave -ReferencedAssemblies System.Drawing }
        return [ScrShot]::Save($hwnd, $path)
    } catch { return $null }
}

function Write-Reflection {
    $script:Evidence.endedAt = (Get-Date).ToString("o")
    if (Test-Path $ClientLog) {
        $script:Evidence.clientLogTail = (Get-Content $ClientLog -Tail 80 -EA SilentlyContinue) -join "`n"
    }
    $md = @()
    $md += "# Real-level bots reflection"
    $md += ""
    $md += "- pass: **$($script:Evidence.pass)**"
    $md += "- started: $($script:Evidence.startedAt)"
    $md += "- ended: $($script:Evidence.endedAt)"
    $md += ""
    $md += "## Steps"
    foreach ($s in $script:Evidence.steps) { $md += "- $s" }
    $md += ""
    $md += "## Errors / reflections"
    if ($script:Evidence.errors.Count -eq 0) { $md += "- (none)" }
    else { foreach ($e in $script:Evidence.errors) { $md += "- $e" } }
    $md += ""
    $md += "## Final GET_STATUS"
    $md += '```'
    $md += $script:Evidence.finalStatus
    $md += '```'
    $md += ""
    $md += "## client.log tail"
    $md += '```'
    $md += $script:Evidence.clientLogTail
    $md += '```'
    $md -join "`n" | Set-Content (Join-Path $ReflectDir "reflection.md") -Encoding UTF8
    ($script:Evidence | ConvertTo-Json -Depth 6) | Set-Content (Join-Path $ReflectDir "reflection.json") -Encoding UTF8
    Note "Reflection -> $ReflectDir"
}

# ---- clean ----
Note "Cleanup previous processes"
# KI-2026-011: fail fast — ME never CreateDevice under RDP (hooksInstalled stays false).
if (Test-RemoteDesktopSession) {
    FailNote "SM_REMOTESESSION=1 (Remote Desktop). Mirror's Edge blocks 3D; hooks never install. Use Parsec/Moonlight/console - see docs/troubleshooting.md + KI-2026-011."
    Write-Reflection
    exit 1
}
$zombieMe = @(Get-Process MirrorsEdge -EA SilentlyContinue | Where-Object {
    try { $_.Threads.Count -eq 0 } catch { $false }
})
if ($zombieMe.Count -gt 0) {
    FailNote ('MirrorsEdge zombie EPROCESS pid={0} (0 threads) - reboot Windows before harness; cannot Stop-Process / relaunch' -f (($zombieMe | ForEach-Object { $_.Id }) -join ','))
    Write-Reflection
    exit 1
}
Get-Process MirrorsEdge, ModuleLauncher, multiplayer-server -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
Start-Sleep 2
Remove-Item $ClientLog -Force -EA SilentlyContinue
Remove-Item $TargetFile -Force -EA SilentlyContinue
Remove-Item (Join-Path $env:TEMP "spawn_drain_trace.txt") -Force -EA SilentlyContinue
Remove-Item (Join-Path $env:TEMP "spawn_queue_trace.txt") -Force -EA SilentlyContinue
Remove-Item (Join-Path $env:TEMP "mirroredge-engine-spawn.log") -Force -EA SilentlyContinue
Remove-Item (Join-Path $env:TEMP "mirroredge-host-bones.bin") -Force -EA SilentlyContinue
Remove-Item (Join-Path $env:TEMP "mirroredge-host-bones-cycle.bin") -Force -EA SilentlyContinue
Remove-Item (Join-Path $env:TEMP "mirroredge-bone-clip-Idle.bin") -Force -EA SilentlyContinue
Remove-Item (Join-Path $env:TEMP "mirroredge-bone-clip-Walking.bin") -Force -EA SilentlyContinue
Remove-Item (Join-Path $env:TEMP "mirroredge-bone-clip-Falling.bin") -Force -EA SilentlyContinue
Remove-Item (Join-Path $env:TEMP "mirroredge-bot-motion.log") -Force -EA SilentlyContinue

# Purge old shots / empty session dirs / stale reflections (keep recent).
$clearTemp = Join-Path $Repo "tools\debug-harness\tools\clear-harness-temp.ps1"
if (Test-Path -LiteralPath $clearTemp) {
    . $clearTemp
    $purge = Clear-HarnessTempArtifacts -RetainDays 2 -KeepNewestShots 40 -KeepNewestReflections 20
    Note ("Temp purge: files={0} dirs={1} freedMB={2}" -f `
        $purge.removedFiles, $purge.removedDirs, $purge.freedMB)
} else {
    Note "WARN: clear-harness-temp.ps1 missing - skip shot/log purge"
}

# ---- deploy latest dist modules into GameRoot (harness does not build) ----
function Deploy-DistModulesToGame {
    param([string]$Root, [string]$RepoRoot)
    $dist = Join-Path $RepoRoot "dist\modules"
    $pairs = @(
        @{ Src = Join-Path $dist "engine\engine.dll"; Dst = Join-Path $Root "modules\engine\engine.dll" },
        @{ Src = Join-Path $dist "multiplayer\multiplayer.dll"; Dst = Join-Path $Root "modules\multiplayer\multiplayer.dll" }
    )
    $serverSrc = Join-Path $dist "multiplayer\multiplayer-server.exe"
    if (-not (Test-Path -LiteralPath $serverSrc)) {
        $serverSrc = Join-Path $RepoRoot "mods\multiplayer\server\multiplayer-server.exe"
    }
    if (Test-Path -LiteralPath $serverSrc) {
        $pairs += @{ Src = $serverSrc; Dst = Join-Path $Root "multiplayer-server.exe" }
    }
    $copied = 0
    foreach ($p in $pairs) {
        if (-not (Test-Path -LiteralPath $p.Src)) {
            Note ("WARN: dist missing {0} - skip copy" -f $p.Src)
            continue
        }
        $dstDir = Split-Path -Parent $p.Dst
        if (-not (Test-Path -LiteralPath $dstDir)) {
            FailNote ("game module dir missing: {0} (incomplete GameRoot? use Steam path with module_manager)" -f $dstDir)
            return $false
        }
        try {
            Copy-Item -LiteralPath $p.Src -Destination $p.Dst -Force
            $copied++
            Note ("Deployed {0} -> {1}" -f (Split-Path $p.Src -Leaf), $p.Dst)
        } catch {
            FailNote ("deploy copy failed {0}: {1}" -f $p.Dst, $_.Exception.Message)
            return $false
        }
    }
    $mm = Join-Path $Root "modules\module_manager\module_manager.dll"
    if (-not (Test-Path -LiteralPath $mm)) {
        FailNote "module_manager.dll missing under GameRoot - hooks will never install (do not use incomplete EA Games tree)"
        return $false
    }
    Note ("Auto-deploy ok ({0} file(s))" -f $copied)
    return $true
}
if (-not (Deploy-DistModulesToGame -Root $GameRoot -RepoRoot $Repo)) {
    Write-Reflection
    exit 1
}

if (-not (Test-Path "$GameRoot\ModuleLauncher.exe")) {
    FailNote "ModuleLauncher missing at $GameRoot"
    Write-Reflection
    exit 1
}
if (-not (Test-Path "$GameRoot\multiplayer-server.exe")) {
    FailNote "multiplayer-server.exe missing at $GameRoot"
    Write-Reflection
    exit 1
}

# ---- server ----
Note "Start multiplayer-server"
# Go server init() appends to %TEMP%\mmultiplayer-server.log (stdout+file).
$ServerLog = Join-Path $env:TEMP "mmultiplayer-server.log"
Remove-Item $ServerLog -Force -EA SilentlyContinue
$server = Start-Process "$GameRoot\multiplayer-server.exe" -WorkingDirectory $GameRoot -PassThru `
    -WindowStyle Minimized
Start-Sleep 2
if (Test-Path $ServerLog) {
    $banner = Select-String -Path $ServerLog -Pattern 'push-relay' -SimpleMatch -EA SilentlyContinue | Select-Object -First 1
    if ($banner) { Note "Server UDP push-relay banner ok" }
    else { Note "WARN: server log missing push-relay banner (old binary?)" }
} else {
    Note "WARN: server log not created yet: $ServerLog"
}

# ---- launcher ----
Note "Start ModuleLauncher + Launch Game"
$launcher = Start-Process "$GameRoot\ModuleLauncher.exe" -WorkingDirectory $GameRoot -PassThru
$clicked = $false
for ($i = 0; $i -lt 60; $i++) {
    $launcher.Refresh()
    $hwnd = $launcher.MainWindowHandle
    if ($hwnd -ne [IntPtr]::Zero) {
        $btn = [MpPipe]::GetDlgItem($hwnd, 1004)
        if ($btn -ne [IntPtr]::Zero -and [MpPipe]::IsWindowEnabled($btn)) {
            [MpPipe]::SetForegroundWindow($hwnd) | Out-Null
            [MpPipe]::SendMessage($btn, [MpPipe]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            $clicked = $true
            Note "Clicked Launch Game"
            break
        }
    }
    Start-Sleep -Milliseconds 500
}
if (-not $clicked) {
    FailNote "Could not click Launch Game"
    Write-Reflection
    exit 1
}

# ---- wait game / hooks / core ----
$game = $null
for ($i = 0; $i -lt 90; $i++) {
    $game = Get-LiveMirrorsEdgeProcess
    if ($game) { Note "Game PID $($game.Id)"; break }
    $z = Get-Process MirrorsEdge -EA SilentlyContinue | Select-Object -First 1
    if ($z -and (@($z.Threads).Count -eq 0) -and ($i -eq 5)) {
        FailNote ("Launch clicked but only zombie MirrorsEdge pid={0} - reboot Windows" -f $z.Id)
        Write-Reflection
        exit 1
    }
    Start-Sleep 1
}
if (-not $game) { FailNote "Game did not start"; Write-Reflection; exit 1 }

$hooksReady = $false
for ($i = 0; $i -lt 120; $i++) {
    if (Test-MeRemoteDesktopBlockDialog) {
        FailNote "ME Remote Desktop MessageBox (KI-2026-011) - CreateDevice blocked; abort hooks wait"
        Write-Reflection
        exit 1
    }
    $ms = MgrQ "GET_STATUS"
    if ($ms -match '"hooksInstalled"\s*:\s*true') {
        Note "Manager hooks ready at ${i}s"
        $hooksReady = $true
        break
    }
    if ($i % 15 -eq 14) { Note "Waiting hooks... ${i}s" }
    Start-Sleep 1
}
if (-not $hooksReady) {
    FailNote "Manager hooksInstalled never true after 120s (RDP/MessageBox/proxy?). See KI-2026-011 / troubleshooting Remote Desktop."
    Write-Reflection
    exit 1
}

$coreReady = $false
for ($i = 0; $i -lt 120; $i++) {
    if (Test-MeRemoteDesktopBlockDialog) {
        FailNote "ME Remote Desktop MessageBox (KI-2026-011) during core wait"
        Write-Reflection
        exit 1
    }
    $cs = CoreQ "GET_STATUS"
    $st = Parse-Status $cs
    if ($st.modReady -or ($cs -match '"component"\s*:\s*"core"' -and $cs -notmatch '^ERR')) {
        # Prefer explicit modReady from engine block if present
        if ($cs -match '"modReady"\s*:\s*true' -or $cs -match '"sdkReady"\s*:\s*true' -or $cs -match '"engine"') {
            Note "Core pipe alive at ${i}s"
            $coreReady = $true
            break
        }
        if ($i -gt 5) { Note "Core pipe alive at ${i}s"; $coreReady = $true; break }
    }
    if ($i % 15 -eq 14) { Note "Waiting core... ${i}s" }
    Start-Sleep 1
}
if (-not $coreReady) {
    FailNote "Core control pipe never ready after 120s"
    Write-Reflection
    exit 1
}

# Settle title / press-start without gameplay hooks.
# Require non-zero gameHwnd first - exact FindWindow("Mirror's Edge") fails when
# borderless retitles the window (e.g. "Mirror's EdgeT"), leaving INJECT_KEY as no-op.
Note "Wait for gameHwnd (InjectKey target)"
$hwndReady = $false
for ($i = 0; $i -lt 60; $i++) {
    $st = Parse-Status (CoreQ "GET_STATUS")
    $raw = CoreQ "GET_STATUS"
    $hwndVal = 0
    if ($raw -match '"gameHwnd"\s*:\s*(\d+)') { $hwndVal = [int64]$Matches[1] }
    if ($hwndVal -gt 0) { Note "gameHwnd=$hwndVal at ${i}s"; $hwndReady = $true; break }
    if ($i % 10 -eq 9) { Note "Waiting gameHwnd... ${i}s" }
    Start-Sleep 1
}
if (-not $hwndReady) {
    FailNote "gameHwnd still 0 - INJECT_KEY cannot target window (title mismatch / Window cache)"
}

Note "Advance title with INJECT_KEY until past splash (no gameplay hooks yet)"
$gameProc = Get-LiveMirrorsEdgeProcess
$readyForStory = $false
for ($r = 0; $r -lt 50; $r++) {
    CoreQ "INJECT_KEY 0x0D" | Out-Null
    Start-Sleep -Milliseconds 80
    CoreQ "INJECT_KEY 0x0D UP" | Out-Null
    if ($gameProc -and $gameProc.MainWindowHandle -ne [IntPtr]::Zero) {
        try {
            [MpPipe]::SetForegroundWindow($gameProc.MainWindowHandle) | Out-Null
            [MpPipe]::PostMessage($gameProc.MainWindowHandle, [MpPipe]::WM_KEYDOWN, [IntPtr]0x0D, [IntPtr]::Zero) | Out-Null
            Start-Sleep -Milliseconds 40
            [MpPipe]::PostMessage($gameProc.MainWindowHandle, [MpPipe]::WM_KEYUP, [IntPtr]0x0D, [IntPtr]::Zero) | Out-Null
        } catch {}
    }
    Start-Sleep 1
    # Pure-white splash (ME logo) has max luma ~255 with little variety.
    # Main menu / tutorial have mixed colors (max often <250 and not flat white).
    $lit = Test-ScreenLit -MinLuma 12
    if ($lit -and ("Scr" -as [type]) -and $gameProc) {
        try {
            $luma = [Scr]::MaxLuma($gameProc.MainWindowHandle)
            # Accept once we leave the white splash (logo) or have spent enough Enters.
            if ($luma -lt 250 -or $r -ge 12) {
                $readyForStory = $true
                Note ("Ready for Story after Enter round {0} (luma={1})" -f $r, $luma)
                Capture-GameShot "title-ready" | ForEach-Object { Note "Shot $_" }
                break
            } elseif (($r % 5) -eq 4) {
                Note "Splash still white (luma=$luma) after Enter x$($r+1)"
            }
        } catch {
            if ($r -ge 12) { $readyForStory = $true; break }
        }
    } elseif (($r % 5) -eq 4) {
        Note "Still black after Enter x$($r+1)"
        Capture-GameShot ("title-$r") | ForEach-Object { Note "Shot $_" }
    }
}
if (-not $readyForStory) {
    FailNote "Never left splash/black - refusing START_NEW_GAME"
    $script:Evidence.finalStatus = CoreQ "GET_STATUS"
    Write-Reflection
    exit 2
}

Note "START_NEW_GAME (Story tutorial path) after title/menu ready"
$r = CoreQ "START_NEW_GAME"
Note "START_NEW_GAME -> $r"
if ($r -notmatch '^OK') {
    FailNote "START_NEW_GAME failed: $r"
}

$mapReady = $false
for ($i = 0; $i -lt 240; $i++) {
    $st = Parse-Status (CoreQ "GET_STATUS")
    $raw = $st.raw
    $clientMap = ""
    if ($raw -match '"clientMap"\s*:\s*"([^"]*)"') { $clientMap = $Matches[1] }
    if ($i % 10 -eq 0) {
        Note ("Wait map [{0}s] map='{1}' clientMap='{2}' ig={3} lit={4}" -f $i, $st.map, $clientMap, $st.ig, (Test-ScreenLit -MinLuma 20))
    }
    $effective = $st.map
    if (-not $effective) { $effective = $clientMap }
    if ($effective -and $effective -ne "tdmainmenu" -and $effective -ne "" -and $effective -ne "gameplay") {
        Note "Real level: $effective"
        $mapReady = $true
        $Level = $effective
        break
    }
    # Do NOT ENSURE_GAMEPLAY_HOOKS here — KI-2026-005: hooks before Story
    # settles can freeze LoadMap. Hooks run once after mapReady below.
    if ($i -gt 35 -and (Test-ScreenLit -MinLuma 20)) {
        if ($i -ge 55 -and (Test-ScreenLit -MinLuma 20)) {
            $shot = Capture-GameShot "visual-map-$i"
            Note "Accepting lit visual level (map empty; host-gate before bots) shot=$shot"
            $mapReady = $true
            if (-not $Level) { $Level = "tutorial_p" }
            break
        }
    }
    if ($i -gt 10 -and ($i % 12) -eq 0) {
        # Space skips intro/cinematic beats that block inGameplay/map string.
        CoreQ "INJECT_KEY 0x20" | Out-Null
        Start-Sleep -Milliseconds 80
        CoreQ "INJECT_KEY 0x20 UP" | Out-Null
    }
    if ($i -gt 5 -and ($i % 8) -eq 0) {
        CoreQ "INJECT_KEY 0x0D" | Out-Null
        Start-Sleep -Milliseconds 80
        CoreQ "INJECT_KEY 0x0D UP" | Out-Null
    }
    Start-Sleep 1
}
if (-not $mapReady) {
    FailNote "Map never left title/tdmainmenu after START_NEW_GAME"
    $script:Evidence.finalStatus = CoreQ "GET_STATUS"
    Write-Reflection
    exit 2
}

Capture-GameShot "after-map" | ForEach-Object { Note "Shot $_" }

# visual-map-55 often catches GAME PAUSED / Runner Vision tip - world is
# loaded but paused; spawn drain never gets a usable PC until tips dismiss.
# Also dismiss crouch tip ([LEFT SHIFT]) seen on tutorial rooftop.
Note "Dismiss pause/hint overlays (Esc/Enter/Space/Shift)"
for ($d = 0; $d -lt 24; $d++) {
    foreach ($vk in @(0x1B, 0x0D, 0x20, 0x10)) {
        CoreQ ("INJECT_KEY 0x{0:X2}" -f $vk) | Out-Null
        Start-Sleep -Milliseconds 50
        CoreQ ("INJECT_KEY 0x{0:X2} UP" -f $vk) | Out-Null
        Start-Sleep -Milliseconds 80
    }
    if ($d % 6 -eq 5) {
        Capture-GameShot ("unpause-$d") | ForEach-Object { Note "Shot $_" }
    }
}
Start-Sleep 2

Note "ENSURE_GAMEPLAY_HOOKS after map settle"
CoreQ "ENSURE_GAMEPLAY_HOOKS" | Out-Null
CoreQ "ENSURE_MP_HOOKS" | Out-Null
Start-Sleep 12

Note "Inject multiplayer"
# Always write %TEMP%\multiplayer.settings before INJECT. Client Init reads
# client.server immediately (default 176.58.101.83); CoreQ SET after join is
# too late for bots on the local harness server. Do not leave a partial file
# — that wipes server and rem=0 forever.
$mpSettingsPath = Join-Path $env:TEMP "multiplayer.settings"
$mpJson = @{
    client = @{
        server = "127.0.0.1"
        room = $Room
        name = "TestHost"
    }
} | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($mpSettingsPath, $mpJson)
Note ("Wrote $mpSettingsPath (server=127.0.0.1 room=$Room)")
$inj = MgrQ "INJECT multiplayer"
Note "INJECT -> $inj"
Start-Sleep 4
CoreQ "SET multiplayer.server 127.0.0.1" | Out-Null
CoreQ "SET multiplayer.room $Room" | Out-Null
CoreQ "SET multiplayer.name TestHost" | Out-Null
CoreQ "RELOAD_SETTINGS" | Out-Null
Start-Sleep 2

$connected = $false
for ($i = 0; $i -lt 45; $i++) {
    $st = Parse-Status (CoreQ "GET_STATUS")
    if ($st.connected) { Note "Connected at ${i}s"; $connected = $true; break }
    if ($i % 5 -eq 4) { Note "Connect wait [${i}s] map=$($st.map)" }
    Start-Sleep 1
}
if (-not $connected) {
    FailNote "Multiplayer never connected"
}

Note "FORCE_HOSTED_LIVE"
$fh = CoreQ "FORCE_HOSTED_LIVE"
Note "FORCE_HOSTED_LIVE -> $fh"
MgrQ "CONSOLE_CLOSE" | Out-Null
MgrQ "MENU_CLOSE" | Out-Null

# Seed Follow target; do NOT start bots until host pose is live — queuing
# remotes while world=0 spins EndScene drain warm and freezes the game.
$hostPosOk = $false
$hostSt = [pscustomobject]@{ posX = -4814.0; posY = -7904.0; posZ = 5854.0; yaw = 0 }
$payload = (@{ x = $hostSt.posX; y = $hostSt.posY; z = $hostSt.posZ; yaw = [int]$hostSt.yaw } | ConvertTo-Json -Compress)
[System.IO.File]::WriteAllText($TargetFile, $payload)
Note ('Wrote fallback TargetFile ({0},{1},{2})' -f $hostSt.posX, $hostSt.posY, $hostSt.posZ)

$liveHost = Wait-HostPoseReady -MaxSeconds 70 -Label "pre-bot host pose"
$preBotHostOk = $false
$preBotHostSec = -1
if ($null -ne $liveHost) {
    $hostPosOk = $true
    $hostSt = $liveHost
    $preBotHostOk = $true
    $preBotHostSec = 0
    foreach ($step in $script:Evidence.steps) {
        if ($step -match 'pre-bot host pose ready at (\d+)s') {
            $preBotHostSec = [int]$Matches[1]
            break
        }
    }
    Note ("Pre-bot host pose OK in ~{0}s (camera/world warm before bots)" -f $preBotHostSec)
} else {
    # Safe idle TdEngine warm is 800/500ms (~60s for late GObjects slots).
    # Starting bots with world=0 / host pose dead yields rem=N sp=0 forever and
    # can soft-freeze Tick (2026-07-21). Fail closed instead of soft-continue.
    FailNote "Host pose not live after 70s - refusing to start bots (world/PC seed incomplete)"
    $script:Evidence.finalStatus = CoreQ "GET_STATUS"
    Write-Reflection
    exit 3
}

function Get-BotStandOff([double]$hx, [double]$hy, [double]$hz, [int]$yaw,
                         [double]$dist, [double]$lat) {
    $rad = ($yaw / 65536.0) * 2.0 * [Math]::PI
    $fx = [Math]::Cos($rad); $fy = [Math]::Sin($rad)
    $rx = -[Math]::Sin($rad); $ry = [Math]::Cos($rad)
    return @{
        x = [Math]::Round($hx + $fx * $dist + $rx * $lat, 1)
        y = [Math]::Round($hy + $fy * $dist + $ry * $lat, 1)
        z = [Math]::Round($hz, 1)
    }
}

# Distinct near-camera slots: left/right of look direction.
# Both Kate (ch=1): Miller (5) DynamicLoad after first TransformBones has hung
# EndScene drain (EXIT=3 rem=2 sp=1, 2026-07-20). SoftProbe also uses Kate.
# Laterals ±30 (was ±50/55): Cam1 stayed near solar panels on tutorial_p.
$botSlots = @(
    @{ ch = 1; dist = 260; lat = -30 },
    @{ ch = 1; dist = 280; lat =  32 }
)

# FORCE_HOSTED_LIVE starts as synthetic "gameplay". Prefer a concrete host map
# when known; otherwise use harness $Level (lit-visual path sets tutorial_p).
# Do not probe StreamingLevels from the game Tick to "fix" host map — that
# soft-froze rem=2 sp=0 (2026-07-21). Host may adopt bot level via
# TryAdoptRemoteGameplayLevel once bots report tutorial_p.
$botLevel = "gameplay"
if ($hostSt.currentMap -and $hostSt.currentMap -ne "" -and
    $hostSt.currentMap -ne "gameplay" -and $hostSt.currentMap -ne "tdmainmenu" -and
    $hostSt.currentMap -ne "entry") {
    $botLevel = [string]$hostSt.currentMap
} elseif ($Level -and $Level -ne "" -and $Level -ne "gameplay" -and
          $Level -ne "tdmainmenu" -and $Level -ne "entry") {
    $botLevel = [string]$Level
    Note "botLevel from harness Level=$botLevel (host still synthetic gameplay)"
}
Note "Start $($botSlots.Count) Follow bots at host stand-offs (botLevel=$botLevel)"
$botProcs = @()
for ($i = 0; $i -lt [Math]::Min($BotCount, $botSlots.Count); $i++) {
    $slot = $botSlots[$i]
    $ch = $slot.ch
    $stand = Get-BotStandOff $hostSt.posX $hostSt.posY $hostSt.posZ ([int]$hostSt.yaw) `
        $slot.dist $slot.lat
    # One ArgumentList string - array form dropped -File on this host and bots
    # never joined (rem=0). Paths with spaces need nested escaped quotes.
    $arg = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Server 127.0.0.1 -Port 5222 -Name Cam{1} -Room {2} -Character {3} -Level {4} -RunSeconds {5} -Follow -FollowDistance {6} -FollowLateral:{7} -StartX:{8} -StartY:{9} -StartZ:{10} -TargetFile "{11}" -AnimateBones' -f `
        $BotScript, ($i + 1), $Room, $ch, $botLevel, ($PlaySeconds + 60), $slot.dist, $slot.lat, $stand.x, $stand.y, $stand.z, $TargetFile
    $botProcs += Start-Process powershell -ArgumentList $arg -PassThru -WindowStyle Minimized
    Note ('Started Cam{0} pid={1} char={2} start=({3},{4},{5}) lat={6}' -f `
        ($i + 1), $botProcs[-1].Id, $ch, $stand.x, $stand.y, $stand.z, $slot.lat)
    Start-Sleep 1
}

$spawnOk = $false
$posedOk = $false
$hostLive = $false
$softProbeStarted = $false
$softProbeStartedAt = -1
$softProbeLogOffset = 0
$liveMeshCaptured = $false
$softCollisionOk = $false
$tagModeOk = $false
$interactOk = $false
$physicsFloorOk = $false
$physicsWallOk = $false
$remoteBonesOk = $false
if ($SkipSoftCollisionProbe) {
    $softCollisionOk = $true
    $tagModeOk = $true
    $interactOk = $true
    Note "Soft collision / Tag / Interact probe skipped (-SkipSoftCollisionProbe)"
}
if ($SkipPhysicsProbe) {
    $physicsFloorOk = $true
    $physicsWallOk = $true
    Note "Physics world-clamp probe skipped (-SkipPhysicsProbe)"
}
$BoneDump = Join-Path $env:TEMP "mirroredge-host-bones.bin"
$BoneCycle = Join-Path $env:TEMP "mirroredge-host-bones-cycle.bin"
$BoneClipWalking = Join-Path $env:TEMP "mirroredge-bone-clip-Walking.bin"
$BoneClipFalling = Join-Path $env:TEMP "mirroredge-bone-clip-Falling.bin"
$clipDriveDone = $false
$lastClientLogWrite = if (Test-Path $ClientLog) { (Get-Item $ClientLog).LastWriteTime } else { Get-Date }
for ($i = 0; $i -lt $PlaySeconds; $i++) {
    if ((Test-Path $ClientLog) -and $i -gt 20) {
        $lw = (Get-Item $ClientLog).LastWriteTime
        if ($lw -gt $lastClientLogWrite) { $lastClientLogWrite = $lw }
        $logStaleSec = [int]((Get-Date) - $lastClientLogWrite).TotalSeconds
        if ($logStaleSec -gt 90 -and -not $spawnOk) {
            FailNote ("client.log stale ${logStaleSec}s with no spawn - game likely frozen (see phase.bin)")
            Get-Process MirrorsEdge -EA SilentlyContinue | Stop-Process -Force
            break
        }
        if ($spawnOk -and $logStaleSec -gt 60 -and (Test-GameHung)) {
            FailNote ("game hung after spawn (IsHungAppWindow; client.log stale ${logStaleSec}s) - soft-despawn/path check")
            Get-Process MirrorsEdge -EA SilentlyContinue | Stop-Process -Force
            break
        }
    }
    # IsHungAppWindow trips during cold SpawnCharacter / first pose warm
    # (morning PASS waited ~45s with rem>0 sp=0). Require sustained hang
    # plus a stale client.log so transient EndScene hitches do not abort.
    if ($i -gt 12 -and (Test-GameHung)) {
        if (-not $script:HungPollStreak) { $script:HungPollStreak = 0 }
        $script:HungPollStreak++
        $logAgeSec = 0
        if (Test-Path $ClientLog) {
            $logAgeSec = [int]((Get-Date) - (Get-Item $ClientLog).LastWriteTime).TotalSeconds
        }
        if ($script:HungPollStreak -ge 8 -and $logAgeSec -gt 12) {
            FailNote ("IsHungAppWindow sustained {0}s + client.log stale {1}s - abort" -f `
                $script:HungPollStreak, $logAgeSec)
            Get-Process MirrorsEdge -EA SilentlyContinue | Stop-Process -Force
            break
        }
    } else {
        $script:HungPollStreak = 0
    }
    $st = Parse-Status (CoreQ "GET_STATUS")
    if (Write-TargetFromStatus $st) {
        $hostPosOk = $true
        $hostLive = $true
        $hostSt = $st
    }
    if ($i % 5 -eq 0) {
        Note ('Poll [{0}s] map={1} rem={2} sp={3} posed={4} host=({5:n0},{6:n0},{7:n0}) bonesDump={8} cycleFrames={9}' -f `
            $i, $st.map, $st.remotes, $st.spawned, $st.posed, $st.posX, $st.posY, $st.posZ,
            (Test-Path $BoneDump), (Get-BoneCycleFrameCount $BoneCycle))
    }

    if ($hostLive -and $hostPosOk -and -not $clipDriveDone -and $i -ge 3) {
        # Fill Walking (+ Falling via jump) Mesh3p clips while bots are live.
        Drive-HostMeshClipCapture -WalkMs 4200 -AlsoJump
        $clipDriveDone = $true
    }
    $walkFrames = Get-BoneCycleFrameCount $BoneClipWalking
    if ($hostLive -and $clipDriveDone -and $walkFrames -lt 4 -and ($i % 10 -eq 0) -and $i -lt 45) {
        Note ("Walking clip still {0} frames - re-drive W+Shift" -f $walkFrames)
        Drive-HostMeshClipCapture -WalkMs 2800
    }

    # Prefer client.log spawn/pose over GET_STATUS spawnedPlayers (engine-spawn
    # can show stage ok while client spawn-ok lags, and status can lag behind log).
    if (Test-Path $ClientLog) {
        $tail = Get-Content $ClientLog -Tail 120 -EA SilentlyContinue
        $logSpawnNow = @($tail | Where-Object { $_ -match 'spawn ok' }).Count
        $logPoseNow = @($tail | Where-Object { $_ -match 'remote pose applied' }).Count
        if ($logSpawnNow -ge $BotCount) { $spawnOk = $true }
        if ($logPoseNow -ge $BotCount) { $posedOk = $true }
    }
    if (-not $spawnOk -and $st.spawned -ge $BotCount) { $spawnOk = $true }
    if (-not $posedOk -and $st.posed -ge $BotCount) { $posedOk = $true }

    $bonesReady = (Test-Path $BoneDump) -and ((Get-Item $BoneDump).Length -ge (329 * 2))
    $boneCycleReady = (Get-BoneCycleFrameCount $BoneCycle) -ge 4
    if (-not $remoteBonesOk -and (Test-Path $ClientLog) -and $bonesReady) {
        # SoftProbe soft-collision spam can push early "remote bones applied"
        # out of a short Tail — latch once BotCount lines are seen anywhere.
        $boneHits = @(Select-String -Path $ClientLog -Pattern 'remote bones applied' -EA SilentlyContinue)
        if ($boneHits.Count -ge $BotCount) { $remoteBonesOk = $true }
    }
    # Keep scanning SoftProbe gates until ALL required ones are green.
    # Previously this block was gated on (-not $softCollisionOk), so Tag/Interact
    # stopped updating the moment softColl engaged (PASS only via final scan).
    $needSoftProbeScan = $softProbeStarted -and (Test-Path $ClientLog) -and (
        ((-not $SkipSoftCollisionProbe) -and
         (-not $softCollisionOk -or -not $tagModeOk -or -not $interactOk)) -or
        ((-not $SkipPhysicsProbe) -and
         (-not $physicsFloorOk -or -not $physicsWallOk))
    )
    if ($needSoftProbeScan) {
        try {
            $fs = [System.IO.File]::Open($ClientLog, [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            try {
                if ($fs.Length -gt $softProbeLogOffset) {
                    $fs.Position = $softProbeLogOffset
                    $sr = New-Object System.IO.StreamReader($fs)
                    $delta = $sr.ReadToEnd()
                    if (-not $SkipSoftCollisionProbe) {
                        if (-not $softCollisionOk -and
                            [regex]::IsMatch($delta, 'soft collision engaged')) {
                            $softCollisionOk = $true
                            Note "Soft collision engaged (client.log after SoftProbe)"
                        }
                        if (-not $tagModeOk -and
                            [regex]::IsMatch($delta, 'tag mode live|tagged id=')) {
                            $tagModeOk = $true
                            Note "Tag mode live (client.log after SoftProbe -StartTag)"
                        }
                        if (-not $interactOk -and
                            [regex]::IsMatch($delta, 'interact recv|interact sent')) {
                            $interactOk = $true
                            Note "Interact wave seen (client.log after SoftProbe -SendInteract)"
                        }
                    }
                    if (-not $SkipPhysicsProbe) {
                        if (-not $physicsFloorOk -and
                            [regex]::IsMatch($delta, 'world clamp floor')) {
                            $physicsFloorOk = $true
                            Note "World clamp floor (client.log after SoftProbe PhysicsFallDrop)"
                        }
                        if (-not $physicsWallOk -and
                            [regex]::IsMatch($delta, 'world clamp wall')) {
                            $physicsWallOk = $true
                            Note "World clamp wall (client.log after SoftProbe PhysicsWallSlam)"
                        }
                    }
                }
            } finally { $fs.Close() }
        } catch {}
        if (-not $softCollisionOk -and (Test-Path (Join-Path $env:TEMP "mirroredge-soft-collision.ok"))) {
            $softCollisionOk = $true
            Note "Soft collision engaged (flag file)"
        }
    }

    # SoftProbe: sit on host XY with FollowDistance inside softCollisionRadius (88).
    # Also runs Tag/Interact and/or PhysicsFallDrop+WallSlam unless skipped.
    $wantSoftProbe = (-not $SkipSoftCollisionProbe) -or (-not $SkipPhysicsProbe)
    if ($wantSoftProbe -and -not $softProbeStarted -and $hostLive -and $spawnOk -and $posedOk -and $i -ge 6) {
        $hx = $hostSt.posX; $hy = $hostSt.posY; $hz = $hostSt.posZ
        if ([Math]::Abs($hx) + [Math]::Abs($hy) + [Math]::Abs($hz) -gt 10.0) {
            $probeCh = 1
            $probeDist = 70
            $extraSwitches = ""
            if (-not $SkipSoftCollisionProbe) {
                $extraSwitches += " -StartTag -SendInteract"
            }
            if (-not $SkipPhysicsProbe) {
                # Delay FallDrop/WallSlam so SoftProbe stays near host for
                # softColl/Tag + live-mesh screenshot (mesh visual V5).
                # FollowDistance 70 (was 18): still inside softRadius 88, but
                # full Kate body in FOV instead of near-clip / corridor-far.
                $extraSwitches += " -PhysicsFallDrop -PhysicsWallSlam -PhysicsProbeDelayMs 10000"
            }
            $argProbe = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Server 127.0.0.1 -Port 5222 -Name SoftProbe -Room {1} -Character {2} -Level {3} -RunSeconds {4} -Follow -FollowDistance {5} -FollowLateral:0 -StartX:{6} -StartY:{7} -StartZ:{8} -TargetFile "{9}" -AnimateBones{10}' -f `
                $BotScript, $Room, $probeCh, $botLevel, ($PlaySeconds + 60), $probeDist, `
                ([Math]::Round($hx, 1)), ([Math]::Round($hy, 1)), ([Math]::Round($hz, 1)), $TargetFile, $extraSwitches
            $botProcs += Start-Process powershell -ArgumentList $argProbe -PassThru -WindowStyle Minimized
            $softProbeStarted = $true
            $softProbeStartedAt = $i
            if (Test-Path $ClientLog) {
                $softProbeLogOffset = (Get-Item $ClientLog).Length
            }
            $SoftCollFlagStart = Join-Path $env:TEMP "mirroredge-soft-collision.ok"
            if (Test-Path $SoftCollFlagStart) {
                Remove-Item -LiteralPath $SoftCollFlagStart -Force -EA SilentlyContinue
            }
            Note ('Started SoftProbe pid={0} at host=({1},{2},{3}) FollowDistance={4} extras={5}' -f `
                $botProcs[-1].Id, ([Math]::Round($hx, 1)), ([Math]::Round($hy, 1)), ([Math]::Round($hz, 1)), $probeDist, $extraSwitches.Trim())
        }
    }

    $softWaitDone = ($softCollisionOk -and $tagModeOk -and $interactOk) -or $SkipSoftCollisionProbe
    $physWaitDone = ($physicsFloorOk -and $physicsWallOk) -or $SkipPhysicsProbe
    # Mesh visual V6c: shoot while SoftProbe still in PhysicsProbeDelay near host
    # (FallDrop/WallSlam yank SoftProbe out of FOV; late settle shot is empty roof).
    if (-not $liveMeshCaptured -and $spawnOk -and $posedOk -and $remoteBonesOk -and
        $softProbeStarted -and ($i - $softProbeStartedAt) -ge 3 -and ($i - $softProbeStartedAt) -le 9 -and
        ($softWaitDone -or ($softCollisionOk -and $tagModeOk))) {
        Look-TowardMeshStandOff
        Capture-GameShot "live-mesh" | ForEach-Object { Note "Live mesh shot (SoftProbe delay window) $_" }
        $liveMeshCaptured = $true
    }
    # SoftProbe settle timeout only near PlaySeconds end — do not early-exit
    # while Tag/Interact/physics still missing.
    $softTimedOut = $softProbeStarted -and ($i - $softProbeStartedAt) -ge 50 -and `
        $i -ge ($PlaySeconds - 20)
    $phase5Ready = $false
    if ((Test-Path $ClientLog) -and $bonesReady -and $remoteBonesOk) {
        $tail5 = Get-Content $ClientLog -Tail 100 -EA SilentlyContinue
        $phase5Ready =
            [bool]($tail5 | Where-Object { $_ -match 'host Mesh3p atoms=' } | Select-Object -First 1) -and
            [bool]($tail5 | Where-Object { $_ -match 'TransformBones visual=' } | Select-Object -First 1)
    }
    # Require live host + spawn/pose + host bone dump + cycle + remotes applying bones
    # + soft/Tag/Interact + physics clamp + phase5 Faith/Kate atom logs.
    if ($hostLive -and $spawnOk -and $posedOk -and $st.remotes -ge $BotCount -and $bonesReady -and $boneCycleReady -and $remoteBonesOk -and (($softWaitDone -and $physWaitDone) -or $softTimedOut) -and $phase5Ready) {
        Note ("Criteria met early at ${i}s (softColl={0} tag={1} interact={2} floor={3} wall={4} + phase5) - settle 6s then shoot" -f `
            $softCollisionOk, $tagModeOk, $interactOk, $physicsFloorOk, $physicsWallOk)
        Start-Sleep 6
        break
    }
    if ($softProbeStarted -and (-not $softCollisionOk -or -not $tagModeOk -or -not $interactOk -or -not $physicsFloorOk -or -not $physicsWallOk) -and (($i - $softProbeStartedAt) % 5 -eq 0) -and ($i -gt $softProbeStartedAt)) {
        Note ("Waiting SoftProbe gates softColl={0} tag={1} interact={2} floor={3} wall={4}..." -f `
            $softCollisionOk, $tagModeOk, $interactOk, $physicsFloorOk, $physicsWallOk)
    }
    if ($hostLive -and $spawnOk -and $posedOk -and $st.remotes -ge $BotCount -and $bonesReady -and -not $remoteBonesOk -and $i -ge 25) {
        if ($i % 5 -eq 0) { Note "Waiting for remote bones applied (bots picking up dump)..." }
    }
    if ($hostLive -and $spawnOk -and $posedOk -and $st.remotes -ge $BotCount -and $bonesReady -and $remoteBonesOk -and -not $boneCycleReady -and $i -ge 8) {
        if ($i % 5 -eq 0) { Note "Waiting for host bone cycle (>=4 frames)..." }
    }
    # Soft early path: live host + bots, but keep waiting for bones until timeout
    if ($hostLive -and $spawnOk -and $posedOk -and $st.remotes -ge $BotCount -and $i -ge ($PlaySeconds - 12)) {
        Note "Near timeout with live host+bots but bone path incomplete - settle then shoot"
        Start-Sleep 5
        break
    }
    Start-Sleep 1
}

# Mesh visual V5/V6: prefer early SoftProbe-delay live-mesh (above). Fallback
# shot here if gates never met in the 3–9s window. Final after Stop is empty
# rooftop (KI-012 null-only despawn).
if (-not $liveMeshCaptured -and $spawnOk -and $posedOk) {
    Look-TowardMeshStandOff
    Capture-GameShot "live-mesh" | ForEach-Object { Note "Live mesh shot (fallback, bots still up) $_" }
    Start-Sleep 1
}

# Stop bots before teardown shot so disconnect soft-despawn runs while harness
# still watches the process (and bots do not linger past PASS).
# Close overlays may hitch PrintWindow on a soft-dead device — check phase
# content age via decode, not file mtime (mmap often does not update mtime).
Note "Stopping harness bots ($($botProcs.Count) procs)"
Stop-HarnessBots $botProcs
Start-Sleep 5
$hungAfter = Test-GameHung
$phaseLastAge = -1
# Read mmap ring directly (decode-phase uses Write-Host; pipeline capture is empty).
$phasePath = Join-Path $env:TEMP "mirroredge-phase.bin"
if (Test-Path $phasePath) {
    try {
        $fs = [System.IO.File]::Open($phasePath, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $bytes = New-Object byte[] $fs.Length
            [void]$fs.Read($bytes, 0, $bytes.Length)
        } finally { $fs.Close() }
        $bestSeq = -1
        $bestText = ""
        $slotSize = 96
        $slots = [int]($bytes.Length / $slotSize)
        for ($si = 0; $si -lt $slots; $si++) {
            $text = [Text.Encoding]::ASCII.GetString($bytes, $si * $slotSize, $slotSize).Trim([char]0, ' ', "`r", "`n")
            if (-not $text) { continue }
            if ($text -match 'seq=(\d+)' -and [int64]$Matches[1] -gt $bestSeq) {
                $bestSeq = [int64]$Matches[1]
                $bestText = $text
            }
        }
        if ($bestText -match 't=(\d{2}):(\d{2}):(\d{2})') {
            $today = Get-Date
            $last = Get-Date -Hour ([int]$Matches[1]) -Minute ([int]$Matches[2]) -Second ([int]$Matches[3])
            if ($last -gt $today.AddMinutes(5)) { $last = $last.AddDays(-1) }
            $phaseLastAge = [int]($today - $last).TotalSeconds
        }
    } catch {
        Note ("phase ring read failed: $($_.Exception.Message)")
    }
}
if ($hungAfter -or ($phaseLastAge -ge 0 -and $phaseLastAge -gt 20)) {
    FailNote ("post-bot-stop hung=$hungAfter phaseLastAge=${phaseLastAge}s (KI-2026-012 Despawn)")
} else {
    Note ("Post-bot-stop: game not hung (phaseLastAge=${phaseLastAge}s)")
}

# Close overlays so the shot shows the level, not console spam
MgrQ "CONSOLE_CLOSE" | Out-Null
MgrQ "MENU_CLOSE" | Out-Null
Start-Sleep 1
$shot = Capture-GameShot "final"
if ($shot) { Note "Final shot $shot" }

$final = CoreQ "GET_STATUS"
$script:Evidence.finalStatus = $final
$st = Parse-Status $final
Note ('FINAL map={0} rem={1} sp={2} posed={3} host=({4},{5},{6})' -f `
    $st.map, $st.remotes, $st.spawned, $st.posed, $st.posX, $st.posY, $st.posZ)

# Evidence from logs (harness counts can lag)
$logSpawn = 0; $logPose = 0; $activation = $false
$bonesSampled = $false
$boneDumpOk = $false
$boneCycleOk = $false
$boneCycleFrames = 0
$bonesCycleLogged = $false
$remoteBonesApplied = 0
$remoteVelocityActive = $false
$hostMeshAtomsLogged = $false
$transformBonesLogged = $false
$pushRelayOk = $false
$remoteBonesLive = $false
$botCyclePlayback = $false
$softCollisionLogged = $false
$tagModeLogged = $false
$interactLogged = $false
# $BoneDump set in poll loop; re-resolve if missing
if (-not $BoneDump) { $BoneDump = Join-Path $env:TEMP "mirroredge-host-bones.bin" }
if (-not $BoneCycle) { $BoneCycle = Join-Path $env:TEMP "mirroredge-host-bones-cycle.bin" }
if (Test-Path $ClientLog) {
    $all = Get-Content $ClientLog -EA SilentlyContinue
    $logSpawn = @($all | Where-Object { $_ -match 'spawn ok' }).Count
    $logPose = @($all | Where-Object { $_ -match 'remote pose applied' }).Count
    $activation = [bool]($all | Where-Object { $_ -match 'activation set live' } | Select-Object -First 1)
    $bonesSampled = [bool]($all | Where-Object { $_ -match 'local bones sampled' } | Select-Object -First 1)
    $bonesCycleLogged = [bool]($all | Where-Object { $_ -match 'local bones cycle' } | Select-Object -First 1)
    $remoteBonesApplied = @($all | Where-Object { $_ -match 'remote bones applied' }).Count
    $remoteVelocityActive = [bool]($all | Where-Object { $_ -match 'remote velocity active' } | Select-Object -First 1)
    $hostMeshAtomsLogged = [bool]($all | Where-Object { $_ -match 'host Mesh3p atoms=' } | Select-Object -First 1)
    $transformBonesLogged = [bool]($all | Where-Object { $_ -match 'TransformBones visual=' } | Select-Object -First 1)
    $remoteBonesLive = [bool]($all | Where-Object { $_ -match 'remote bones live stream' } | Select-Object -First 1)
}
# SoftProbe gates (softColl / Tag / Interact / physics) must appear AFTER SoftProbe start.
$SoftCollFlag = Join-Path $env:TEMP "mirroredge-soft-collision.ok"
$softCollisionLogged = $false
$tagModeLogged = $false
$interactLogged = $false
$physicsFloorLogged = $false
$physicsWallLogged = $false
if ($softProbeStarted -and ((-not $SkipSoftCollisionProbe) -or (-not $SkipPhysicsProbe))) {
    if (Test-Path $ClientLog) {
        try {
            $fs = [System.IO.File]::Open($ClientLog, [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            try {
                if ($fs.Length -gt $softProbeLogOffset) {
                    $fs.Position = $softProbeLogOffset
                    $sr = New-Object System.IO.StreamReader($fs)
                    $delta = $sr.ReadToEnd()
                    if (-not $SkipSoftCollisionProbe) {
                        $softCollisionLogged = [regex]::IsMatch($delta, 'soft collision engaged')
                        $tagModeLogged = [regex]::IsMatch($delta, 'tag mode live|tagged id=')
                        $interactLogged = [regex]::IsMatch($delta, 'interact recv|interact sent')
                    }
                    if (-not $SkipPhysicsProbe) {
                        $physicsFloorLogged = [regex]::IsMatch($delta, 'world clamp floor')
                        $physicsWallLogged = [regex]::IsMatch($delta, 'world clamp wall')
                    }
                }
            } finally { $fs.Close() }
        } catch {}
    }
    if (-not $SkipSoftCollisionProbe -and -not $softCollisionLogged -and (Test-Path $SoftCollFlag)) {
        $softCollisionLogged = $true
        Note "Soft collision flag present (%TEMP%\\mirroredge-soft-collision.ok)"
    }
}
if ($softCollisionLogged) { $softCollisionOk = $true }
if ($tagModeLogged) { $tagModeOk = $true }
if ($interactLogged) { $interactOk = $true }
if ($physicsFloorLogged) { $physicsFloorOk = $true }
if ($physicsWallLogged) { $physicsWallOk = $true }
if ($SkipSoftCollisionProbe) {
    $softCollisionOk = $true
    $tagModeOk = $true
    $interactOk = $true
}
if ($SkipPhysicsProbe) {
    $physicsFloorOk = $true
    $physicsWallOk = $true
}
$BotMotionLog = Join-Path $env:TEMP "mirroredge-bot-motion.log"
if (Test-Path $BotMotionLog) {
    $botCyclePlayback = [bool](Select-String -Path $BotMotionLog -Pattern 'cycle playback (speed-driven|mebc-v2|speed)' -EA SilentlyContinue | Select-Object -First 1)
}
if (-not $ServerLog) { $ServerLog = Join-Path $env:TEMP "mmultiplayer-server.log" }
if (Test-Path $ServerLog) {
    $srv = Get-Content $ServerLog -EA SilentlyContinue
    $pushRelayBanner = [bool]($srv | Where-Object { $_ -match 'push-relay' } | Select-Object -First 1)
    $pushRelayTraffic = [bool]($srv | Where-Object { $_ -match 'push-relay first forward' } | Select-Object -First 1)
    if (-not $pushRelayTraffic) {
        foreach ($line in $srv) {
            if ($line -match 'push=(\d+)' -and [int]$Matches[1] -gt 0) {
                $pushRelayTraffic = $true
                break
            }
        }
    }
    $pushRelayOk = $pushRelayBanner -and $pushRelayTraffic
    Note ("Server log push-relay banner={0} traffic={1}" -f $pushRelayBanner, $pushRelayTraffic)
} else {
    Note "Server log missing: $ServerLog"
}
if (Test-Path $BoneDump) {
    $len = (Get-Item $BoneDump).Length
    $boneDumpOk = ($len -ge (329 * 2))
    Note ("Bone dump {0} bytes={1} ok={2}" -f $BoneDump, $len, $boneDumpOk)
} else {
    Note "Bone dump missing: $BoneDump"
}
$boneCycleFrames = Get-BoneCycleFrameCount $BoneCycle
$boneCycleOk = ($boneCycleFrames -ge 4)
if (Test-Path $BoneCycle) {
    Note ("Bone cycle {0} frames={1} ok={2}" -f $BoneCycle, $boneCycleFrames, $boneCycleOk)
} else {
    Note "Bone cycle missing: $BoneCycle"
}
foreach ($clip in @("Idle","Walking","Falling")) {
    $cp = Join-Path $env:TEMP ("mirroredge-bone-clip-{0}.bin" -f $clip)
    $fc = Get-BoneCycleFrameCount $cp
    Note ("Bone clip {0} frames={1}" -f $clip, $fc)
}
$walkClipOk = ((Get-BoneCycleFrameCount (Join-Path $env:TEMP "mirroredge-bone-clip-Walking.bin")) -ge 4)
$fallClipOk = ((Get-BoneCycleFrameCount (Join-Path $env:TEMP "mirroredge-bone-clip-Falling.bin")) -ge 4)
if ($walkClipOk) { Note "BONUS: Walking Mesh3p clip >=4 frames" }
if ($fallClipOk) { Note "BONUS: Falling Mesh3p clip >=4 frames" }
Note "client.log spawn ok=$logSpawn pose=$logPose activationLive=$activation hostPosOk=$hostPosOk bonesSampled=$bonesSampled boneDumpOk=$boneDumpOk boneCycleOk=$boneCycleOk boneCycleLogged=$bonesCycleLogged remoteBones=$remoteBonesApplied velocity=$remoteVelocityActive hostAtoms=$hostMeshAtomsLogged xform=$transformBonesLogged pushRelay=$pushRelayOk liveStream=$remoteBonesLive botCycle=$botCyclePlayback softColl=$softCollisionOk tag=$tagModeOk interact=$interactOk floor=$physicsFloorOk wall=$physicsWallOk probe=$softProbeStarted"

$mapOk = ($st.map -eq $Level -or $st.map -match 'tutorial' -or
          ($st.map -eq 'gameplay' -and $hostPosOk))
$botsVisible = (($logSpawn -ge $BotCount) -or ($st.spawned -ge $BotCount)) -and `
               (($logPose -ge $BotCount) -or ($st.posed -ge $BotCount))
# Prefer client.log counts in evidence (status is fallback only).
$script:Evidence.spawnSource = if ($logSpawn -ge $BotCount) { 'client.log' } else { 'GET_STATUS' }
# Base + phase1-6 + phase8 (host-driven live bone stream + bot speed cycle)
$script:Evidence.pass = [bool]($mapOk -and $connected -and $activation -and $botsVisible -and $hostPosOk)
$script:Evidence.bonesSampled = $bonesSampled
$script:Evidence.boneDumpOk = $boneDumpOk
$script:Evidence.boneCycleOk = $boneCycleOk
$script:Evidence.boneCycleFrames = $boneCycleFrames
$script:Evidence.remoteVelocityActive = $remoteVelocityActive
$script:Evidence.hostMeshAtomsLogged = $hostMeshAtomsLogged
$script:Evidence.transformBonesLogged = $transformBonesLogged
$script:Evidence.pushRelayOk = $pushRelayOk
$script:Evidence.remoteBonesLive = $remoteBonesLive
$script:Evidence.botCyclePlayback = $botCyclePlayback
$script:Evidence.softCollisionOk = $softCollisionOk
$script:Evidence.softProbeStarted = $softProbeStarted
$script:Evidence.tagModeOk = $tagModeOk
$script:Evidence.interactOk = $interactOk
$script:Evidence.physicsFloorOk = $physicsFloorOk
$script:Evidence.physicsWallOk = $physicsWallOk
$script:Evidence.preBotHostOk = $preBotHostOk
$script:Evidence.preBotHostSec = $preBotHostSec
$remoteBonesOk = ($remoteBonesApplied -ge $BotCount)
$phase8Ok = $remoteBonesLive -and $botCyclePlayback
$script:Evidence.phase8Ok = $phase8Ok
$script:Evidence.motionPass = [bool]($script:Evidence.pass -and $bonesSampled -and $boneDumpOk -and $remoteBonesOk -and $boneCycleOk -and $bonesCycleLogged -and $remoteVelocityActive -and $hostMeshAtomsLogged -and $transformBonesLogged -and $pushRelayOk -and $phase8Ok -and $softCollisionOk -and $tagModeOk -and $interactOk -and $physicsFloorOk -and $physicsWallOk)

if (-not $mapOk) { FailNote ("Map not tutorial: '{0}'" -f $st.map) }
if (-not $hostPosOk) {
    FailNote "Host pos still ~0 after GamePlayers PC/pawn seed; Follow cannot aim; check GetPlayerPawn(false) seed / Loading flag"
}
if (-not $botsVisible) {
    FailNote ('Bots not spawned/posed (status sp={0} posed={1}; log spawn={2} pose={3})' -f $st.spawned, $st.posed, $logSpawn, $logPose)
}
if (-not $activation) { FailNote "No activation set live in client.log" }
if (-not $bonesSampled) { FailNote "No 'local bones sampled' in client.log (host Mesh3p sample / dump path)" }
if (-not $boneDumpOk) { FailNote "Host bone dump missing or short (%TEMP%\mirroredge-host-bones.bin)" }
if (-not $remoteBonesOk) {
    FailNote ("No 'remote bones applied' x{0} in client.log (bots must load BoneFile / non-zero CompressedBones)" -f $BotCount)
}
if (-not $boneCycleOk) {
    FailNote ("Host bone cycle missing or <4 frames (%TEMP%\\mirroredge-host-bones-cycle.bin frames={0})" -f $boneCycleFrames)
}
if (-not $bonesCycleLogged) {
    FailNote "No 'local bones cycle' in client.log (host multi-frame ring dump path)"
}
if (-not $remoteVelocityActive) {
    FailNote "No 'remote velocity active' in client.log (phase4: 688-byte UDP Velocity trailer or derive-from-delta)"
}
if (-not $hostMeshAtomsLogged) {
    FailNote "No 'host Mesh3p atoms=' in client.log (phase5: atom-count gated Faith sample)"
}
if (-not $transformBonesLogged) {
    FailNote "No 'TransformBones visual=' in client.log (phase5: Kate/Miller remap path)"
}
if (-not $pushRelayOk) {
    FailNote "Server push-relay not verified (need banner + push>0 in %TEMP%\\mmultiplayer-server.log)"
}
if (-not $remoteBonesLive) {
    FailNote "No 'remote bones live stream' in client.log (phase8: continuous UDP compressed bones)"
}
if (-not $botCyclePlayback) {
    FailNote "No bot cycle playback (speed-driven|mebc-v2) in %TEMP%\\mirroredge-bot-motion.log (phase8)"
}
if (-not $softCollisionOk) {
    FailNote "No 'soft collision engaged' in client.log (SoftProbe overlap; client.softCollision enabled?)"
}
if (-not $tagModeOk) {
    FailNote "No 'tag mode live' / 'tagged id=' in client.log (SoftProbe -StartTag; rebuilt multiplayer-server?)"
}
if (-not $interactOk) {
    FailNote "No 'interact recv' / 'interact sent' in client.log (SoftProbe -SendInteract; B1 TCP wave)"
}
if (-not $physicsFloorOk) {
    FailNote "No 'world clamp floor' in client.log (SoftProbe -PhysicsFallDrop; client.worldClamp?)"
}
if (-not $physicsWallOk) {
    FailNote "No 'world clamp wall' in client.log (SoftProbe -PhysicsWallSlam; FastTrace blocked?)"
}
if (-not $preBotHostOk) {
    Note "SOFT: pre-bot host pose missing within 70s (TdEngine idle warm 800/500ms + Tick GamePlayers)"
}

Write-Reflection

if ($script:Evidence.motionPass -and $script:Evidence.errors.Count -eq 0) {
    Write-Host "`n=== PASS: motion phase1-8 + softColl + Tag + Interact + worldClamp ===" -ForegroundColor Green
    if ($preBotHostOk) {
        Write-Host ("=== BONUS: pre-bot host pose at ~{0}s ===" -f $preBotHostSec) -ForegroundColor Green
    }
    exit 0
}
if ($preBotHostOk) {
    Write-Host ("=== BONUS: pre-bot host pose at ~{0}s (even if motion PARTIAL) ===" -f $preBotHostSec) -ForegroundColor Green
}
if ($script:Evidence.errors.Count -gt 0 -and $script:Evidence.pass -and $bonesSampled -and $boneDumpOk -and $remoteBonesOk -and $boneCycleOk -and $bonesCycleLogged -and $remoteVelocityActive -and $hostMeshAtomsLogged -and $transformBonesLogged -and $pushRelayOk -and $phase8Ok -and (-not $softCollisionOk -or -not $tagModeOk -or -not $interactOk -or -not $physicsFloorOk -or -not $physicsWallOk)) {
    Write-Host ("`n=== PARTIAL: phase1-8 OK, SoftProbe gates incomplete softColl={0} tag={1} interact={2} floor={3} wall={4} ===" -f $softCollisionOk, $tagModeOk, $interactOk, $physicsFloorOk, $physicsWallOk) -ForegroundColor Yellow
    exit 2
}
if ($script:Evidence.errors.Count -gt 0 -and $script:Evidence.motionPass) {
    Write-Host "`n=== FAIL: motion criteria met but post-checks failed (hung/despawn?) ===" -ForegroundColor Red
    exit 4
}
if ($script:Evidence.pass -and $bonesSampled -and $boneDumpOk -and $remoteBonesOk -and $boneCycleOk -and $bonesCycleLogged -and $remoteVelocityActive -and $hostMeshAtomsLogged -and $transformBonesLogged -and $pushRelayOk) {
    Write-Host "`n=== PARTIAL: phase1-6 OK, host-driven cycle/live (phase8) incomplete ===" -ForegroundColor Yellow
    exit 2
}
if ($script:Evidence.pass -and $bonesSampled -and $boneDumpOk -and $remoteBonesOk -and $boneCycleOk -and $bonesCycleLogged -and $remoteVelocityActive -and $hostMeshAtomsLogged -and $transformBonesLogged) {
    Write-Host "`n=== PARTIAL: phase1-5 OK, push-relay (phase6) incomplete ===" -ForegroundColor Yellow
    exit 2
}
if ($script:Evidence.pass -and $bonesSampled -and $boneDumpOk -and $remoteBonesOk -and $boneCycleOk -and $bonesCycleLogged -and $remoteVelocityActive) {
    Write-Host "`n=== PARTIAL: phase1-4 OK, Faith/Kate atom logs (phase5) incomplete ===" -ForegroundColor Yellow
    exit 2
}
if ($script:Evidence.pass -and $bonesSampled -and $boneDumpOk -and $remoteBonesOk -and $boneCycleOk -and $bonesCycleLogged) {
    Write-Host "`n=== PARTIAL: phase1-3 OK, velocity (phase4) incomplete ===" -ForegroundColor Yellow
    exit 2
}
if ($script:Evidence.pass -and $bonesSampled -and $boneDumpOk -and $remoteBonesOk) {
    Write-Host "`n=== PARTIAL: phase1+2 OK, bone cycle (phase3) incomplete ===" -ForegroundColor Yellow
    exit 2
}
if ($script:Evidence.pass) {
    Write-Host "`n=== PARTIAL: bots near host OK, bone sample/dump incomplete ===" -ForegroundColor Yellow
    exit 2
} else {
    Write-Host "`n=== FAIL: see $ReflectDir ===" -ForegroundColor Yellow
    exit 3
}
