#Requires -Version 5.1
<#
.SYNOPSIS
  Verify SDK generated headers against Ghidra reference data or live binary.

.DESCRIPTION
  Reads sdk-reference.json (from Ghidra extraction) and/or scans the live
  MirrorsEdge.exe to verify:
  - FNV code probe matches
  - GNames/GObjects patterns resolve
  - Class sizes match generated headers
  - Image size is within expected bounds

  Can operate in two modes:
  1. Reference mode: compare against tools/sdk-verify/reference/sdk-reference.json
  2. Binary mode: scan MirrorsEdge.exe directly for patterns (no Ghidra needed)

.PARAMETER GameExe
  Path to MirrorsEdge.exe for binary-mode verification.

.PARAMETER WriteLiteReference
  Write pure binary/instruction scan results to reference/sdk-reference-lite.json.

.PARAMETER CheckLiteReference
  Compare current pure binary/instruction scan results with reference/sdk-reference-lite.json.
#>
param(
    [string]$GameExe = "",
    [switch]$BinaryOnly,
    [switch]$WriteLiteReference,
    [switch]$CheckLiteReference,
    [string]$LiteReferencePath = ""
)

$ErrorActionPreference = "Continue"
$ToolDir = $PSScriptRoot
# $ToolDir = .../tools/sdk-verify → go up 2 levels to repo root
$Root = Split-Path (Split-Path $ToolDir -Parent) -Parent
$ReferenceJson = Join-Path $ToolDir "reference\sdk-reference.json"
if (-not $LiteReferencePath) {
    $LiteReferencePath = Join-Path $ToolDir "reference\sdk-reference-lite.json"
}
$Errors = 0
$Warnings = 0

function Write-Pass { Write-Host "  [PASS] $args" -ForegroundColor Green }
function Write-Fail { Write-Host "  [FAIL] $args" -ForegroundColor Red; $script:Errors++ }
function Write-Warn { Write-Host "  [WARN] $args" -ForegroundColor Yellow; $script:Warnings++ }

function Format-HexValue {
    param([UInt64]$Value)
    return "0x{0:X}" -f $Value
}

function ConvertTo-NullableHex {
    param([Nullable[UInt64]]$Value)
    if ($null -eq $Value) { return $null }
    return Format-HexValue $Value
}

function Find-GameExe {
    param([string]$Root)
    if ($GameExe -and (Test-Path $GameExe)) { return $GameExe }
    $deployConfig = Join-Path $Root "deploy.config.json"
    if (Test-Path $deployConfig) {
        $json = Get-Content $deployConfig -Raw | ConvertFrom-Json
        if ($json.deployPath) {
            $candidate = Join-Path $json.deployPath "Binaries\MirrorsEdge.exe"
            if (Test-Path $candidate) { return $candidate }
            $candidate = Join-Path $json.deployPath "MirrorsEdge.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    $candidate = Join-Path $Root "Binaries\MirrorsEdge.exe"
    if (Test-Path $candidate) { return $candidate }
    $candidate = Join-Path $Root "MirrorsEdge.exe"
    if (Test-Path $candidate) { return $candidate }
    return $null
}

function Read-UInt32 {
    param([byte[]]$Bytes, [int]$Offset)
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Read-UInt16 {
    param([byte[]]$Bytes, [int]$Offset)
    return [BitConverter]::ToUInt16($Bytes, $Offset)
}

function Read-AsciiString {
    param([byte[]]$Bytes, [int]$Offset, [int]$Length)
    $raw = [System.Text.Encoding]::ASCII.GetString($Bytes, $Offset, $Length)
    return $raw.TrimEnd([char]0)
}

# Embedded C# FNV-1a — PowerShell integer arithmetic is lossy with uint32 wrapping
Add-Type -TypeDefinition @'
using System;
public static class FnvHash {
    public static uint Compute(byte[] data) {
        uint hash = 2166136261;
        foreach (byte b in data) {
            hash ^= b;
            hash *= 16777619;
        }
        return hash;
    }
}
'@ -ErrorAction SilentlyContinue

function Compute-Fnv1a32 {
    param([byte[]]$Bytes)
    return [FnvHash]::Compute($Bytes)
}

function Find-Bytes {
    param([byte[]]$Haystack, [byte[]]$Needle, [string]$Mask)
    $matches = Find-ByteMatches $Haystack $Needle $Mask
    if ($matches.Count -gt 0) { return $matches[0] }
    return -1
}

function Find-ByteMatches {
    param([byte[]]$Haystack, [byte[]]$Needle, [string]$Mask)
    $matches = [System.Collections.Generic.List[int]]::new()
    for ($i = 0; $i -le $Haystack.Length - $Mask.Length; $i++) {
        $match = $true
        for ($j = 0; $j -lt $Mask.Length; $j++) {
            if ($Mask[$j] -eq 'x' -and $Haystack[$i + $j] -ne $Needle[$j]) {
                $match = $false
                break
            }
        }
        if ($match) { $matches.Add($i) }
    }
    return $matches.ToArray()
}

function Find-ByteMatchesInRanges {
    param([byte[]]$Haystack, [byte[]]$Needle, [string]$Mask, [object[]]$Ranges)
    $matches = [System.Collections.Generic.List[int]]::new()
    foreach ($range in $Ranges) {
        $start = [int]$range.start
        $end = [Math]::Min([int]$range.end, $Haystack.Length)
        for ($i = $start; $i -le $end - $Mask.Length; $i++) {
            $match = $true
            for ($j = 0; $j -lt $Mask.Length; $j++) {
                if ($Mask[$j] -eq 'x' -and $Haystack[$i + $j] -ne $Needle[$j]) {
                    $match = $false
                    break
                }
            }
            if ($match) { $matches.Add($i) }
        }
    }
    return $matches.ToArray()
}

function Read-PeImage {
    param([string]$ExePath, [int]$Offset, [int]$Length)
    $fs = [System.IO.File]::OpenRead($ExePath)
    $reader = [System.IO.BinaryReader]::new($fs)
    $fs.Seek($Offset, [System.IO.SeekOrigin]::Begin) | Out-Null
    $data = $reader.ReadBytes($Length)
    $reader.Close()
    $fs.Close()
    return $data
}

function Get-PeImageSize {
    param([string]$ExePath)
    $fs = [System.IO.File]::OpenRead($ExePath)
    $reader = [System.IO.BinaryReader]::new($fs)
    # Read PE header offset at 0x3C
    $fs.Seek(0x3C, [System.IO.SeekOrigin]::Begin) | Out-Null
    $peOffsetBytes = $reader.ReadBytes(4)
    $peOffset = [BitConverter]::ToInt32($peOffsetBytes, 0)
    # Read SizeOfImage at PE offset + 0x50
    $fs.Seek($peOffset + 0x50, [System.IO.SeekOrigin]::Begin) | Out-Null
    $sizeBytes = $reader.ReadBytes(4)
    $imageSize = [BitConverter]::ToUInt32($sizeBytes, 0)
    $reader.Close()
    $fs.Close()
    return $imageSize
}

function Get-PeInfo {
    param([byte[]]$Bytes)

    $peOffset = [BitConverter]::ToInt32($Bytes, 0x3C)
    $sectionCount = Read-UInt16 $Bytes ($peOffset + 6)
    $optionalHeaderSize = Read-UInt16 $Bytes ($peOffset + 20)
    $optionalHeaderOffset = $peOffset + 24
    $imageBase = Read-UInt32 $Bytes ($optionalHeaderOffset + 28)
    $imageSize = Read-UInt32 $Bytes ($optionalHeaderOffset + 56)
    $sectionTableOffset = $optionalHeaderOffset + $optionalHeaderSize

    $sections = @()
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $offset = $sectionTableOffset + ($i * 40)
        $sections += [pscustomobject]@{
            name = Read-AsciiString $Bytes $offset 8
            virtual_size = Read-UInt32 $Bytes ($offset + 8)
            virtual_address = Read-UInt32 $Bytes ($offset + 12)
            raw_size = Read-UInt32 $Bytes ($offset + 16)
            raw_pointer = Read-UInt32 $Bytes ($offset + 20)
            characteristics = Read-UInt32 $Bytes ($offset + 36)
        }
    }

    return [pscustomobject]@{
        image_base = $imageBase
        image_size = $imageSize
        sections = $sections
    }
}

function Get-ExecutableScanRanges {
    param([pscustomobject]$PeInfo, [int]$FileSize)

    $ranges = @()
    foreach ($section in $PeInfo.sections) {
        $isExecutable = (($section.characteristics -band 0x20000000) -ne 0) -or
                        ($section.name -eq ".text")
        if (-not $isExecutable -or $section.raw_size -le 0) {
            continue
        }
        $start = [int]$section.raw_pointer
        $end = [Math]::Min($FileSize, $start + [int]$section.raw_size)
        if ($end -gt $start) {
            $ranges += [pscustomobject]@{
                section = $section.name
                start = $start
                end = $end
            }
        }
    }
    return $ranges
}

function Resolve-FileOffset {
    param([pscustomobject]$PeInfo, [int]$FileOffset)

    foreach ($section in $PeInfo.sections) {
        $rawStart = [int64]$section.raw_pointer
        $rawEnd = $rawStart + [int64]$section.raw_size
        if ($FileOffset -ge $rawStart -and $FileOffset -lt $rawEnd) {
            $rva = [uint32]($section.virtual_address + ($FileOffset - $section.raw_pointer))
            $va = [uint32]($PeInfo.image_base + $rva)
            return [pscustomobject]@{
                file_offset = $FileOffset
                file_offset_hex = Format-HexValue $FileOffset
                rva = $rva
                rva_hex = Format-HexValue $rva
                va = $va
                va_hex = Format-HexValue $va
                section = $section.name
            }
        }
    }

    return [pscustomobject]@{
        file_offset = $FileOffset
        file_offset_hex = Format-HexValue $FileOffset
        rva = $null
        rva_hex = $null
        va = $null
        va_hex = $null
        section = $null
    }
}

function Resolve-Va {
    param([pscustomobject]$PeInfo, [uint32]$Va)

    $rva = [int64]$Va - [int64]$PeInfo.image_base
    foreach ($section in $PeInfo.sections) {
        $sectionStart = [int64]$section.virtual_address
        $sectionSpan = [Math]::Max([int64]$section.virtual_size, [int64]$section.raw_size)
        $sectionEnd = $sectionStart + $sectionSpan
        if ($rva -ge $sectionStart -and $rva -lt $sectionEnd) {
            $fileOffset = $null
            $rawDelta = $rva - $sectionStart
            if ($rawDelta -ge 0 -and $rawDelta -lt [int64]$section.raw_size) {
                $fileOffset = [uint32]($section.raw_pointer + $rawDelta)
            }
            return [pscustomobject]@{
                va = $Va
                va_hex = Format-HexValue $Va
                rva = [uint32]$rva
                rva_hex = Format-HexValue $rva
                file_offset = $fileOffset
                file_offset_hex = ConvertTo-NullableHex $fileOffset
                section = $section.name
            }
        }
    }

    return [pscustomobject]@{
        va = $Va
        va_hex = Format-HexValue $Va
        rva = if ($rva -ge 0) { [uint32]$rva } else { $null }
        rva_hex = if ($rva -ge 0) { Format-HexValue $rva } else { $null }
        file_offset = $null
        file_offset_hex = $null
        section = $null
    }
}

function Get-NestedValue {
    param([object]$Object, [string]$Path)
    $current = $Object
    foreach ($part in ($Path -split '\.')) {
        if ($null -eq $current) { return $null }
        $prop = $current.PSObject.Properties[$part]
        if ($null -eq $prop) { return $null }
        $current = $prop.Value
    }
    return $current
}

function Compare-LiteReference {
    param([object]$Expected, [object]$Actual)

    $paths = @(
        "format_version",
        "generated_by",
        "mode",
        "binary.file_name",
        "binary.file_size",
        "binary.sha256",
        "binary.image_size",
        "binary.image_base",
        "binary.section_count",
        "binary.scan_scope",
        "code_probe.file_offset",
        "code_probe.length",
        "code_probe.fnv1a32",
        "patterns.gnames.signature",
        "patterns.gnames.mask",
        "patterns.gnames.match_count",
        "patterns.gnames.file_offset",
        "patterns.gnames.rva",
        "patterns.gnames.va",
        "patterns.gnames.section",
        "patterns.gnames.target_va",
        "patterns.gnames.target_rva",
        "patterns.gnames.target_file_offset",
        "patterns.gnames.target_section",
        "patterns.gobjects.signature",
        "patterns.gobjects.mask",
        "patterns.gobjects.match_count",
        "patterns.gobjects.file_offset",
        "patterns.gobjects.rva",
        "patterns.gobjects.va",
        "patterns.gobjects.section",
        "patterns.gobjects.target_va",
        "patterns.gobjects.target_rva",
        "patterns.gobjects.target_file_offset",
        "patterns.gobjects.target_section"
    )

    foreach ($path in $paths) {
        $expectedValue = Get-NestedValue $Expected $path
        $actualValue = Get-NestedValue $Actual $path
        if ("$expectedValue" -ne "$actualValue") {
            Write-Fail "Lite reference mismatch at $path (expected '$expectedValue', actual '$actualValue')"
        }
    }
}

Write-Host "=== SDK Verification ===" -ForegroundColor Cyan
Write-Host ""

$gameExePath = Find-GameExe -Root $Root
$liteReference = $null

# --- Binary Verification ---
$binaryOk = $false
if ($gameExePath -and (Test-Path $gameExePath)) {
    Write-Host "--- Binary: $gameExePath ---" -ForegroundColor DarkGray
    $fileInfo = Get-Item $gameExePath

    # 1. Image size check
    $imageSize = Get-PeImageSize $gameExePath
    $minSize = 0x03000000
    $maxSize = 0x04000000
    if ($imageSize -lt $minSize -or $imageSize -gt $maxSize) {
        Write-Fail "Image size 0x$($imageSize.ToString('X')) outside expected range [0x$($minSize.ToString('X')), 0x$($maxSize.ToString('X'))]"
    } else {
        Write-Pass "Image size: 0x$($imageSize.ToString('X')) ($($imageSize / 1MB)MB)"
    }

    # 2. Code probe FNV
    $probeData = Read-PeImage $gameExePath 0x1000 4096
    $fnv = Compute-Fnv1a32 $probeData
    Write-Host "  Code probe FNV: 0x$($fnv.ToString('X8'))" -ForegroundColor DarkGray

    # 3. Pattern scan (need to read full binary or raw section data)
    Write-Host "  Scanning for GNames/GObjects patterns in $(($fileInfo.Length) / 1MB)MB file..." -ForegroundColor DarkGray

    $allBytes = [System.IO.File]::ReadAllBytes($gameExePath)
    $peInfo = Get-PeInfo $allBytes
    $scanRanges = Get-ExecutableScanRanges $peInfo $allBytes.Length
    $scanRangeNames = @($scanRanges | ForEach-Object { $_.section })

    # GNames: 8B 0D ?? ?? ?? ?? 8B 84 24 ?? ?? ?? ?? 8B 04 81
    $gnamesPattern = [byte[]]@(0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x84, 0x24, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x04, 0x81)
    $gnamesMask = "xx????xxx????xxx"
    $gnamesMatches = @(Find-ByteMatchesInRanges $allBytes $gnamesPattern $gnamesMask $scanRanges)
    $gnamesOffset = if ($gnamesMatches.Count -gt 0) { $gnamesMatches[0] } else { -1 }
    if ($gnamesMatches.Count -eq 1) {
        Write-Pass "GNames pattern unique at file offset 0x$($gnamesOffset.ToString('X'))"
    } elseif ($gnamesMatches.Count -gt 1) {
        Write-Fail "GNames pattern not unique ($($gnamesMatches.Count) matches)"
    } else {
        Write-Fail "GNames pattern not found in binary"
    }
    $gnamesTarget = if ($gnamesOffset -ge 0) { Read-UInt32 $allBytes ($gnamesOffset + 2) } else { 0 }
    $gnamesLocation = if ($gnamesOffset -ge 0) { Resolve-FileOffset $peInfo $gnamesOffset } else { $null }
    $gnamesTargetLocation = if ($gnamesTarget -ne 0) { Resolve-Va $peInfo $gnamesTarget } else { $null }
    if ($gnamesTarget -ne 0 -and $gnamesTargetLocation -and -not $gnamesTargetLocation.section) {
        Write-Fail "GNames target VA 0x$($gnamesTarget.ToString('X')) is outside PE sections"
    }

    # GObjects: 8B 15 ?? ?? ?? ?? 8B 0C B2 8D 44 24 30
    $gobjectsPattern = [byte[]]@(0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x0C, 0xB2, 0x8D, 0x44, 0x24, 0x30)
    $gobjectsMask = "xx????xxxxxxx"
    $gobjectsMatches = @(Find-ByteMatchesInRanges $allBytes $gobjectsPattern $gobjectsMask $scanRanges)
    $gobjectsOffset = if ($gobjectsMatches.Count -gt 0) { $gobjectsMatches[0] } else { -1 }
    if ($gobjectsMatches.Count -eq 1) {
        Write-Pass "GObjects pattern unique at file offset 0x$($gobjectsOffset.ToString('X'))"
    } elseif ($gobjectsMatches.Count -gt 1) {
        Write-Fail "GObjects pattern not unique ($($gobjectsMatches.Count) matches)"
    } else {
        Write-Fail "GObjects pattern not found in binary"
    }
    $gobjectsTarget = if ($gobjectsOffset -ge 0) { Read-UInt32 $allBytes ($gobjectsOffset + 2) } else { 0 }
    $gobjectsLocation = if ($gobjectsOffset -ge 0) { Resolve-FileOffset $peInfo $gobjectsOffset } else { $null }
    $gobjectsTargetLocation = if ($gobjectsTarget -ne 0) { Resolve-Va $peInfo $gobjectsTarget } else { $null }
    if ($gobjectsTarget -ne 0 -and -not $gobjectsTargetLocation.section) {
        Write-Fail "GObjects target VA 0x$($gobjectsTarget.ToString('X')) is outside PE sections"
    }

    $liteReference = [ordered]@{
        format_version = 1
        generated_by = "verify-sdk.ps1"
        mode = "pure-instruction-scan"
        binary = [ordered]@{
            file_name = $fileInfo.Name
            file_size = $fileInfo.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -Path $gameExePath).Hash.ToLowerInvariant()
            image_base = $peInfo.image_base
            image_base_hex = Format-HexValue $peInfo.image_base
            image_size = $imageSize
            image_size_hex = Format-HexValue $imageSize
            section_count = $peInfo.sections.Count
            scan_scope = "executable-sections"
            scan_sections = $scanRangeNames
        }
        code_probe = [ordered]@{
            file_offset = 0x1000
            length = 4096
            fnv1a32 = $fnv
            fnv1a32_hex = "0x$($fnv.ToString('X8'))"
        }
        patterns = [ordered]@{
            gnames = [ordered]@{
                signature = "8B 0D ?? ?? ?? ?? 8B 84 24 ?? ?? ?? ?? 8B 04 81"
                mask = $gnamesMask
                found = ($gnamesOffset -ge 0)
                match_count = $gnamesMatches.Count
                file_offset = $gnamesOffset
                file_offset_hex = if ($gnamesOffset -ge 0) { Format-HexValue $gnamesOffset } else { $null }
                rva = if ($gnamesLocation) { $gnamesLocation.rva } else { $null }
                rva_hex = if ($gnamesLocation) { $gnamesLocation.rva_hex } else { $null }
                va = if ($gnamesLocation) { $gnamesLocation.va } else { $null }
                va_hex = if ($gnamesLocation) { $gnamesLocation.va_hex } else { $null }
                section = if ($gnamesLocation) { $gnamesLocation.section } else { $null }
                target_va = $gnamesTarget
                target_va_hex = if ($gnamesTarget -ne 0) { Format-HexValue $gnamesTarget } else { $null }
                target_rva = if ($gnamesTargetLocation) { $gnamesTargetLocation.rva } else { $null }
                target_rva_hex = if ($gnamesTargetLocation) { $gnamesTargetLocation.rva_hex } else { $null }
                target_file_offset = if ($gnamesTargetLocation) { $gnamesTargetLocation.file_offset } else { $null }
                target_file_offset_hex = if ($gnamesTargetLocation) { $gnamesTargetLocation.file_offset_hex } else { $null }
                target_section = if ($gnamesTargetLocation) { $gnamesTargetLocation.section } else { $null }
            }
            gobjects = [ordered]@{
                signature = "8B 15 ?? ?? ?? ?? 8B 0C B2 8D 44 24 30"
                mask = $gobjectsMask
                found = ($gobjectsOffset -ge 0)
                match_count = $gobjectsMatches.Count
                file_offset = $gobjectsOffset
                file_offset_hex = if ($gobjectsOffset -ge 0) { Format-HexValue $gobjectsOffset } else { $null }
                rva = if ($gobjectsLocation) { $gobjectsLocation.rva } else { $null }
                rva_hex = if ($gobjectsLocation) { $gobjectsLocation.rva_hex } else { $null }
                va = if ($gobjectsLocation) { $gobjectsLocation.va } else { $null }
                va_hex = if ($gobjectsLocation) { $gobjectsLocation.va_hex } else { $null }
                section = if ($gobjectsLocation) { $gobjectsLocation.section } else { $null }
                target_va = $gobjectsTarget
                target_va_hex = if ($gobjectsTarget -ne 0) { Format-HexValue $gobjectsTarget } else { $null }
                target_rva = if ($gobjectsTargetLocation) { $gobjectsTargetLocation.rva } else { $null }
                target_rva_hex = if ($gobjectsTargetLocation) { $gobjectsTargetLocation.rva_hex } else { $null }
                target_file_offset = if ($gobjectsTargetLocation) { $gobjectsTargetLocation.file_offset } else { $null }
                target_file_offset_hex = if ($gobjectsTargetLocation) { $gobjectsTargetLocation.file_offset_hex } else { $null }
                target_section = if ($gobjectsTargetLocation) { $gobjectsTargetLocation.section } else { $null }
            }
        }
    }

    $binaryOk = $true
} else {
    Write-Warn "MirrorsEdge.exe not found - skipping binary verification"
    Write-Host "  Set deploy.config.json deployPath or use -GameExe parameter" -ForegroundColor DarkGray
}

Write-Host ""

# --- Reference Comparison ---
if (-not $BinaryOnly -and (Test-Path $ReferenceJson)) {
    Write-Host "--- Reference data: $ReferenceJson ---" -ForegroundColor DarkGray
    $ref = Get-Content $ReferenceJson -Raw | ConvertFrom-Json

    if ($ref.code_probe_fnv -and $ref.code_probe_fnv -ne 0) {
        Write-Host "  Reference FNV: 0x$($ref.code_probe_fnv.ToString('X8'))" -ForegroundColor DarkGray
    }

    if ($ref.patterns) {
        if ($ref.patterns.gnames) {
            Write-Host "  Reference GNames pattern: 0x$($ref.patterns.gnames.ToString('X'))" -ForegroundColor DarkGray
        }
        if ($ref.patterns.gobjects) {
            Write-Host "  Reference GObjects pattern: 0x$($ref.patterns.gobjects.ToString('X'))" -ForegroundColor DarkGray
        }
    }

    if ($ref.classes) {
        Write-Host "  Reference classes: $($ref.classes.PSObject.Properties.Count)" -ForegroundColor DarkGray
        foreach ($prop in $ref.classes.PSObject.Properties) {
            $className = $prop.Name
            $classData = $prop.Value
            Write-Host "    $className size=0x$($classData.size.ToString('X'))" -ForegroundColor DarkGray
        }
    }

    Write-Pass "Reference data loaded successfully"
} elseif ($BinaryOnly) {
    Write-Host "--- Reference data skipped (-BinaryOnly) ---" -ForegroundColor DarkGray
} else {
    Write-Warn "sdk-reference.json not found - run extract-sdk-data.ps1 first"
}

if ($CheckLiteReference) {
    if (-not $liteReference) {
        Write-Fail "Cannot check lite reference without a scanned game binary"
    } elseif (-not (Test-Path $LiteReferencePath)) {
        Write-Fail "Lite reference not found: $LiteReferencePath"
    } else {
        Write-Host "--- Lite reference check: $LiteReferencePath ---" -ForegroundColor DarkGray
        $expectedLiteReference = Get-Content $LiteReferencePath -Raw | ConvertFrom-Json
        $actualLiteReference = ($liteReference | ConvertTo-Json -Depth 8) | ConvertFrom-Json
        Compare-LiteReference -Expected $expectedLiteReference -Actual $actualLiteReference
        if ($Errors -eq 0) {
            Write-Pass "Lite reference matches current binary scan"
        }
    }
}

if ($WriteLiteReference) {
    if ($liteReference) {
        $liteDir = Split-Path $LiteReferencePath -Parent
        if ($liteDir -and -not (Test-Path $liteDir)) {
            New-Item -ItemType Directory -Force -Path $liteDir | Out-Null
        }
        $json = $liteReference | ConvertTo-Json -Depth 8
        $utf8 = [System.Text.UTF8Encoding]::new($false)
        [System.IO.File]::WriteAllText($LiteReferencePath, $json + [Environment]::NewLine, $utf8)
        Write-Pass "Lite reference written: $LiteReferencePath"
    } else {
        Write-Fail "Cannot write lite reference without a scanned game binary"
    }
}

Write-Host ""

# --- Summary ---
Write-Host "=== Verification Summary ===" -ForegroundColor Cyan
if ($Errors -eq 0 -and $Warnings -eq 0) {
    Write-Host "  All checks passed." -ForegroundColor Green
} elseif ($Errors -eq 0) {
    Write-Host "  $Warnings warning(s), 0 errors." -ForegroundColor Yellow
} else {
    Write-Host "  $Errors error(s), $Warnings warning(s)." -ForegroundColor Red
}

if ($Errors -gt 0) {
    exit 1
}
exit 0
