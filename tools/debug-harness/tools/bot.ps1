#Requires -Version 5.1
<#
  Multiplayer Bot Client
  Connects to the multiplayer server (default 176.58.101.83:5222) and simulates
  a remote player. Works against the public internet server or multiplayer-server.exe.

  Protocol:
    TCP/5222 → JSON control (connect, ping/pong, disconnect)
    UDP/5222 → binary PACKET_COMPRESSED position/bone data
#>
param(
    [string]$Server = "127.0.0.1",
    [int]$Port = 5222,
    [string]$Name = "Bot-001",
    [string]$Room = "playthrough-lobby",
    [int]$Character = 0,   # 0=Faith, 1=Kate, ...
    [string]$Level = "gameplay",
    [int]$RunSeconds = 0,  # 0 = run until killed
    [switch]$KeepAlive,     # stay connected indefinitely
    # Follow defaults ON (chase host UDP / TargetFile). Harness may still
    # pass -Follow (accepted). Use -NoFollow for the old orbit-at-(500,500) demo.
    [switch]$Follow,
    [switch]$NoFollow,
    [string]$TargetFile = "", # JSON {"x","y","z","yaw"}; default TEMP file if present
    [double]$FollowDistance = 220.0,
    [double]$FollowSpeed = 900.0,
    [double]$FollowLateral = [double]::NaN,  # NaN = derive from bot slot
    [double]$StartX = [double]::NaN,
    [double]$StartY = [double]::NaN,
    [double]$StartZ = [double]::NaN,
    # Bone snapshot for TransformBones (host dumps %%TEMP%%\mirroredge-host-bones.bin).
    # Default: load that file when present. -NoBones forces zero compressed bones.
    [string]$BoneFile = "",
    [switch]$NoBones,
    # When moving (Follow chase), cycle host bone frames or lightly modulate
    # a few bone shorts so the mesh is not a frozen T-pose clone.
    [switch]$AnimateBones,
    # Multi-frame walk cycle (host dumps %%TEMP%%\mirroredge-host-bones-cycle.bin).
    [string]$BoneCycleFile = "",
    # B0/B1 harness: SoftProbe starts Tag and waves at Follow peer (TCP only).
    [switch]$StartTag,
    [switch]$SendInteract,
    # Physics probe: hover high then resume Follow (host floor-clamps remote).
    [switch]$PhysicsFallDrop,
    # Physics probe: after fall phase, teleport past host into +X (wall clamp).
    [switch]$PhysicsWallSlam,
    # Delay FallDrop/WallSlam so SoftProbe can softColl/Tag near host first (V5).
    [int]$PhysicsProbeDelayMs = 0
)

$ErrorActionPreference = "Stop"

# Unique default name — bare "Bot-001" for every instance made all nametags
# identical and made manual multi-bot tests look like ghost duplicates.
if ($Name -eq "Bot-001") {
    $Name = "Bot-C{0}-P{1}" -f $Character, $PID
}

# Default follow ON unless -NoFollow (bare bot.ps1 used to orbit at 500,500
# and never appear near the player).
$DoFollow = -not $NoFollow

# Harness / mp-real-level-bots write this; pick it up automatically when
# the user launches bot.ps1 by hand without -TargetFile.
if (-not $TargetFile) {
    $defaultTarget = Join-Path $env:TEMP "mirroredge-bot-target.json"
    if (Test-Path -LiteralPath $defaultTarget) {
        $TargetFile = $defaultTarget
    }
}

# ── CompressedBoneOffsets from multiplayer client.h (329 entries) ──
$CompressedBoneOffsets = @(
    0x14,0x20,0x24,0x28,0x2C,0x30,0x34,0x38,0x40,0x44,0x48,
    0x4C,0x50,0x54,0x58,0x60,0x64,0x68,0x6C,0x70,0x74,0x78,
    0x80,0x84,0x88,0x8C,0x90,0x94,0x98,0xA0,0xA4,0xA8,0xAC,
    0xC0,0xC4,0xC8,0xCC,0xD0,0xD4,0xD8,0x1E4,0x204,0x208,0x20C,
    0x210,0x214,0x224,0x228,0x230,0x238,0x244,0x260,0x268,0x26C,0x280,
    0x284,0x288,0x28C,0x290,0x2A0,0x2A8,0x2AC,0x2B0,0x2BC,0x2D0,0x2D4,
    0x2D8,0x2E0,0x2E4,0x2F0,0x2F4,0x2F8,0x3E0,0x3E4,0x3E8,0x3EC,0x3F0,
    0x3F4,0x3F8,0x400,0x404,0x408,0x40C,0x410,0x414,0x418,0x440,0x444,
    0x448,0x44C,0x450,0x454,0x458,0x460,0x464,0x468,0x46C,0x470,0x474,
    0x478,0x4E0,0x4E4,0x4E8,0x4EC,0x4F0,0x4F4,0x4F8,0x550,0x554,0x558,
    0x5A0,0x5A4,0x5A8,0x5AC,0x5C0,0x5C4,0x5C8,0x5CC,0x5E0,0x5E4,0x5E8,
    0x5EC,0x600,0x604,0x608,0x60C,0x644,0x648,0x660,0x664,0x668,0x66C,
    0x684,0x688,0x68C,0x6A4,0x6A8,0x6AC,0x6C4,0x6C8,0x6E0,0x6E4,0x6E8,
    0x6EC,0x704,0x708,0x70C,0x724,0x728,0x72C,0x744,0x748,0x74C,0x760,
    0x764,0x768,0x76C,0x784,0x788,0x78C,0x7A4,0x7A8,0x7AC,0x7C4,0x7C8,
    0x7E0,0x7E4,0x7E8,0x7EC,0x804,0x808,0x80C,0x824,0x828,0x82C,0x840,
    0x844,0x848,0x84C,0x860,0x864,0x868,0x86C,0x888,0x88C,0x8A0,0x8AC,
    0x8C0,0x8C4,0x8C8,0x8CC,0x8E0,0x8E4,0x8E8,0x8EC,0x900,0x904,0x908,
    0x90C,0x920,0x924,0x928,0x92C,0x940,0x944,0x948,0x94C,0x960,0x964,
    0x968,0x96C,0x970,0x974,0x978,0x980,0x984,0x988,0x98C,0x9A0,0x9A4,
    0x9A8,0x9AC,0x9C8,0x9CC,0x9E8,0x9EC,0xA00,0xA04,0xA08,0xA20,0xA24,
    0xA28,0xA2C,0xA48,0xA4C,0xA68,0xA6C,0xA80,0xA84,0xA88,0xAA0,0xAA4,
    0xAA8,0xAAC,0xAC8,0xACC,0xAE8,0xAEC,0xB00,0xB04,0xB08,0xB0C,0xB20,
    0xB24,0xB28,0xB2C,0xB48,0xB4C,0xB68,0xB6C,0xB80,0xB84,0xB88,0xB8C,
    0xBA0,0xBA4,0xBA8,0xBAC,0xBC8,0xBCC,0xBE0,0xBE4,0xBE8,0xBEC,0xBF0,
    0xBF4,0xBF8,0xC00,0xC04,0xC08,0xC0C,0xC20,0xC24,0xC28,0xC2C,0xC40,
    0xC44,0xC48,0xC4C,0xC60,0xC64,0xC68,0xC6C,0xC70,0xC74,0xC78,0xC80,
    0xC84,0xC88,0xC8C,0xC90,0xC94,0xC98,0xCA0,0xCAC,0xCC0,0xCC4,0xCC8,
    0xCCC,0xCE0,0xCE4,0xCE8,0xCEC,0xD00,0xD04,0xD08,0xD0C,0xD14,0xD20,
    0xD24,0xD28,0xD2C,0xD34,0xD40,0xD4C,0xD60,0xD64,0xD68,0xD6C
)

$BoneCount = $CompressedBoneOffsets.Count
$PacketSize = 692  # +Seq; server accepts 676-692
$PacketSizeLegacy = 676
$PacketSizeVelocity = 688
$PacketSizeMove = 690

if (-not $BoneFile -and -not $NoBones) {
    $defaultBones = Join-Path $env:TEMP "mirroredge-host-bones.bin"
    if (Test-Path -LiteralPath $defaultBones) {
        $BoneFile = $defaultBones
    }
}
if (-not $BoneCycleFile -and -not $NoBones) {
    $BoneCycleFile = Join-Path $env:TEMP "mirroredge-host-bones-cycle.bin"
}
# Named Mesh3p clip library (V14) — Idle / Walking / Falling MEBC files.
$BoneClipDir = $env:TEMP
$BoneClipPaths = @{
    Idle     = (Join-Path $BoneClipDir "mirroredge-bone-clip-Idle.bin")
    Walking  = (Join-Path $BoneClipDir "mirroredge-bone-clip-Walking.bin")
    Falling  = (Join-Path $BoneClipDir "mirroredge-bone-clip-Falling.bin")
}

# ── Embed C# for socket I/O + binary struct serialization ──
if (-not ("MirrorBot" -as [type])) {
    Add-Type @"
using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

public static class MirrorBot {
    public const int PacketSize = 692;
    public const int PacketSizeLegacy = 676;
    public const int PacketSizeVelocity = 688;
    public const int PacketSizeMove = 690;
    static ushort s_seq = 0;

    public static byte[] BuildPacket(uint id, float x, float y, float z, ushort yaw, short[] bones, float vx, float vy, float vz, byte movementState = 0, byte physics = 0) {
        byte[] buf = new byte[PacketSize];
        int off = 0;
        BitConverter.GetBytes(id).CopyTo(buf, off); off += 4;
        BitConverter.GetBytes(x).CopyTo(buf, off); off += 4;
        BitConverter.GetBytes(y).CopyTo(buf, off); off += 4;
        BitConverter.GetBytes(z).CopyTo(buf, off); off += 4;
        BitConverter.GetBytes(yaw).CopyTo(buf, off); off += 2;
        int boneCount = Math.Min(bones.Length, (PacketSizeLegacy - off) / 2);
        for (int i = 0; i < boneCount; i++) {
            BitConverter.GetBytes(bones[i]).CopyTo(buf, off); off += 2;
        }
        off = PacketSizeLegacy;
        BitConverter.GetBytes(vx).CopyTo(buf, off); off += 4;
        BitConverter.GetBytes(vy).CopyTo(buf, off); off += 4;
        BitConverter.GetBytes(vz).CopyTo(buf, off); off += 4;
        // MovementState + Physics (B3-lite). Bots set from motion heuristic.
        buf[off++] = movementState;
        buf[off++] = physics;
        s_seq++;
        BitConverter.GetBytes(s_seq).CopyTo(buf, off);
        return buf;
    }

    public static bool TryParsePacket(byte[] buf, int length, out uint id, out float x, out float y, out float z, out ushort yaw) {
        id = 0; x = 0; y = 0; z = 0; yaw = 0;
        if (length < 18) return false;
        id = BitConverter.ToUInt32(buf, 0);
        x = BitConverter.ToSingle(buf, 4);
        y = BitConverter.ToSingle(buf, 8);
        z = BitConverter.ToSingle(buf, 12);
        yaw = BitConverter.ToUInt16(buf, 16);
        return true;
    }

    public static long GetTimestamp() {
        return (long)Environment.TickCount;
    }
}
"@
}

# ── Bot main ──
Write-Host ""
Write-Host "============================================"
Write-Host "  Mirror Bot Client"
Write-Host "============================================"
Write-Host "  Server : ${Server}:${Port}"
Write-Host "  Name   : $Name"
Write-Host "  Room   : $Room"
Write-Host "  Char   : $Character"
Write-Host "  Level  : $Level"
if ($DoFollow) {
    Write-Host "  Follow : on (dist=$FollowDistance)"
    if ($TargetFile) { Write-Host "  Target : $TargetFile" }
    else { Write-Host "  Target : (UDP host pose; no TargetFile)" }
} else {
    Write-Host "  Follow : off (orbit demo at ~500,500 - far from player)"
}
if ($NoBones) {
    Write-Host "  Bones  : off (zero compressed = rest pose, no TransformBones)"
} elseif ($BoneFile) {
    Write-Host "  Bones  : $BoneFile"
    if ($AnimateBones) { Write-Host "  Animate: on (cycle or modulate while moving)" }
    if ($BoneCycleFile) { Write-Host "  Cycle  : $BoneCycleFile" }
} else {
    Write-Host "  Bones  : none yet (host dumps mirroredge-host-bones.bin after Set Gameplay)"
}
Write-Host "============================================"
Write-Host ""

# Connect TCP
$tcp = New-Object System.Net.Sockets.TcpClient
try {
    Write-Host "[bot] Connecting to ${Server}:${Port}..."
    $tcp.Connect($Server, $Port)
    $tcp.NoDelay = $true
    $tcpSocket = $tcp.Client

    function Send-TcpJsonMessage {
        param([string]$Json)
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Json + [char]0)
        [void]$tcpSocket.Send($bytes)
    }

    $script:tcpRecvPending = ""

    function Append-TcpReceive {
        param([int]$BytesRead)
        $parsed = @()
        if ($BytesRead -le 0) {
            return $parsed
        }

        $script:tcpRecvPending += [System.Text.Encoding]::UTF8.GetString($recvBuf, 0, $BytesRead)
        while ($script:tcpRecvPending.Contains([char]0)) {
            $idx = $script:tcpRecvPending.IndexOf([char]0)
            $raw = $script:tcpRecvPending.Substring(0, $idx)
            $script:tcpRecvPending = $script:tcpRecvPending.Substring($idx + 1)
            if (-not $raw) {
                continue
            }

            try {
                $parsed += ,($raw | ConvertFrom-Json)
            } catch {
                Write-Host "[bot] WARN: couldn't parse response: $_"
            }
        }

        return $parsed
    }

    Write-Host "[bot] TCP connected"
} catch {
    Write-Host "[bot] ERROR: TCP connect failed: $_"
    exit 1
}

# Open UDP
$udp = New-Object System.Net.Sockets.UdpClient
try {
    $udp.Connect($Server, $Port)
    Write-Host "[bot] UDP ready"
} catch {
    Write-Host "[bot] ERROR: UDP setup failed: $_"
    $tcp.Close()
    exit 1
}

# Send connect message
$connectJson = (@{
    type      = "connect"
    room      = $Room
    name      = $Name
    level     = $Level
    character = $Character
} | ConvertTo-Json -Compress)

Write-Host "[bot] Sending connect: $connectJson"
Send-TcpJsonMessage -Json $connectJson

$script:knownPeerIds = New-Object 'System.Collections.Generic.List[uint32]'
# id -> @{ x; y; z; ms } from UDP — SoftProbe interact must pick nearest (host),
# not followTargetId (often a stand-off Cam rejected by server distance gate).
$script:peerPos = @{}

# Wait for ID response
$recvBuf = New-Object byte[] 4096
$retries = 0
$botId = [UInt32]0
while ($retries -lt 50) {
    try {
        $n = $tcpSocket.Receive($recvBuf, 0, $recvBuf.Length, [System.Net.Sockets.SocketFlags]::None)
        foreach ($json in (Append-TcpReceive -BytesRead $n)) {
            Write-Host "[bot] Received: $($json | ConvertTo-Json -Compress)"
            if ($json.'type' -eq "id") {
                $botId = [UInt32]$json.id
                Write-Host "[bot] Assigned ID: $botId"
            } elseif ($json.'type' -eq "connect" -and $null -ne $json.id) {
                $pid = [uint32]$json.id
                if ($pid -ne 0 -and -not $script:knownPeerIds.Contains($pid)) {
                    $script:knownPeerIds.Add($pid) | Out-Null
                }
            }
        }
        if ($botId -ne 0) {
            break
        }
    } catch {
        Start-Sleep -Milliseconds 200
        $retries++
    }
}

if ($botId -eq 0) {
    Write-Host "[bot] ERROR: did not receive ID assignment"
    $tcp.Close()
    exit 1
}

# ── Main loop: send position updates + handle pings ──
Write-Host "[bot] Starting position broadcast (60 Hz)..."
$deadline = if ($RunSeconds -gt 0) { (Get-Date).AddSeconds($RunSeconds) } else { [DateTime]::MaxValue }
$lastPing = [MirrorBot]::GetTimestamp()
# NEVER offset by raw botId (UInt32 ~1e9) - that teleports actors to
# astronomical coords and the host then loses the Actor pointer.
$slot = [Math]::Abs([int]$Character) % 16
if (-not [double]::IsNaN($StartX)) { $posX = [float]$StartX }
else { $posX = 500.0 + ($slot * 100.0) }
if (-not [double]::IsNaN($StartY)) { $posY = [float]$StartY }
else { $posY = 500.0 + ($slot * 50.0) }
# Menu FORCE_HOSTED_LIVE used Z~300; tutorial rooftop is much higher.
# Follow/TargetFile should override once host pos is available.
if (-not [double]::IsNaN($StartZ)) { $posZ = [float]$StartZ }
elseif ($Level -match 'tutorial') { $posZ = 2600.0 }
else { $posZ = 300.0 }
Write-Host ("[bot] Initial pos=({0:n0},{1:n0},{2:n0})" -f $posX,$posY,$posZ)
$yaw = (Get-Random -Minimum 0 -Maximum 65535)
$tick = 0
$connected = $true

# Default bones (all zeros = idle/rest; client skips TransformBones)
$bones = [Int16[]]::new($BoneCount)
$baseBones = [Int16[]]::new($BoneCount)
$bonesLoaded = $false
$lastBoneReloadMs = 0
$boneCycle = $null
$boneClips = @{
    Idle    = $null
    Walking = $null
    Falling = $null
}
$lastCycleReloadMs = 0
$loggedBoneCycle = $false
$loggedClipLibrary = $false
$loggedCyclePlayback = $false
$cyclePhase = 0.0
$activeClipName = "Idle"
$clipBlendFrom = $null
$clipBlendRemainMs = 0
$clipBlendDurationMs = 180
$prevClipFrameCount = 16
$lastCycleFrame = $null

function Read-HostBoneSnapshot {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
        $need = $BoneCount * 2
        if ($bytes.Length -lt $need) { return $null }
        $out = [Int16[]]::new($BoneCount)
        for ($i = 0; $i -lt $BoneCount; $i++) {
            $out[$i] = [BitConverter]::ToInt16($bytes, $i * 2)
        }
        return $out
    } catch {
        return $null
    }
}

function Read-HostBoneCycle {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -lt 16) { return $null }
        $magic = [Text.Encoding]::ASCII.GetString($bytes, 0, 4)
        if ($magic -ne 'MEBC') { return $null }
        $version = [BitConverter]::ToUInt16($bytes, 4)
        $boneCount = [BitConverter]::ToUInt16($bytes, 6)
        $frameCount = [BitConverter]::ToUInt16($bytes, 8)
        $frameBytes = [BitConverter]::ToUInt16($bytes, 10)
        if ($boneCount -ne $BoneCount -or $frameBytes -ne ($BoneCount * 2)) { return $null }
        if ($frameCount -lt 1 -or $frameCount -gt 16) { return $null }
        $hdrSize = if ($version -ge 2) { 20 } else { 16 }
        $need = $hdrSize + ($frameCount * $frameBytes)
        if ($bytes.Length -lt $need) { return $null }
        $baseTickMs = [uint32]0
        if ($version -ge 2) {
            $baseTickMs = [BitConverter]::ToUInt32($bytes, 16)
        }
        $frames = New-Object System.Collections.Generic.List[Object]
        for ($f = 0; $f -lt $frameCount; $f++) {
            $off = $hdrSize + ($f * $frameBytes)
            $frame = [Int16[]]::new($BoneCount)
            for ($i = 0; $i -lt $BoneCount; $i++) {
                $frame[$i] = [BitConverter]::ToInt16($bytes, $off + ($i * 2))
            }
            [void]$frames.Add($frame)
        }
        $relMs = [uint16[]]::new($frameCount)
        if ($version -ge 2 -and $bytes.Length -ge ($need + ($frameCount * 2))) {
            $relOff = $need
            for ($f = 0; $f -lt $frameCount; $f++) {
                $relMs[$f] = [BitConverter]::ToUInt16($bytes, $relOff + ($f * 2))
            }
        } else {
            for ($f = 0; $f -lt $frameCount; $f++) {
                $relMs[$f] = [uint16]($f * 40)
            }
        }
        $loopMs = [int]$relMs[$frameCount - 1]
        if ($loopMs -lt ($frameCount * 20)) { $loopMs = $frameCount * 40 }
        return @{
            FrameCount = [int]$frameCount
            Frames     = $frames.ToArray()
            Version    = [int]$version
            BaseTickMs = [uint32]$baseTickMs
            FrameRelMs = $relMs
            LoopMs     = [int]$loopMs
        }
    } catch {
        return $null
    }
}

function Update-BotBones {
    param(
        [Int16[]]$Base,
        [bool]$Moving,
        [double]$Phase,
        [Int16[]]$CycleFrame = $null,
        [bool]$AllowSinFallback = $true
    )
    # Mesh V13: host Mesh3p snapshot/cycle only — no synthetic slot nudges.
    if ($null -ne $CycleFrame) {
        return [Int16[]]$CycleFrame.Clone()
    }
    $out = [Int16[]]::new($BoneCount)
    [Array]::Copy($Base, $out, $BoneCount)
    return $out
}

if (-not $NoBones -and $BoneFile) {
    $loaded = Read-HostBoneSnapshot -Path $BoneFile
    if ($null -ne $loaded) {
        [Array]::Copy($loaded, $baseBones, $BoneCount)
        [Array]::Copy($loaded, $bones, $BoneCount)
        $bonesLoaded = $true
        $nz = ($baseBones | Where-Object { $_ -ne 0 }).Count
        Write-Host "[bot] Loaded bone snapshot ($nz non-zero shorts) from $BoneFile"
    } else {
        Write-Host "[bot] BoneFile unreadable or short: $BoneFile (sending zeros)"
    }
}

function Read-FollowTarget {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path $Path)) { return $null }
    try {
        # Allow harness to rewrite the file while bots hold a reader open.
        $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $sr = New-Object System.IO.StreamReader($fs)
            try {
                $raw = $sr.ReadToEnd()
            } finally {
                $sr.Dispose()
            }
        } finally {
            $fs.Dispose()
        }
        if (-not $raw) { return $null }
        $obj = $raw | ConvertFrom-Json
        if ($null -eq $obj -or $null -eq $obj.x) { return $null }
        # Ignore origin / pre-seed heartbeats - host UDP used to send (0,0,0)
        # and bots snapped stand-off near world origin (invisible).
        if ([Math]::Abs([double]$obj.x) + [Math]::Abs([double]$obj.y) + [Math]::Abs([double]$obj.z) -lt 100.0) {
            return $null
        }
        return $obj
    } catch {
        return $null
    }
}

function Get-StandOffTarget {
    param(
        [double]$HostX, [double]$HostY, [double]$HostZ,
        [int]$HostYaw,
        [double]$Distance,
        [double]$Lateral
    )
    # UE3 yaw: 65536 = 360deg; 0 looks along +X.
    $rad = ($HostYaw / 65536.0) * 2.0 * [Math]::PI
    $fx = [Math]::Cos($rad)
    $fy = [Math]::Sin($rad)
    $rx = -[Math]::Sin($rad)
    $ry = [Math]::Cos($rad)
    return @{
        x = $HostX + $fx * $Distance + $rx * $Lateral
        y = $HostY + $fy * $Distance + $ry * $Lateral
        z = $HostZ
    }
}

function Update-FollowPosition {
    param(
        [ref]$PosX, [ref]$PosY, [ref]$PosZ, [ref]$Yaw,
        [double]$TargetX, [double]$TargetY, [double]$TargetZ,
        [double]$DeltaSec
    )

    # Snap when far - slow chase left bots off-camera (spawn pose stuck at
    # initial 500,500,z while host is on tutorial rooftop ~5854 Z).
    $dx = $TargetX - $PosX.Value
    $dy = $TargetY - $PosY.Value
    $dz = $TargetZ - $PosZ.Value
    $dist = [Math]::Sqrt($dx * $dx + $dy * $dy + $dz * $dz)
    if ($dist -lt 8.0) { return }

    if ($dist -gt 350.0) {
        $PosX.Value = [float]$TargetX
        $PosY.Value = [float]$TargetY
        $PosZ.Value = [float]$TargetZ
    } else {
        $moveDist = [Math]::Min($FollowSpeed * $DeltaSec, $dist)
        $scale = $moveDist / $dist
        $PosX.Value = [float]($PosX.Value + $dx * $scale)
        $PosY.Value = [float]($PosY.Value + $dy * $scale)
        $PosZ.Value = [float]($PosZ.Value + $dz * $scale)
    }

    if ([Math]::Abs($dx) + [Math]::Abs($dy) -gt 1.0) {
        $angle = [Math]::Atan2($dy, $dx) * 180.0 / [Math]::PI
        if ($angle -lt 0) { $angle += 360.0 }
        $Yaw.Value = [UInt16](($angle / 360.0) * 65536.0)
    }
}

$followTargetId = 0
$followTargetScore = 0.0
$followTargetLastSeenMs = 0
$lastFollowLogMs = 0
$tagStarted = $false
$tagStartAfterMs = [MirrorBot]::GetTimestamp() + 2500
$lastInteractMs = 0
$waveUntilMs = 0
$physicsProbeStartMs = [MirrorBot]::GetTimestamp()
$loggedFallDrop = $false
$loggedWallSlam = $false

function Add-KnownPeerId([uint32]$Id) {
    if ($Id -eq 0 -or $Id -eq $botId) { return }
    if (-not $script:knownPeerIds.Contains($Id)) {
        $script:knownPeerIds.Add($Id) | Out-Null
    }
}

function Get-InteractTargetId {
    # Prefer nearest live UDP peer within ~3.5m (server InteractMaxMeters=3.0).
    # Do NOT fall back to arbitrary TCP peers — that made SoftProbe "wave you"
    # while FallDrop/WallSlam (or far Cam bots) sat through walls / out of LOS.
    $bestId = [uint32]0
    $bestD2 = [double]::MaxValue
    $maxD2 = 350.0 * 350.0
    $now = [MirrorBot]::GetTimestamp()
    foreach ($key in @($script:peerPos.Keys)) {
        $id = [uint32]$key
        if ($id -eq 0 -or $id -eq $botId) { continue }
        $p = $script:peerPos[$key]
        if (($now - [int64]$p.ms) -gt 5000) { continue }
        $dx = [double]$p.x - [double]$posX
        $dy = [double]$p.y - [double]$posY
        $dz = [double]$p.z - [double]$posZ
        $d2 = $dx * $dx + $dy * $dy + $dz * $dz
        if ($d2 -lt $bestD2) {
            $bestD2 = $d2
            $bestId = $id
        }
    }
    if ($bestId -ne 0 -and $bestD2 -le $maxD2) {
        return @{ Id = $bestId; DistM = [Math]::Sqrt($bestD2) / 100.0 }
    }
    return @{ Id = [uint32]0; DistM = 0.0 }
}

$lastTickMs = [MirrorBot]::GetTimestamp()

function Get-PoseScore {
    param([double]$X, [double]$Y, [double]$Z)
    return [Math]::Abs($X) + [Math]::Abs($Y) + [Math]::Abs($Z)
}

while ($connected -and (Get-Date) -lt $deadline) {
    $tick++
    $nowMs = [MirrorBot]::GetTimestamp()
    $deltaSec = [Math]::Max(0.001, ($nowMs - $lastTickMs) / 1000.0)
    $lastTickMs = $nowMs

    # Check for incoming TCP messages (pings, disconnect, etc.)
    if ($tcp.Client.Poll(0, [System.Net.Sockets.SelectMode]::SelectRead)) {
        try {
            $n = $tcpSocket.Receive($recvBuf, 0, $recvBuf.Length, [System.Net.Sockets.SocketFlags]::None)
            if ($n -le 0) {
                Write-Host "[bot] Server closed connection"
                $connected = $false
                break
            }
            foreach ($json in (Append-TcpReceive -BytesRead $n)) {
                if ($json.'type' -eq "ping") {
                    # Respond with pong
                    $pong = (@{type="pong"; id=$botId} | ConvertTo-Json -Compress)
                    Send-TcpJsonMessage -Json $pong
                    $lastPing = [MirrorBot]::GetTimestamp()
                } elseif ($json.'type' -eq "disconnect") {
                    Write-Host "[bot] Received disconnect from server"
                    $connected = $false
                    break
                } elseif ($json.'type' -eq "connect") {
                    $otherName = if ($json.name) { $json.name } else { "Unknown" }
                    Write-Host "[bot] Player joined: $otherName"
                    if ($null -ne $json.id) {
                        Add-KnownPeerId ([uint32]$json.id)
                    }
                } elseif ($json.'type' -eq "level") {
                    if ($null -ne $json.id) {
                        Add-KnownPeerId ([uint32]$json.id)
                    }
                    $lvl = [string]$json.level
                    if ($lvl -and $lvl -ne $Level) {
                        $Level = $lvl.ToLower()
                        Write-Host "[bot] Level sync -> $Level"
                        $levelMsg = (@{type="level"; id=$botId; level=$Level} | ConvertTo-Json -Compress)
                        Send-TcpJsonMessage -Json $levelMsg
                    }
                }
            }
        } catch {
            $connected = $false
            break
        }
    }

    # Follow target: harness file and/or UDP relay from server
    $hostX = $null
    $hostY = $null
    $hostZ = $null
    $hostYaw = 0
    $prevPosX = $posX
    $prevPosY = $posY
    $prevPosZ = $posZ

    if ($DoFollow -and $TargetFile) {
        $fileTarget = Read-FollowTarget -Path $TargetFile
        if ($fileTarget) {
            $hostX = [double]$fileTarget.x
            $hostY = [double]$fileTarget.y
            $hostZ = [double]$fileTarget.z
            if ($null -ne $fileTarget.yaw) { $hostYaw = [int]$fileTarget.yaw }
        }
    }

    if ($DoFollow) {
        # Drain UDP. Prefer TargetFile host when present — peer score>4000 used
        # to steal Follow every tick (bots chase each other / fly around).
        $haveFileHost = ($null -ne $hostX)
        while ($udp.Client.Available -ge 18) {
            $remote = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
            $data = $udp.Receive([ref]$remote)
            $id = 0; $rx = 0.0; $ry = 0.0; $rz = 0.0; $ryaw = 0
            if ([MirrorBot]::TryParsePacket($data, $data.Length, [ref]$id, [ref]$rx, [ref]$ry, [ref]$rz, [ref]$ryaw)) {
                if ($id -eq $botId) { continue }

                # Ignore origin / pre-seed heartbeats.
                $score = Get-PoseScore -X $rx -Y $ry -Z $rz
                if ($score -lt 100.0) { continue }

                $script:peerPos[$id] = @{ x = $rx; y = $ry; z = $rz; ms = $nowMs }

                # TargetFile already has host - keep UDP as soft backup only
                # when file host is missing. Still track locked peer for later.
                if ($haveFileHost) {
                    if ($followTargetId -eq 0 -or $score -gt ($followTargetScore + 5000.0)) {
                        $followTargetId = $id
                        $followTargetScore = $score
                        $followTargetLastSeenMs = $nowMs
                    } elseif ($id -eq $followTargetId) {
                        $followTargetScore = [Math]::Max($followTargetScore, $score)
                        $followTargetLastSeenMs = $nowMs
                    }
                    continue
                }

                # No TargetFile: lock to one peer. Do NOT flip on every peer
                # with score>4000 (all rooftop bots share ~1e4 and used to
                # steal Follow every packet -> flying meshes).
                $adopt = $false
                if ($followTargetId -eq 0) {
                    $adopt = $true
                } elseif ($id -eq $followTargetId) {
                    $adopt = $true
                    $followTargetLastSeenMs = $nowMs
                    if ($score -gt $followTargetScore) { $followTargetScore = $score }
                } else {
                    $staleMs = 2500
                    $stale = (($nowMs - $followTargetLastSeenMs) -gt $staleMs)
                    # Challenger must clearly dominate a stale lock.
                    if ($stale -and $score -gt ($followTargetScore + 3000.0)) {
                        $adopt = $true
                    }
                }

                if ($adopt) {
                    if ($followTargetId -ne $id) {
                        Write-Host ("[bot] Following player id {0} via UDP score={1:n0}" -f $id, $score)
                        $followTargetId = $id
                    }
                    $followTargetScore = [Math]::Max($followTargetScore, $score)
                    $followTargetLastSeenMs = $nowMs
                    $hostX = $rx
                    $hostY = $ry
                    $hostZ = $rz
                    $hostYaw = [int]$ryaw
                }
            }
        }
    }

    if ($DoFollow -and ($null -ne $hostX)) {
        # Stand in front of the host camera (+ lateral) so the mesh is in
        # frustum - not at camera origin (near-clip / invisible).
        $lateral = if (-not [double]::IsNaN($FollowLateral)) {
            $FollowLateral
        } else {
            (($slot % 5) - 2) * 120.0
        }
        $stand = Get-StandOffTarget -HostX $hostX -HostY $hostY -HostZ $hostZ `
            -HostYaw $hostYaw -Distance $FollowDistance -Lateral $lateral
        Update-FollowPosition -PosX ([ref]$posX) -PosY ([ref]$posY) -PosZ ([ref]$posZ) `
            -Yaw ([ref]$yaw) -TargetX $stand.x -TargetY $stand.y -TargetZ $stand.z `
            -DeltaSec $deltaSec
        # Physics probes override stand-off after Follow (host FastTrace clamp).
        # Optional delay keeps SoftProbe near host for softColl/Tag before slam.
        # FallDrop: drop with real downward velocity (not a V=0 hover) so the
        # host grounded-bone override only arms after a true fall settle.
        $physElapsed = $nowMs - $physicsProbeStartMs - $PhysicsProbeDelayMs
        if ($physElapsed -ge 0 -and $PhysicsFallDrop -and $physElapsed -lt 4000) {
            if ($physElapsed -lt 200) {
                $posZ = [float]($hostZ + 1000.0)
            } else {
                # ~1000 UU over ~1.2s then sit near host plane for clamp.
                $tDrop = [Math]::Min(1.0, ($physElapsed - 200) / 1200.0)
                $posZ = [float]($hostZ + 1000.0 * (1.0 - $tDrop) + 40.0)
            }
            if (-not $loggedFallDrop) {
                $loggedFallDrop = $true
                Write-Host ("[bot] PhysicsFallDrop drop z={0:n0} (from host+1000)" -f $posZ)
                try {
                    Add-Content -LiteralPath (Join-Path $env:TEMP "mirroredge-bot-motion.log") `
                        -Value ("PhysicsFallDrop drop z={0}" -f $posZ) -Encoding ascii
                } catch {}
            }
        }
        if ($physElapsed -ge 0 -and $PhysicsWallSlam -and $physElapsed -ge 4500 -and $physElapsed -lt 9000) {
            $posX = [float]($hostX + 900.0)
            $posY = [float]$hostY
            $posZ = [float]$hostZ
            if (-not $loggedWallSlam) {
                $loggedWallSlam = $true
                Write-Host ("[bot] PhysicsWallSlam xy=({0:n0},{1:n0})" -f $posX, $posY)
                try {
                    Add-Content -LiteralPath (Join-Path $env:TEMP "mirroredge-bot-motion.log") `
                        -Value ("PhysicsWallSlam x={0} y={1}" -f $posX, $posY) -Encoding ascii
                } catch {}
            }
        } elseif ($physElapsed -ge 9000 -and ($PhysicsWallSlam -or $PhysicsFallDrop)) {
            # After WallSlam/FallDrop: snap back to Follow stand-off so SoftProbe
            # does not keep waving from inside geometry (Mesh V13).
            $posX = [float]$stand.x
            $posY = [float]$stand.y
            $posZ = [float]$stand.z
        }
        if (($nowMs - $lastFollowLogMs) -gt 3000) {
            $moved = [Math]::Abs($posX - $prevPosX) + [Math]::Abs($posY - $prevPosY) + [Math]::Abs($posZ - $prevPosZ)
            if ($moved -gt 50.0 -or $tick -lt 120) {
                Write-Host ("[bot] Near host stand-off=({0:n0},{1:n0},{2:n0}) host=({3:n0},{4:n0},{5:n0})" -f `
                    $posX, $posY, $posZ, $hostX, $hostY, $hostZ)
                $lastFollowLogMs = $nowMs
            }
        }
    } elseif ($DoFollow) {
        if (($nowMs - $lastFollowLogMs) -gt 5000) {
            Write-Host "[bot] Waiting for host pose (UDP or TargetFile) - stay near player: Set Gameplay + host in level"
            $lastFollowLogMs = $nowMs
        }
    } elseif (-not $DoFollow) {
        # Legacy demo path: orbit in a circle
        $posX = 500.0 + [Math]::Sin($tick * 0.05) * 200.0
        $posY = 500.0 + [Math]::Cos($tick * 0.05) * 200.0
        $yaw = [UInt16](($tick * 10) % 65536)
    }

    # Reload host bone dump / cycle occasionally (host rewrites while running).
    if (-not $NoBones) {
        if (-not $BoneFile) {
            $autoBones = Join-Path $env:TEMP "mirroredge-host-bones.bin"
            if (Test-Path -LiteralPath $autoBones) { $BoneFile = $autoBones }
        }
        if ($BoneFile -and (($nowMs - $lastBoneReloadMs) -gt 250)) {
            $lastBoneReloadMs = $nowMs
            $reloaded = Read-HostBoneSnapshot -Path $BoneFile
            if ($null -ne $reloaded) {
                [Array]::Copy($reloaded, $baseBones, $BoneCount)
                if (-not $bonesLoaded) {
                    $bonesLoaded = $true
                    $nz = ($baseBones | Where-Object { $_ -ne 0 }).Count
                    Write-Host "[bot] Picked up host bone dump ($nz non-zero): $BoneFile"
                }
            }
        }
        if ($BoneCycleFile -and (($nowMs - $lastCycleReloadMs) -gt 250)) {
            $lastCycleReloadMs = $nowMs
            $reloadedCycle = Read-HostBoneCycle -Path $BoneCycleFile
            if ($null -ne $reloadedCycle -and $reloadedCycle.FrameCount -ge 2) {
                $boneCycle = $reloadedCycle
                if (-not $loggedBoneCycle) {
                    $loggedBoneCycle = $true
                    Write-Host ("[bot] Using bone cycle {0} frames from {1}" -f `
                        $reloadedCycle.FrameCount, $BoneCycleFile)
                }
            }
            foreach ($clipName in @("Idle", "Walking", "Falling")) {
                $p = $BoneClipPaths[$clipName]
                $c = Read-HostBoneCycle -Path $p
                if ($null -ne $c -and $c.FrameCount -ge 2) {
                    $boneClips[$clipName] = $c
                }
            }
            if (-not $loggedClipLibrary) {
                $iN = if ($boneClips.Idle) { $boneClips.Idle.FrameCount } else { 0 }
                $wN = if ($boneClips.Walking) { $boneClips.Walking.FrameCount } else { 0 }
                $fN = if ($boneClips.Falling) { $boneClips.Falling.FrameCount } else { 0 }
                $ver = 0
                if ($boneClips.Walking -and $boneClips.Walking.Version) { $ver = [int]$boneClips.Walking.Version }
                elseif ($boneClips.Idle -and $boneClips.Idle.Version) { $ver = [int]$boneClips.Idle.Version }
                if (($iN + $wN + $fN) -gt 0) {
                    $loggedClipLibrary = $true
                    Write-Host ("[bot] bone clip library Idle={0} Walking={1} Falling={2} mebc=v{3}" -f $iN, $wN, $fN, $ver)
                    try {
                        Add-Content -LiteralPath (Join-Path $env:TEMP "mirroredge-bot-motion.log") `
                            -Value ("bone clip library Idle={0} Walking={1} Falling={2} mebc=v{3} name={4}" -f $iN, $wN, $fN, $ver, $Name) `
                            -Encoding ascii
                    } catch {}
                }
            }
        }
    }

    $movedThisTick = [Math]::Abs($posX - $prevPosX) + [Math]::Abs($posY - $prevPosY) + [Math]::Abs($posZ - $prevPosZ)
    $isMoving = ($movedThisTick -gt 5.0)
    $horizSpeed = $movedThisTick / [Math]::Max($deltaSec, 0.001)
    # Resolve MovementState early so named Mesh3p clips can match (V14).
    $moveState = [byte]0
    $physByte = [byte]1
    $fallProbeMs = $nowMs - $physicsProbeStartMs - $PhysicsProbeDelayMs
    if ($PhysicsFallDrop -and $fallProbeMs -ge 0 -and $fallProbeMs -lt 4000) {
        $moveState = [byte]2
        $physByte = [byte]2
    } elseif ($isMoving) {
        $moveState = [byte]1
        $physByte = [byte]1
    } else {
        $moveState = [byte]0
        $physByte = [byte]1
    }
    if (-not $NoBones -and $bonesLoaded) {
        $cycleFrame = $null
        # Prefer named clip library; fall back to legacy cycle / Idle / Walking.
        $wantClip = if ($moveState -eq 2 -or $physByte -eq 2) { "Falling" }
                    elseif ($isMoving -or $moveState -eq 1) { "Walking" }
                    else { "Idle" }
        $playCycle = $null
        if ($null -ne $boneClips[$wantClip] -and $boneClips[$wantClip].FrameCount -ge 2) {
            $playCycle = $boneClips[$wantClip]
        } elseif ($wantClip -eq "Falling" -and $null -ne $boneClips.Walking -and $boneClips.Walking.FrameCount -ge 2) {
            $playCycle = $boneClips.Walking
            $wantClip = "Walking"
        } elseif ($null -ne $boneClips.Idle -and $boneClips.Idle.FrameCount -ge 2) {
            $playCycle = $boneClips.Idle
            $wantClip = "Idle"
        } elseif ($null -ne $boneCycle -and $boneCycle.FrameCount -ge 2) {
            $playCycle = $boneCycle
            $wantClip = "Legacy"
        }
        if ($wantClip -ne $activeClipName) {
            # V15: crossfade into new clip instead of phase=0 snap.
            if ($null -ne $lastCycleFrame) {
                $clipBlendFrom = [Int16[]]::new($BoneCount)
                [Array]::Copy($lastCycleFrame, $clipBlendFrom, $BoneCount)
                $clipBlendRemainMs = $clipBlendDurationMs
            }
            $oldFc = [Math]::Max(1.0, [double]$prevClipFrameCount)
            $activeClipName = $wantClip
            if ($null -ne $playCycle -and $playCycle.FrameCount -ge 2) {
                $frac = ($cyclePhase % $oldFc) / $oldFc
                if ($frac -lt 0) { $frac += 1.0 }
                $cyclePhase = $frac * [double]$playCycle.FrameCount
                $prevClipFrameCount = $playCycle.FrameCount
            } else {
                $cyclePhase = 0.0
            }
        }
        $haveCycle = ($null -ne $playCycle)
        # V16: MEBC v2 time-align to host sample ticks (shared wall clock mod loop).
        # Fallback: speed-driven phase when timestamps missing.
        if ($AnimateBones -and $haveCycle) {
            $useTs = ($null -ne $playCycle.FrameRelMs -and $playCycle.LoopMs -gt 0)
            if ($useTs) {
                $loopMs = [Math]::Max(1, [int64]$playCycle.LoopMs)
                # Avoid uint32 subtraction — PS promotes to signed and throws on cast.
                $tick32 = [int64]([Environment]::TickCount64 -band 0xFFFFFFFF)
                $base32 = [int64]([uint32]$playCycle.BaseTickMs)
                $ageLong = $tick32 - $base32
                if ($ageLong -lt 0) { $ageLong += [int64]4294967296 }
                $tInLoop = [double]($ageLong % $loopMs)
                $rel = $playCycle.FrameRelMs
                $i0 = 0
                for ($fi = 0; $fi -lt ($playCycle.FrameCount - 1); $fi++) {
                    if ([double]$rel[$fi + 1] -le $tInLoop) { $i0 = $fi + 1 }
                    else { break }
                }
                $i1 = ($i0 + 1) % $playCycle.FrameCount
                $t0 = [double]$rel[$i0]
                $t1 = if ($i1 -eq 0) { [double]$loopMs } else { [double]$rel[$i1] }
                $span = [Math]::Max(1.0, $t1 - $t0)
                $frac = ($tInLoop - $t0) / $span
                if ($frac -lt 0.0) { $frac = 0.0 }
                if ($frac -gt 1.0) { $frac = 1.0 }
                $cyclePhase = [double]$i0 + $frac
            } elseif ($isMoving -or $moveState -eq 1) {
                $cyclePhase += ($horizSpeed / 200.0) * $deltaSec * [double]$playCycle.FrameCount * 2.0
            } else {
                $cyclePhase += $deltaSec * 1.35 * [double]$playCycle.FrameCount
            }
            if (-not $loggedCyclePlayback) {
                $loggedCyclePlayback = $true
                $tsNote = if ($useTs) { "mebc-v2" } else { "speed" }
                Write-Host ("[bot] cycle playback {0} frames={1} clip={2} loopMs={3}" -f `
                    $tsNote, $playCycle.FrameCount, $wantClip, $(if ($playCycle.LoopMs) { $playCycle.LoopMs } else { 0 }))
                try {
                    Add-Content -LiteralPath (Join-Path $env:TEMP "mirroredge-bot-motion.log") `
                        -Value ("cycle playback {0} frames={1} clip={2} name={3}" -f $tsNote, $playCycle.FrameCount, $wantClip, $Name) `
                        -Encoding ascii
                } catch {}
            }
            if (-not $useTs) {
                $fc = [double]$playCycle.FrameCount
                $phaseWrap = $cyclePhase % $fc
                if ($phaseWrap -lt 0) { $phaseWrap += $fc }
                $i0 = [int][Math]::Floor($phaseWrap) % $playCycle.FrameCount
                if ($i0 -lt 0) { $i0 += $playCycle.FrameCount }
                $i1 = ($i0 + 1) % $playCycle.FrameCount
                $frac = $phaseWrap - [Math]::Floor($phaseWrap)
            }
            $a = [Int16[]]$playCycle.Frames[$i0]
            $b = [Int16[]]$playCycle.Frames[$i1]
            $cycleFrame = [Int16[]]::new($BoneCount)
            for ($bi = 0; $bi -lt $BoneCount; $bi++) {
                $cycleFrame[$bi] = [Int16]([Math]::Round(
                    (1.0 - $frac) * [double]$a[$bi] + $frac * [double]$b[$bi]))
            }
            if ($clipBlendRemainMs -gt 0 -and $null -ne $clipBlendFrom) {
                $blendT = 1.0 - ([double]$clipBlendRemainMs / [double]$clipBlendDurationMs)
                if ($blendT -lt 0.0) { $blendT = 0.0 }
                if ($blendT -gt 1.0) { $blendT = 1.0 }
                for ($bi = 0; $bi -lt $BoneCount; $bi++) {
                    $cycleFrame[$bi] = [Int16]([Math]::Round(
                        (1.0 - $blendT) * [double]$clipBlendFrom[$bi] +
                        $blendT * [double]$cycleFrame[$bi]))
                }
                $clipBlendRemainMs -= [int]($deltaSec * 1000.0)
                if ($clipBlendRemainMs -le 0) {
                    $clipBlendRemainMs = 0
                    $clipBlendFrom = $null
                }
            }
            $prevClipFrameCount = $playCycle.FrameCount
            $lastCycleFrame = $cycleFrame
        }
        $bones = Update-BotBones -Base $baseBones -Moving $isMoving `
            -Phase ($nowMs / 1000.0 * 1.7) -CycleFrame $cycleFrame `
            -AllowSinFallback $false
    }

    # Build and send UDP position packet (include velocity from tick delta)
    $vx = 0.0; $vy = 0.0; $vz = 0.0
    if ($deltaSec -gt 0.001 -and $deltaSec -lt 0.5) {
        $vx = ($posX - $prevPosX) / $deltaSec
        $vy = ($posY - $prevPosY) / $deltaSec
        $vz = ($posZ - $prevPosZ) / $deltaSec
    }
    # B3-lite trailer already resolved above for clip selection.
    # Do not TX near-origin XY on real levels — host corridor had to invent a
    # stand-off and SoftProbe looked hundreds of UU down the walkway.
    $xyMag = [Math]::Abs([double]$posX) + [Math]::Abs([double]$posY)
    if ($xyMag -lt 100.0 -and $Level -match 'tutorial|gameplay') {
        Start-Sleep -Milliseconds 16
        continue
    }
    $packet = [MirrorBot]::BuildPacket(
        [UInt32]$botId,
        [float]$posX,
        [float]$posY,
        [float]$posZ,
        [UInt16]$yaw,
        $bones,
        [float]$vx,
        [float]$vy,
        [float]$vz,
        $moveState,
        $physByte
    )
    try {
        $udp.Send($packet, $packet.Length)
    } catch {
        Write-Host "[bot] UDP send error: $_"
        $connected = $false
        break
    }

    # Ping keep-alive every 5 seconds
    $now = [MirrorBot]::GetTimestamp()
    if ($now - $lastPing -gt 5000) {
        $ping = (@{type="client_ping"; id=$botId; ts=$now} | ConvertTo-Json -Compress)
        try {
            Send-TcpJsonMessage -Json $ping
            $lastPing = $now
        } catch {
            $connected = $false
        }
    }

    # B0: SoftProbe / harness starts Tag after a short settle.
    if ($StartTag -and -not $tagStarted -and $nowMs -ge $tagStartAfterMs) {
        try {
            Send-TcpJsonMessage -Json (@{ type = "startTagGameMode" } | ConvertTo-Json -Compress)
            $announce = "[Tag] {0} started tag" -f $Name
            Send-TcpJsonMessage -Json (@{ type = "announce"; body = $announce } | ConvertTo-Json -Compress)
            $tagStarted = $true
            Write-Host "[bot] startTagGameMode sent"
            try {
                Add-Content -LiteralPath (Join-Path $env:TEMP "mirroredge-bot-motion.log") `
                    -Value ("startTagGameMode name={0}" -f $Name) -Encoding ascii
            } catch {}
        } catch {
            Write-Host "[bot] WARN: startTag failed: $_"
        }
    }

    # B1: wave nearest Follow peer (chat UX on host). Blackout FallDrop +
    # WallSlam + 2s settle (physElapsed < 11000) so SoftProbe does not wave
    # while still inside WallSlam geometry (Mesh V13).
    $physElapsedForWave = $nowMs - $physicsProbeStartMs - $PhysicsProbeDelayMs
    $physicsWaveBlocked = $false
    if ($PhysicsFallDrop -or $PhysicsWallSlam) {
        $physicsWaveBlocked =
            ($physElapsedForWave -ge 0 -and $physElapsedForWave -lt 11000)
    }
    if ($SendInteract -and (-not $physicsWaveBlocked) -and ($nowMs - $lastInteractMs) -gt 2000) {
        $target = Get-InteractTargetId
        $toId = [uint32]$target.Id
        if ($toId -ne 0) {
            try {
                $msg = @{
                    type = "interact"
                    from = $botId
                    to   = $toId
                    kind = "wave"
                    dist = [Math]::Round([double]$target.DistM, 2)
                } | ConvertTo-Json -Compress
                Send-TcpJsonMessage -Json $msg
                $lastInteractMs = $nowMs
                $waveUntilMs = $nowMs + 1800
                Write-Host ("[bot] interact wave -> id {0} dist={1:n2}m" -f $toId, $target.DistM)
                try {
                    Add-Content -LiteralPath (Join-Path $env:TEMP "mirroredge-bot-motion.log") `
                        -Value ("interact wave to={0} name={1} dist={2}" -f $toId, $Name, $target.DistM) -Encoding ascii
                } catch {}
            } catch {
                Write-Host "[bot] WARN: interact failed: $_"
            }
        }
    }

    # ~60 Hz update rate
    Start-Sleep -Milliseconds 16
}

Write-Host "[bot] Cleanup..."

# Send disconnect
try {
    $disco = (@{type="disconnect"; id=$botId} | ConvertTo-Json -Compress)
    Send-TcpJsonMessage -Json $disco
} catch {}

$udp.Close()
$tcp.Close()
Write-Host "[bot] Disconnected"
exit 0
