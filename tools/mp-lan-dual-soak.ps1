#Requires -Version 5.1
<#
  LAN dual real-client soak (host + client).

  Host:   .\tools\mp-lan-dual-soak.ps1 -Role host -SoakMinutes 15
  Client: .\tools\mp-lan-dual-soak.ps1 -Role client -PeerIp <host-lan-ip> -SoakMinutes 15

  Recommended order:
    1) Start host (prints PeerIp, waits -ClientGraceSec, then mp-real-level-bots).
    2) On peer, start client with that PeerIp (waits for TCP :5222, then auto boot/inject).

  Client automates: Start-SplitInjectionSession -> START_NEW_GAME -> wait server TCP
  -> inject MP (server=PeerIp) -> FORCE_HOSTED_LIVE -> wait activation live
  -> soak poll on client.log / GET_STATUS (pose/bones + optional udp seq).

  Paths from deploy.config.json / ME_DEPLOY_PATH only.
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('host', 'client')]
    [string]$Role,
    [string]$PeerIp = "",
    [string]$Room = "lan-soak",
    [int]$SoakMinutes = 15,
    [int]$ServerPort = 5222,
    # Host: seconds to wait after advertising PeerIp so the peer can launch client.
    [int]$ClientGraceSec = 90,
    # Client: how long to wait for host TCP before inject.
    [int]$ServerWaitSec = 600,
    [int]$InjectRetries = 3,
    [switch]$SkipBuild,
    [switch]$SkipServerWait,
    [switch]$AllowNoLive
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$lib = Join-Path $repoRoot "tools\debug-harness\lib\DebugHarness.psm1"
Import-Module $lib -Force

function Write-SoakStage {
    param([string]$Message)
    Write-Host ("[{0:HH:mm:ss}] lan-soak: {1}" -f (Get-Date), $Message)
}

function Get-LanIPv4Candidates {
    $list = @()
    try {
        $list = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object {
                $_.IPAddress -notlike "127.*" -and
                $_.PrefixOrigin -ne "WellKnown" -and
                $_.AddressState -eq "Preferred"
            } |
            Sort-Object {
                # Prefer private LAN ranges; deprioritize APIPA.
                if ($_.IPAddress -like "169.254.*") { 9 }
                elseif ($_.IPAddress -like "192.168.*") { 0 }
                elseif ($_.IPAddress -like "10.*") { 1 }
                elseif ($_.IPAddress -like "172.*") { 2 }
                else { 5 }
            } |
            Select-Object -ExpandProperty IPAddress -Unique)
    } catch {}
    if (-not $list -or $list.Count -eq 0) {
        # Fallback without Get-NetIPAddress.
        try {
            $list = @([System.Net.Dns]::GetHostAddresses([System.Net.Dns]::GetHostName()) |
                Where-Object { $_.AddressFamily -eq "InterNetwork" -and $_.ToString() -notlike "127.*" } |
                ForEach-Object { $_.ToString() })
        } catch {}
    }
    return @($list)
}

function Get-LanIPv4 {
    $c = Get-LanIPv4Candidates
    if ($c.Count -eq 0) { return $null }
    return $c[0]
}

function Write-SoakCoord {
    param([string]$Status, [hashtable]$Extra = @{})
    $data = @{ role = $Role; room = $Room; status = $Status; at = (Get-Date).ToString("o") }
    foreach ($k in $Extra.Keys) { $data[$k] = $Extra[$k] }
    try {
        Write-HarnessCoordination -Status ("lan-soak-{0}-{1}" -f $Role, $Status) -Summary $data
    } catch {
        Write-SoakStage ("WARN coordination write failed ({0})" -f $_.Exception.Message)
    }
}

function Write-HostAdvertiseFile {
    param([string]$Ip, [int]$Port, [string]$RoomName)
    $objDir = Join-Path $repoRoot "obj"
    if (-not (Test-Path -LiteralPath $objDir)) {
        New-Item -ItemType Directory -Path $objDir -Force | Out-Null
    }
    $path = Join-Path $objDir "lan-soak-host.json"
    $payload = [ordered]@{
        role      = "host"
        ip        = $Ip
        port      = $Port
        room      = $RoomName
        updatedAt = (Get-Date).ToUniversalTime().ToString("o")
        clientCmd = (".\tools\mp-lan-dual-soak.ps1 -Role client -PeerIp {0} -Room {1} -SoakMinutes {2}" -f $Ip, $RoomName, $SoakMinutes)
    }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($path, ($payload | ConvertTo-Json -Depth 4), $utf8)
    return $path
}

function Test-TcpOpen {
    param([string]$TargetHost, [int]$Port, [int]$TimeoutMs = 1500)
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $iar = $client.BeginConnect($TargetHost, $Port, $null, $null)
        $ok = $iar.AsyncWaitHandle.WaitOne($TimeoutMs, $false)
        if (-not $ok) {
            try { $client.Close() } catch {}
            return $false
        }
        $client.EndConnect($iar) | Out-Null
        $client.Close()
        return $true
    } catch {
        return $false
    }
}

function Wait-LanServerTcp {
    param(
        [string]$TargetHost,
        [int]$Port,
        [int]$TimeoutSec
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    Write-SoakStage ("waiting for TCP {0}:{1} (timeout {2}s)" -f $TargetHost, $Port, $TimeoutSec)
    $n = 0
    while ((Get-Date) -lt $deadline) {
        if (Test-TcpOpen -TargetHost $TargetHost -Port $Port) {
            Write-SoakStage ("server TCP open {0}:{1}" -f $TargetHost, $Port)
            return
        }
        $n++
        if (($n % 6) -eq 0) {
            $left = [Math]::Max(0, [int]($deadline - (Get-Date)).TotalSeconds)
            Write-SoakStage ("still waiting for {0}:{1} ({2}s left)" -f $TargetHost, $Port, $left)
        }
        Start-Sleep -Seconds 5
    }
    throw ("lan-soak client: server {0}:{1} not reachable within {2}s (start host first / check firewall)" -f $TargetHost, $Port, $TimeoutSec)
}

function Invoke-CoreQ([string]$cmd, [int]$TimeoutMs = 12000) {
    return Invoke-ModControlPipe -Command $cmd -Target core -TimeoutMs $TimeoutMs
}

function Write-ClientSettings {
    param([string]$Server, [string]$RoomName, [string]$PlayerName)
    $settingsPath = Join-Path $env:TEMP "multiplayer.settings"
    $jsonObj = [ordered]@{
        client = [ordered]@{
            server = $Server
            room   = $RoomName
            name   = $PlayerName
        }
    }
    ($jsonObj | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $settingsPath -Encoding ASCII
    return $settingsPath
}

function Invoke-InjectMultiplayerRetry {
    param([int]$Retries)
    $last = ""
    for ($a = 1; $a -le $Retries; $a++) {
        Write-SoakStage ("INJECT multiplayer attempt {0}/{1}" -f $a, $Retries)
        $last = Invoke-ModControlPipe -Command "INJECT multiplayer" -Target manager -TimeoutMs 120000
        Write-SoakStage ("INJECT -> {0}" -f $last)
        if ($last -match '(?i)^(OK|already)') {
            try {
                Wait-ModuleManagerLoadLog -ModuleId "multiplayer" -TimeoutSec 120 | Out-Null
            } catch {
                Write-SoakStage ("WARN Wait-ModuleManagerLoadLog: {0}" -f $_.Exception.Message)
            }
            return $last
        }
        Start-Sleep -Seconds 3
    }
    throw ("lan-soak client: INJECT multiplayer failed after {0} tries: {1}" -f $Retries, $last)
}

function Wait-ClientActivationLive {
    param([string]$LogPath, [int]$TimeoutSec = 180)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    Write-SoakStage ("waiting for activation set live (timeout {0}s)" -f $TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $LogPath) {
            $tail = Get-Content -LiteralPath $LogPath -Tail 120 -ErrorAction SilentlyContinue
            if ($tail -match 'activation set live') {
                Write-SoakStage "activation set live"
                return $true
            }
        }
        Start-Sleep -Seconds 3
    }
    return $false
}

Write-SoakStage ("role={0} room={1} soak={2}m" -f $Role, $Room, $SoakMinutes)
Write-SoakCoord -Status "started"

if ($Role -eq "host") {
    $candidates = Get-LanIPv4Candidates
    if (-not $candidates -or $candidates.Count -eq 0) {
        throw "lan-soak host: no LAN IPv4 found"
    }
    $lanIp = $candidates[0]
    if ($candidates.Count -gt 1) {
        Write-SoakStage ("LAN IPv4 candidates: {0} (using {1})" -f ($candidates -join ", "), $lanIp)
    }
    Write-SoakStage ("advertise PeerIp={0} port={1}" -f $lanIp, $ServerPort)
    $advPath = Write-HostAdvertiseFile -Ip $lanIp -Port $ServerPort -RoomName $Room
    Write-SoakCoord -Status "host-ready" -Extra @{ ip = $lanIp; port = $ServerPort; room = $Room; advertise = $advPath }
    Write-SoakStage "tell peer to run:"
    Write-Host ("  .\tools\mp-lan-dual-soak.ps1 -Role client -PeerIp {0} -Room {1} -SoakMinutes {2}" -f $lanIp, $Room, $SoakMinutes)
    Write-SoakStage ("wrote {0}" -f $advPath)

    if ($ClientGraceSec -gt 0) {
        Write-SoakStage ("client grace {0}s - start client on peer now" -f $ClientGraceSec)
        $graceEnd = (Get-Date).AddSeconds($ClientGraceSec)
        while ((Get-Date) -lt $graceEnd) {
            $left = [Math]::Max(0, [int]($graceEnd - (Get-Date)).TotalSeconds)
            if (($left % 15) -eq 0 -or $left -le 5) {
                Write-SoakStage ("grace left {0}s" -f $left)
            }
            Start-Sleep -Seconds 1
        }
    }

    $realLevel = Join-Path $repoRoot "tools\mp-real-level-bots.ps1"
    if (-not (Test-Path -LiteralPath $realLevel)) { throw "lan-soak: missing $realLevel" }
    $playSec = [Math]::Max(90, $SoakMinutes * 60)
    Write-SoakStage ("starting mp-real-level-bots BotCount=2 PlaySeconds={0}" -f $playSec)
    & $realLevel -BotCount 2 -PlaySeconds $playSec
    $exit = $LASTEXITCODE
    if ($null -eq $exit) { $exit = 0 }
    if ($exit -ne 0) {
        Write-SoakCoord -Status "fail" -Extra @{ exit = $exit }
        throw ("lan-soak host: mp-real-level-bots EXIT={0}" -f $exit)
    }
    Write-SoakCoord -Status "pass"
    Write-SoakStage "host PASS"
    exit 0
}

# --- client ---
if ([string]::IsNullOrWhiteSpace($PeerIp)) {
    if (-not [string]::IsNullOrWhiteSpace($env:ME_LAN_PEER_IP)) {
        $PeerIp = $env:ME_LAN_PEER_IP.Trim()
        Write-SoakStage ("PeerIp from ME_LAN_PEER_IP={0}" -f $PeerIp)
    }
}
if ([string]::IsNullOrWhiteSpace($PeerIp)) {
    throw "lan-soak client: -PeerIp required (host LAN address), or set ME_LAN_PEER_IP"
}

$settingsPath = Write-ClientSettings -Server $PeerIp -RoomName $Room -PlayerName "LanSoakClient"
Write-SoakStage ("wrote {0} server={1} room={2}" -f $settingsPath, $PeerIp, $Room)
Write-SoakCoord -Status "settings-ready" -Extra @{ peerIp = $PeerIp; port = $ServerPort }

$ctx = $null
try {
    $ctx = Start-SplitInjectionSession -RepoRoot $repoRoot -SkipBuild:$SkipBuild -StopExisting
    Assert-ValidHarnessContext -Value $ctx -Scenario "mp-lan-dual-soak-client"

    Wait-ManagerHooksReady -BootNudge -TimeoutSec 180 | Out-Null
    Invoke-EnsureCoreLoaded -LogPath (Get-SafeContextLogPath -Context $ctx) | Out-Null
    Invoke-ModControlPipe -Command "RELOAD_SETTINGS" -Target core -TimeoutMs 20000 | Out-Null

    Enable-HarnessIntroHangImmunity -Seconds 300
    Invoke-GameIntroSkip -MinBootSec 20 -KeepFocused
    Invoke-GameIntroSkipBlind -SkipRounds 12 -KeepFocused
    Wait-GameMainMenuReady -KeepFocused -TimeoutSec 300 -MaxSkipRounds 40 -StablePolls 2 | Out-Null

    Write-SoakStage "START_NEW_GAME"
    $r = Invoke-CoreQ "START_NEW_GAME"
    if ($r -notmatch '^OK') { throw ("START_NEW_GAME failed: {0}" -f $r) }

    $mapReady = $false
    $level = "tutorial_p"
    for ($i = 0; $i -lt 240; $i++) {
        $st = Get-MmultiplayerStatusJson -ErrorAction SilentlyContinue
        $map = ""
        if ($st) {
            if ($st.currentMap) { $map = [string]$st.currentMap }
            elseif ($st.clientMap) { $map = [string]$st.clientMap }
        }
        if ($map -and $map -ne "tdmainmenu" -and $map -ne "gameplay") {
            $level = $map
            $mapReady = $true
            Write-SoakStage ("level={0}" -f $level)
            break
        }
        if (($i % 8) -eq 0) {
            Invoke-CoreQ "INJECT_KEY 0x0D" | Out-Null
            Start-Sleep -Milliseconds 80
            Invoke-CoreQ "INJECT_KEY 0x0D UP" | Out-Null
        }
        Start-Sleep 1
    }
    if (-not $mapReady) { throw "lan-soak client: map never left tdmainmenu" }

    for ($d = 0; $d -lt 16; $d++) {
        foreach ($vk in @(0x1B, 0x0D, 0x20, 0x10)) {
            Invoke-CoreQ ("INJECT_KEY 0x{0:X2}" -f $vk) | Out-Null
            Start-Sleep -Milliseconds 40
            Invoke-CoreQ ("INJECT_KEY 0x{0:X2} UP" -f $vk) | Out-Null
        }
    }

    Write-SoakStage "hooks after map"
    Invoke-CoreQ "ENSURE_GAMEPLAY_HOOKS" | Out-Null
    Invoke-CoreQ "ENSURE_MP_HOOKS" | Out-Null
    Start-Sleep 8

    if (-not $SkipServerWait) {
        Wait-LanServerTcp -TargetHost $PeerIp -Port $ServerPort -TimeoutSec $ServerWaitSec
    } else {
        Write-SoakStage "SkipServerWait set - not probing TCP"
    }

    # Rewrite settings immediately before inject (DLL Init reads file).
    Write-ClientSettings -Server $PeerIp -RoomName $Room -PlayerName "LanSoakClient" | Out-Null
    Invoke-InjectMultiplayerRetry -Retries ([Math]::Max(1, $InjectRetries))
    Start-Sleep 3

    Write-SoakStage "FORCE_HOSTED_LIVE"
    $fh = Invoke-CoreQ "FORCE_HOSTED_LIVE"
    Write-SoakStage ("FORCE_HOSTED_LIVE -> {0}" -f $fh)
    if ($fh -and $fh -notmatch '(?i)OK|live|already') {
        Write-SoakStage ("WARN unexpected FORCE_HOSTED_LIVE reply: {0}" -f $fh)
    }

    $clientLog = Join-Path $env:TEMP "mirroredge-multiplayer-client.log"
    $sawLive = Wait-ClientActivationLive -LogPath $clientLog -TimeoutSec 180
    if (-not $sawLive -and -not $AllowNoLive) {
        Write-SoakCoord -Status "fail" -Extra @{ reason = "no-activation-live" }
        throw "lan-soak client: no 'activation set live' within 180s after FORCE_HOSTED_LIVE (is host server up / same room?)"
    }
    if (-not $sawLive) {
        Write-SoakStage "WARN AllowNoLive: continuing without activation set live"
    }

    Write-SoakCoord -Status "client-ready" -Extra @{ peerIp = $PeerIp; level = $level; sawLive = $sawLive }

    $deadline = (Get-Date).AddMinutes($SoakMinutes)
    $sawPose = $false
    $sawUdpSeq = $false
    $maxRemotes = 0
    Write-SoakStage ("soak until {0:u}" -f $deadline)
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "lan-soak-client" -SkipHangCheck | Out-Null
        if (Test-Path -LiteralPath $clientLog) {
            $tail = Get-Content -LiteralPath $clientLog -Tail 100 -ErrorAction SilentlyContinue
            if ($tail -match 'activation set live') { $sawLive = $true }
            if ($tail -match 'remote pose applied|remote bones applied') { $sawPose = $true }
            if ($tail -match 'udp seq stream') { $sawUdpSeq = $true }
            if ($tail) {
                Write-SoakStage ("log: {0}" -f (($tail | Select-Object -Last 2) -join " | "))
            }
        }
        try {
            $st = Get-MmultiplayerStatusJson
            $rem = 0
            if ($null -ne $st.mpRemotePlayers) { $rem = [int]$st.mpRemotePlayers }
            if ($rem -gt $maxRemotes) { $maxRemotes = $rem }
            Write-SoakStage ("rem={0} sp={1} map={2}" -f $st.mpRemotePlayers, $st.mpSpawnedPlayers, $st.currentMap)
        } catch {}
        Start-Sleep -Seconds 15
    }

    $motionOk = $sawPose -or $sawUdpSeq -or ($maxRemotes -gt 0)
    if (-not $motionOk -and $SoakMinutes -ge 5) {
        Write-SoakCoord -Status "fail" -Extra @{ sawLive = $sawLive; sawPose = $sawPose; sawUdpSeq = $sawUdpSeq; maxRemotes = $maxRemotes }
        throw "lan-soak client: no remote pose/bones/udp-seq/remotes during soak"
    }
    if (-not $sawLive -and -not $AllowNoLive) {
        Write-SoakStage "WARN activation live never observed (should have failed earlier)"
    }

    Write-SoakCoord -Status "pass" -Extra @{
        sawLive    = $sawLive
        sawPose    = $sawPose
        sawUdpSeq  = $sawUdpSeq
        maxRemotes = $maxRemotes
    }
    Write-SoakStage ("client PASS live={0} pose={1} udpSeq={2} maxRem={3}" -f $sawLive, $sawPose, $sawUdpSeq, $maxRemotes)
} finally {
    if ($ctx) {
        try { Complete-SplitInjectionSession -Context $ctx | Out-Null } catch {}
    }
}
exit 0
