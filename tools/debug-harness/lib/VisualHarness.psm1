#Requires -Version 5.1
Set-StrictMode -Version Latest

if (-not ("Win32Capture" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Capture {
    public const uint PW_RENDERFULLCONTENT = 0x00000002;

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
}
"@
}

function Initialize-VisualHarnessDrawing {
    if (-not ("System.Drawing.Bitmap" -as [type])) {
        Add-Type -AssemblyName System.Drawing
    }
}

function Get-VisualArtifactsDir {
    $base = Join-Path $env:TEMP "mirroredge-debug"
    $sid = "adhoc"
    $manifest = Join-Path $base "last-session.json"
    if (Test-Path $manifest) {
        try {
            $last = Get-Content $manifest -Raw | ConvertFrom-Json
            if ($last.sessionId) {
                $sid = [string]$last.sessionId
            }
        } catch {}
    }
    $dir = Join-Path $base "$sid-visual"
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    return $dir
}

function Get-VisualBaselineThresholds {
    $root = Split-Path $PSScriptRoot -Parent
    $path = Join-Path $root "visual-baselines\thresholds.json"
    $defaults = [ordered]@{
        minNonBlackRatio   = 0.015
        minWidth           = 320
        minHeight          = 240
        minMeanLumDelta    = 1.2
        minVarianceDelta   = 25.0
        minNonBlackDelta   = 0.004
        captureSettleMs    = 400
        minWidthPartial    = 200
        minHeightPartial   = 180
    }
    if (-not (Test-Path $path)) {
        return [pscustomobject]$defaults
    }
    try {
        $file = Get-Content $path -Raw | ConvertFrom-Json
        foreach ($key in @($defaults.Keys)) {
            if ($null -ne $file.$key) {
                $defaults[$key] = $file.$key
            }
        }
    } catch {
        Write-Host "visual: WARN thresholds.json unreadable ($($_.Exception.Message)); using defaults"
    }
    return [pscustomobject]$defaults
}

function Get-ImageFrameStats {
    param(
        [Parameter(Mandatory)]
        [string]$ImagePath,
        [int]$SampleStep = 4
    )

    Initialize-VisualHarnessDrawing
    if (-not (Test-Path $ImagePath)) {
        throw "Get-ImageFrameStats: missing file $ImagePath"
    }

    $bmp = [System.Drawing.Bitmap]::FromFile($ImagePath)
    try {
        $w = $bmp.Width
        $h = $bmp.Height
        if ($w -le 0 -or $h -le 0) {
            throw "Get-ImageFrameStats: invalid dimensions ${w}x${h}"
        }

        $step = [Math]::Max(1, $SampleStep)
        $sumR = 0.0
        $sumG = 0.0
        $sumB = 0.0
        $sumSq = 0.0
        $samples = 0
        $nonBlack = 0

        for ($y = 0; $y -lt $h; $y += $step) {
            for ($x = 0; $x -lt $w; $x += $step) {
                $c = $bmp.GetPixel($x, $y)
                $r = [double]$c.R
                $g = [double]$c.G
                $b = [double]$c.B
                $lum = ($r + $g + $b) / 3.0
                $sumR += $r
                $sumG += $g
                $sumB += $b
                $sumSq += $lum * $lum
                $samples++
                if (($r + $g + $b) -gt 18) {
                    $nonBlack++
                }
            }
        }

        if ($samples -le 0) {
            throw "Get-ImageFrameStats: no samples"
        }

        $meanR = $sumR / $samples
        $meanG = $sumG / $samples
        $meanB = $sumB / $samples
        $meanLum = ($meanR + $meanG + $meanB) / 3.0
        $variance = ($sumSq / $samples) - ($meanLum * $meanLum)
        if ($variance -lt 0) { $variance = 0 }

        return [pscustomobject]@{
            Path           = $ImagePath
            Width          = $w
            Height         = $h
            Samples        = $samples
            MeanR          = [Math]::Round($meanR, 3)
            MeanG          = [Math]::Round($meanG, 3)
            MeanB          = [Math]::Round($meanB, 3)
            MeanLuminance  = [Math]::Round($meanLum, 3)
            Variance       = [Math]::Round($variance, 3)
            NonBlackRatio  = [Math]::Round($nonBlack / $samples, 5)
        }
    } finally {
        $bmp.Dispose()
    }
}

function Capture-WindowScreenshot {
    param(
        [Parameter(Mandatory)]
        [IntPtr]$WindowHandle,
        [Parameter(Mandatory)]
        [string]$OutPath,
        [string]$Label = ""
    )

    if ($WindowHandle -eq [IntPtr]::Zero) {
        throw "Capture-WindowScreenshot: null window handle ($Label)"
    }

    Initialize-VisualHarnessDrawing

    $rect = New-Object Win32Window+RECT
    if (-not [Win32Window]::GetWindowRect($WindowHandle, [ref]$rect)) {
        throw "Capture-WindowScreenshot: GetWindowRect failed ($Label)"
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        throw "Capture-WindowScreenshot: invalid window size ${width}x${height} ($Label)"
    }

    $dir = Split-Path $OutPath -Parent
    if ($dir -and -not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }

    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $graphics = $null
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bmp)
        $graphics.Clear([System.Drawing.Color]::Black)
        $hdc = $graphics.GetHdc()
        try {
            $ok = [Win32Capture]::PrintWindow(
                $WindowHandle, $hdc, [Win32Capture]::PW_RENDERFULLCONTENT)
            if (-not $ok) {
                $ok = [Win32Capture]::PrintWindow($WindowHandle, $hdc, 0)
            }
            if (-not $ok) {
                throw "PrintWindow failed ($Label)"
            }
        } finally {
            $graphics.ReleaseHdc($hdc)
        }
        $bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        if ($graphics) { $graphics.Dispose() }
        $bmp.Dispose()
    }

    $stats = Get-ImageFrameStats -ImagePath $OutPath
    Write-Host ("visual: capture {0} -> {1} ({2}x{3} lum={4} var={5} nonBlack={6})" -f `
        $Label, (Split-Path $OutPath -Leaf), $stats.Width, $stats.Height, `
        $stats.MeanLuminance, $stats.Variance, $stats.NonBlackRatio)

    return [pscustomobject]@{
        Path  = $OutPath
        Label = $Label
        Stats = $stats
    }
}

function Capture-GameWindowScreenshot {
    param(
        [Parameter(Mandatory)]
        [string]$Label,
        [IntPtr]$WindowHandle = [IntPtr]::Zero
    )

    if ($WindowHandle -eq [IntPtr]::Zero) {
        if (-not (Get-Command Get-GameWindowHandle -ErrorAction SilentlyContinue)) {
            throw "Capture-GameWindowScreenshot: Get-GameWindowHandle unavailable"
        }
        $WindowHandle = Get-GameWindowHandle
    }
    if ($WindowHandle -eq [IntPtr]::Zero) {
        throw "Capture-GameWindowScreenshot: no game window ($Label)"
    }

    $safe = ($Label -replace '[^\w\-]+', '_').Trim('_')
    if (-not $safe) { $safe = "frame" }
    $outPath = Join-Path (Get-VisualArtifactsDir) "$safe.png"
    return Capture-WindowScreenshot -WindowHandle $WindowHandle -OutPath $outPath -Label $Label
}

function Assert-VisualFrameSane {
    param(
        [Parameter(Mandatory)]
        $Stats,
        [Parameter(Mandatory)]
        [string]$Label,
        $Thresholds = $(Get-VisualBaselineThresholds)
    )

    if ($Stats.Width -lt $Thresholds.minWidth -or $Stats.Height -lt $Thresholds.minHeight) {
        $minW = [int]$Thresholds.minWidthPartial
        $minH = [int]$Thresholds.minHeightPartial
    } else {
        $minW = [int]$Thresholds.minWidth
        $minH = [int]$Thresholds.minHeight
    }

    if ($Stats.Width -lt $minW -or $Stats.Height -lt $minH) {
        throw "visual: frame too small for $Label ($($Stats.Width)x$($Stats.Height), min=${minW}x${minH})"
    }
    if ($Stats.NonBlackRatio -lt $Thresholds.minNonBlackRatio) {
        throw ("visual: frame mostly black for $Label (nonBlack={0}, min={1})" -f `
            $Stats.NonBlackRatio, $Thresholds.minNonBlackRatio)
    }
}

function Assert-VisualDelta {
    param(
        [Parameter(Mandatory)]
        $Before,
        [Parameter(Mandatory)]
        $After,
        [Parameter(Mandatory)]
        [string]$Label,
        $Thresholds = $(Get-VisualBaselineThresholds)
    )

    $meanDelta = [Math]::Abs($After.MeanLuminance - $Before.MeanLuminance)
    $varDelta = [Math]::Abs($After.Variance - $Before.Variance)
    $nbDelta = [Math]::Abs($After.NonBlackRatio - $Before.NonBlackRatio)

    $ok = ($meanDelta -ge $Thresholds.minMeanLumDelta) -or `
        ($varDelta -ge $Thresholds.minVarianceDelta) -or `
        ($nbDelta -ge $Thresholds.minNonBlackDelta)

    if (-not $ok) {
        throw ("visual: insufficient pixel delta for {0} (dLum={1}, dVar={2}, dNB={3})" -f `
            $Label, [Math]::Round($meanDelta, 3), [Math]::Round($varDelta, 3), `
            [Math]::Round($nbDelta, 5))
    }

    Write-Host ("visual: delta OK {0} (dLum={1}, dVar={2}, dNB={3})" -f `
        $Label, [Math]::Round($meanDelta, 3), [Math]::Round($varDelta, 3), `
        [Math]::Round($nbDelta, 5))
}

function Test-HarnessVisualEnabled {
  if ($env:MMOD_DEBUG_SKIP_VISUAL -eq "1") { return $false }
  return $true
}

$script:__HarnessVisualManifestEntries = @()
$script:__HarnessVisualLogging = $false

function Test-HarnessVisualSuppressHook {
    return [bool]$script:__HarnessVisualLogging
}

function Set-HarnessVisualSuppressHook {
    param([bool]$Suppress)
    $script:__HarnessVisualLogging = $Suppress
}

$script:__HarnessVisualMilestoneActions = @{
    'intro|main_menu_ready'        = $true
    'intro|force_skip_complete'    = $true
    'intro|blind_skip_complete'    = $true
    'intro|menu_stable_early'      = $true
    'session|start'                = $true
    'session|overlay_ready'        = $true
    'session|inject_begin'         = $true
    'session|core_ready'             = $true
    'session|client_configured'      = $true
    'session|multiplayer_injected'   = $true
    'session|connected_at_menu'      = $true
    'session|in_level'               = $true
    'session|connected_in_level'     = $true
    'session|pass'                   = $true
    'inject|console_open'            = $true
    'inject|module_ready'            = $true
    'inject|console_closed'          = $true
    'menu|entered_gameplay'          = $true
    'menu|main_menu_ready'           = $true
    'menu|loading'                   = $true
    'ui|user_flow_complete'          = $true
    'level|in_level'                 = $true
    'movement|session_begin'         = $true
    'movement|session_end'           = $true
    'movement|pose_ready'            = $true
    'bots|spawn'                     = $true
    'bots|remote_ready'              = $true
    'bots|spawn_settle'              = $true
}

function Reset-HarnessVisualSession {
    $script:__HarnessVisualManifestEntries = @()
    Set-HarnessVisualSuppressHook -Suppress $false
}

function Test-HarnessVisualMilestoneAction {
    param(
        [Parameter(Mandatory)]
        [string]$Phase,
        [Parameter(Mandatory)]
        [string]$Action
    )
    $key = "$Phase|$Action"
    return [bool]$script:__HarnessVisualMilestoneActions[$key]
}

function Write-HarnessVisualManifest {
    if (-not (Test-HarnessVisualEnabled)) { return $null }
    $dir = Get-VisualArtifactsDir
    $manifestPath = Join-Path $dir "manifest.json"
    $frames = @(Get-ChildItem -Path $dir -Filter "*.png" -ErrorAction SilentlyContinue |
        Sort-Object Name |
        ForEach-Object {
            $st = Get-ImageFrameStats -ImagePath $_.FullName -SampleStep 8
            [ordered]@{
                file          = $_.Name
                meanLuminance = $st.MeanLuminance
                variance      = $st.Variance
                nonBlackRatio = $st.NonBlackRatio
                width         = $st.Width
                height        = $st.Height
            }
        })
    $doc = [ordered]@{
        updatedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        frames    = $frames
        milestones = @($script:__HarnessVisualManifestEntries)
    }
    ($doc | ConvertTo-Json -Depth 5) | Set-Content -Path $manifestPath -Encoding UTF8
    return $manifestPath
}

function Invoke-HarnessVisualMilestone {
    param(
        [Parameter(Mandatory)]
        [string]$Step,
        [IntPtr]$WindowHandle = [IntPtr]::Zero,
        [int]$SettleMs = -1,
        [switch]$SkipSaneCheck
    )

    if (-not (Test-HarnessVisualEnabled)) {
        return $null
    }

    $stats = Invoke-VisualCaptureStep -Step $Step -WindowHandle $WindowHandle -SettleMs $SettleMs
    if (-not $SkipSaneCheck) {
        Assert-VisualFrameSane -Stats $stats -Label $Step
    }

    $script:__HarnessVisualManifestEntries += [ordered]@{
        step          = $Step
        ts            = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        meanLuminance = $stats.MeanLuminance
        variance      = $stats.Variance
        nonBlackRatio = $stats.NonBlackRatio
        width         = $stats.Width
        height        = $stats.Height
    }

    return $stats
}

function Invoke-HarnessVisualFromInteraction {
    param(
        [Parameter(Mandatory)]
        [string]$Phase,
        [Parameter(Mandatory)]
        [string]$Action
    )

    if ($Phase -eq 'visual') { return $null }
    if (-not (Test-HarnessVisualMilestoneAction -Phase $Phase -Action $Action)) {
        return $null
    }

    $step = "${Phase}_${Action}"
    return Invoke-HarnessVisualMilestone -Step $step
}

function Invoke-VisualCaptureStep {
    param(
        [Parameter(Mandatory)]
        [string]$Step,
        [IntPtr]$WindowHandle = [IntPtr]::Zero,
        [int]$SettleMs = -1
    )

    $thresholds = Get-VisualBaselineThresholds
    $waitMs = if ($SettleMs -ge 0) { $SettleMs } else { [int]$thresholds.captureSettleMs }
    if ($waitMs -gt 0) {
        Start-Sleep -Milliseconds $waitMs
    }

    $cap = Capture-GameWindowScreenshot -Label $Step -WindowHandle $WindowHandle
    if (Get-Command Write-HarnessInteraction -ErrorAction SilentlyContinue) {
        if (-not (Test-HarnessVisualSuppressHook)) {
            Set-HarnessVisualSuppressHook -Suppress $true
            try {
                Write-HarnessInteraction -Phase "visual" -Action "capture" -Data @{
                    step          = $Step
                    path          = $cap.Path
                    meanLuminance = $cap.Stats.MeanLuminance
                    variance      = $cap.Stats.Variance
                    nonBlackRatio = $cap.Stats.NonBlackRatio
                    width         = $cap.Stats.Width
                    height        = $cap.Stats.Height
                }
            } finally {
                Set-HarnessVisualSuppressHook -Suppress $false
            }
        }
    }
    return $cap.Stats
}

function Test-VisualHarnessPrimitives {
    Initialize-VisualHarnessDrawing
    $dir = Join-Path $env:TEMP "mirroredge-debug-visual-selftest"
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }

    $redPath = Join-Path $dir "solid-red.png"
    $bluePath = Join-Path $dir "solid-blue.png"

    foreach ($pair in @(
            @{ Path = $redPath; Color = [System.Drawing.Color]::FromArgb(255, 220, 40, 40) }
            @{ Path = $bluePath; Color = [System.Drawing.Color]::FromArgb(255, 40, 80, 220) }
        )) {
        $bmp = New-Object System.Drawing.Bitmap 64, 64
        $graphics = [System.Drawing.Graphics]::FromImage($bmp)
        try {
            $graphics.Clear($pair.Color)
        } finally {
            $graphics.Dispose()
            $bmp.Save($pair.Path, [System.Drawing.Imaging.ImageFormat]::Png)
            $bmp.Dispose()
        }
    }

    $redStats = Get-ImageFrameStats -ImagePath $redPath -SampleStep 2
    if ($redStats.MeanR -lt 200 -or $redStats.MeanG -gt 80 -or $redStats.MeanB -gt 80) {
        throw "visual self-test: red stats unexpected (R=$($redStats.MeanR))"
    }

    $blueStats = Get-ImageFrameStats -ImagePath $bluePath -SampleStep 2
    Assert-VisualDelta -Before $redStats -After $blueStats -Label "selftest red vs blue"

    Write-Host "visual: primitives self-test OK"
    return [pscustomobject]@{ Pass = $true }
}

function Test-ModuleManagerOverlayVisual {
    param(
        [scriptblock]$OnMenuClosed = $null,
        [scriptblock]$OnMenuOpen = $null,
        [scriptblock]$OnMenuClosedAgain = $null,
        [scriptblock]$OnConsoleOpen = $null
    )

    if ($env:MMOD_DEBUG_SKIP_VISUAL -eq "1") {
        Write-Host "visual: SKIP (MMOD_DEBUG_SKIP_VISUAL=1)"
        return [pscustomobject]@{ Pass = $true; Skipped = $true }
    }

    $baseline = $null
    $afterClose = $null

    if ($OnMenuClosed) {
        & $OnMenuClosed | Out-Null
        $baseline = Invoke-VisualCaptureStep -Step "menu_closed_baseline"
        Assert-VisualFrameSane -Stats $baseline -Label "menu_closed_baseline"
    }

    if ($OnMenuOpen) {
        & $OnMenuOpen | Out-Null
        $openStats = Invoke-VisualCaptureStep -Step "menu_open"
        Assert-VisualFrameSane -Stats $openStats -Label "menu_open"
        if ($baseline) {
            Assert-VisualDelta -Before $baseline -After $openStats -Label "menu_open vs closed"
        }
    }

    if ($OnMenuClosedAgain) {
        & $OnMenuClosedAgain | Out-Null
        $afterClose = Invoke-VisualCaptureStep -Step "menu_closed_after"
        Assert-VisualFrameSane -Stats $afterClose -Label "menu_closed_after"
    }

    if ($OnConsoleOpen) {
        & $OnConsoleOpen | Out-Null
        $consoleStats = Invoke-VisualCaptureStep -Step "console_open"
        Assert-VisualFrameSane -Stats $consoleStats -Label "console_open"
        $compare = if ($afterClose) { $afterClose } elseif ($baseline) { $baseline } else { $null }
        if ($compare) {
            Assert-VisualDelta -Before $compare -After $consoleStats -Label "console_open vs menu_closed"
        }
    }

    $manifestPath = Join-Path (Get-VisualArtifactsDir) "manifest.json"
    $frames = @(Get-ChildItem -Path (Get-VisualArtifactsDir) -Filter "*.png" |
        Sort-Object Name |
        ForEach-Object {
            $st = Get-ImageFrameStats -ImagePath $_.FullName -SampleStep 8
            [ordered]@{
                file          = $_.Name
                meanLuminance = $st.MeanLuminance
                variance      = $st.Variance
                nonBlackRatio = $st.NonBlackRatio
            }
        })
    ($frames | ConvertTo-Json -Depth 4) | Set-Content -Path $manifestPath -Encoding UTF8
    Write-Host "visual: manifest -> $manifestPath"

    return [pscustomobject]@{ Pass = $true; Skipped = $false; Manifest = $manifestPath }
}

Export-ModuleMember -Function @(
    'Get-VisualArtifactsDir',
    'Get-VisualBaselineThresholds',
    'Get-ImageFrameStats',
    'Capture-WindowScreenshot',
    'Capture-GameWindowScreenshot',
    'Assert-VisualFrameSane',
    'Assert-VisualDelta',
    'Invoke-VisualCaptureStep',
    'Test-VisualHarnessPrimitives',
    'Test-ModuleManagerOverlayVisual',
    'Test-HarnessVisualEnabled',
    'Reset-HarnessVisualSession',
    'Test-HarnessVisualSuppressHook',
    'Set-HarnessVisualSuppressHook',
    'Test-HarnessVisualMilestoneAction',
    'Write-HarnessVisualManifest',
    'Invoke-HarnessVisualMilestone',
    'Invoke-HarnessVisualFromInteraction'
)
