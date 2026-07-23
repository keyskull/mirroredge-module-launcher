#Requires -Version 5.1
<#
  LAN dual real-client soak (host + client roles).

  Interim mesh/motion gate remains: tools/mp-real-level-bots.ps1
  This script coordinates two real MirrorsEdge.exe peers for soak.

  Host:
    .\tools\mp-lan-dual-soak.ps1 -Role host -SoakMinutes 15
  Client:
    .\tools\mp-lan-dual-soak.ps1 -Role client -PeerIp 192.168.x.x -SoakMinutes 15

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
    try { Write-HarnessCoordination -Status ("lan-soak-{0}-{1}" -f $Role, $Status) -Summary $data }
    catch { Write-Host ("lan-soak: WARN coordination write failed ({0})" -f $_.Exception.Message) }
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
    & $realLevel -BotCount 2 -PlaySeconds $playSec -SkipBuild:$SkipBuild
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
$json = @"
{
  "client": {
    "server": "$PeerIp",
    "room": "$Room",
    "name": "LanSoakClient"
  }
}
"@
Set-Content -LiteralPath $settingsPath -Value $json -Encoding UTF8
Write-Host ("lan-soak client: wrote {0} server={1} room={2}" -f $settingsPath, $PeerIp, $Room)
Write-SoakCoord -Status "settings-ready" -Extra @{ peerIp = $PeerIp }
Write-Host "lan-soak client: manual steps (automated dual-launch TBD):"
Write-Host "  1. Deploy same build (.\build.ps1)."
Write-Host "  2. Launch game; inject core + multiplayer."
Write-Host ("  3. Confirm server={0} room={1}." -f $PeerIp, $Room)
Write-Host "  4. START_NEW_GAME / Story -> tutorial_p; Set Gameplay."
Write-Host "  5. Watch %TEMP%\mirroredge-multiplayer-client.log for spawn ok + remote pose."
Write-Host ("  6. Soak {0}m; on crash run collect-diagnostics.ps1." -f $SoakMinutes)

$deadline = (Get-Date).AddMinutes($SoakMinutes)
Write-SoakCoord -Status "client-ready" -Extra @{ peerIp = $PeerIp }
Write-Host ("lan-soak client: soak until {0:u}" -f $deadline)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 30
    $log = Join-Path $env:TEMP "mirroredge-multiplayer-client.log"
    if (Test-Path -LiteralPath $log) {
        $tail = Get-Content -LiteralPath $log -Tail 3 -ErrorAction SilentlyContinue
        if ($tail) { Write-Host ("lan-soak client: log tail: {0}" -f ($tail -join " | ")) }
    }
}
Write-SoakCoord -Status "pass"
Write-Host "lan-soak client: soak window finished (verify runbook 8 gates)"
exit 0
