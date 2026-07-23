#Requires -Version 5.1
<#
  LAN dual real-client soak (host + client).

  Host:  .\tools\mp-lan-dual-soak.ps1 -Role host -SoakMinutes 15
  Client:.\tools\mp-lan-dual-soak.ps1 -Role client -PeerIp <host-lan-ip> -SoakMinutes 15

  Client automates: Start-SplitInjectionSession -> START_NEW_GAME -> inject MP
  (server=PeerIp) -> FORCE_HOSTED_LIVE -> soak poll on client.log / GET_STATUS.

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
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$lib = Join-Path $repoRoot "tools\debug-harness\lib\DebugHarness.psm1"
Import-Module $lib -Force

function Get-LanIPv4 {
    Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notlike "127.*" -and $_.PrefixOrigin -ne "WellKnown" } |
        Select-Object -ExpandProperty IPAddress -First 1
}

function Write-SoakCoord {
    param([string]$Status, [hashtable]$Extra = @{})
    $data = @{ role = $Role; room = $Room; status = $Status; at = (Get-Date).ToString("o") }
    foreach ($k in $Extra.Keys) { $data[$k] = $Extra[$k] }
    try {
        Write-HarnessCoordination -Status ("lan-soak-{0}-{1}" -f $Role, $Status) -Summary $data
    } catch {
        Write-Host ("lan-soak: WARN coordination write failed ({0})" -f $_.Exception.Message)
    }
}

function Invoke-CoreQ([string]$cmd, [int]$TimeoutMs = 12000) {
    return Invoke-ModControlPipe -Command $cmd -Target core -TimeoutMs $TimeoutMs
}

Write-Host ("lan-soak: role={0} room={1} soak={2}m" -f $Role, $Room, $SoakMinutes)
Write-SoakCoord -Status "started"

if ($Role -eq "host") {
    $lanIp = Get-LanIPv4
    if (-not $lanIp) { throw "lan-soak host: no LAN IPv4 found" }
    Write-Host ("lan-soak host: advertise PeerIp={0} port={1}" -f $lanIp, $ServerPort)
    Write-SoakCoord -Status "host-ready" -Extra @{ ip = $lanIp; port = $ServerPort; room = $Room }
    Write-Host "lan-soak host: tell peer to run:"
    Write-Host ("  .\tools\mp-lan-dual-soak.ps1 -Role client -PeerIp {0} -Room {1} -SoakMinutes {2}" -f $lanIp, $Room, $SoakMinutes)
    $realLevel = Join-Path $repoRoot "tools\mp-real-level-bots.ps1"
    if (-not (Test-Path -LiteralPath $realLevel)) { throw "lan-soak: missing $realLevel" }
    $playSec = [Math]::Max(90, $SoakMinutes * 60)
    & $realLevel -BotCount 2 -PlaySeconds $playSec
    $exit = $LASTEXITCODE
    if ($null -eq $exit) { $exit = 0 }
    if ($exit -ne 0) {
        Write-SoakCoord -Status "fail" -Extra @{ exit = $exit }
        throw ("lan-soak host: mp-real-level-bots EXIT={0}" -f $exit)
    }
    Write-SoakCoord -Status "pass"
    Write-Host "lan-soak host: PASS"
    exit 0
}

if ([string]::IsNullOrWhiteSpace($PeerIp)) {
    throw "lan-soak client: -PeerIp required (host LAN address)"
}

$settingsPath = Join-Path $env:TEMP "multiplayer.settings"
$jsonObj = [ordered]@{
    client = [ordered]@{
        server = $PeerIp
        room   = $Room
        name   = "LanSoakClient"
    }
}
($jsonObj | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $settingsPath -Encoding ASCII
Write-Host ("lan-soak client: wrote {0} server={1} room={2}" -f $settingsPath, $PeerIp, $Room)
Write-SoakCoord -Status "settings-ready" -Extra @{ peerIp = $PeerIp }

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

    Write-Host "lan-soak client: START_NEW_GAME"
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
            Write-Host ("lan-soak client: level={0}" -f $level)
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

    Write-Host "lan-soak client: hooks after map"
    Invoke-CoreQ "ENSURE_GAMEPLAY_HOOKS" | Out-Null
    Invoke-CoreQ "ENSURE_MP_HOOKS" | Out-Null
    Start-Sleep 8

    # Rewrite settings again immediately before inject (DLL Init reads file).
    ($jsonObj | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $settingsPath -Encoding ASCII
    $inj = Invoke-ModControlPipe -Command "INJECT multiplayer" -Target manager -TimeoutMs 120000
    Write-Host ("lan-soak client: INJECT multiplayer -> {0}" -f $inj)
    Wait-ModuleManagerLoadLog -ModuleId "multiplayer" -TimeoutSec 120 | Out-Null
    Start-Sleep 3

    Write-Host "lan-soak client: FORCE_HOSTED_LIVE"
    Invoke-CoreQ "FORCE_HOSTED_LIVE" | Out-Null
    Write-SoakCoord -Status "client-ready" -Extra @{ peerIp = $PeerIp; level = $level }

    $clientLog = Join-Path $env:TEMP "mirroredge-multiplayer-client.log"
    $deadline = (Get-Date).AddMinutes($SoakMinutes)
    $sawPose = $false
    $sawLive = $false
    Write-Host ("lan-soak client: soak until {0:u}" -f $deadline)
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "lan-soak-client" -SkipHangCheck | Out-Null
        if (Test-Path -LiteralPath $clientLog) {
            $tail = Get-Content -LiteralPath $clientLog -Tail 80 -ErrorAction SilentlyContinue
            if ($tail -match 'activation set live') { $sawLive = $true }
            if ($tail -match 'remote pose applied|remote bones applied') { $sawPose = $true }
            if ($tail) {
                Write-Host ("lan-soak client: log: {0}" -f (($tail | Select-Object -Last 2) -join " | "))
            }
        }
        try {
            $st = Get-MmultiplayerStatusJson
            Write-Host ("lan-soak client: rem={0} sp={1} map={2}" -f $st.mpRemotePlayers, $st.mpSpawnedPlayers, $st.currentMap)
        } catch {}
        Start-Sleep -Seconds 15
    }

    if (-not $sawLive) {
        Write-Host "lan-soak client: WARN no 'activation set live' in client.log (peer may not have joined)"
    }
    if (-not $sawPose -and $SoakMinutes -ge 5) {
        throw "lan-soak client: no remote pose/bones in client.log during soak"
    }
    Write-SoakCoord -Status "pass" -Extra @{ sawLive = $sawLive; sawPose = $sawPose }
    Write-Host "lan-soak client: PASS"
} finally {
    if ($ctx) {
        try { Complete-SplitInjectionSession -Context $ctx | Out-Null } catch {}
    }
}
exit 0