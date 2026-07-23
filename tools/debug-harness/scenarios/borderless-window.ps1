#Requires -Version 5.1
param(
    [switch]$SkipLaunch,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$lib = Join-Path $PSScriptRoot "..\lib\DebugHarness.psm1"
Import-Module $lib -Force

$repo = Get-RepoRoot
$scale = Get-WindowLayoutScale
$layoutSettings = Set-WindowLayoutHarnessSettings -WindowScale $scale
Write-Host ("borderless-window: prepared window {0}x{1} @ {2}%%, render {3}x{4}" -f `
    $layoutSettings.WindowWidth, $layoutSettings.WindowHeight, ($scale * 100), `
    $layoutSettings.RenderWidth, $layoutSettings.RenderHeight)

if ($SkipLaunch) {
    $session = Initialize-DebugSession
    Write-Host "debug session: $($session.SessionId)"
    if (-not $SkipBuild) {
        Invoke-DebugBuild -RepoRoot $repo -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1")
    }
    Write-Host "borderless-window: SKIP launch (session initialized, build done)"
    exit 0
}

$ctx = Start-SplitInjectionSession -RepoRoot $repo -SkipBuild:$SkipBuild `
    -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1") -StopExisting
Assert-ValidHarnessContext -Value $ctx -Scenario "borderless-window"

Wait-ManagerHooksReady -KeepFocused | Out-Null
$logPath = Get-SafeContextLogPath -Context $ctx
Wait-CoreReady -TimeoutSec 120 -LogPath $logPath | Out-Null
try {
    $apply = Invoke-ModControlPipe -Command "APPLY_WINDOW_LAYOUT" -Target manager -TimeoutMs 10000
    Write-Host "borderless-window: initial APPLY_WINDOW_LAYOUT -> $apply"
} catch {
    Write-Host "borderless-window: WARN initial layout apply ($($_.Exception.Message))"
}
Write-Host "borderless-window: hooks ready (scale=$scale), waiting for layout + D3D sync..."

$layout = Wait-GameWindowLayout -TimeoutSec 240 -KeepFocused -WindowScale $scale

Write-Host ("borderless-window: PASS ({0}x{1} target {2}x{3} @ {4}%%)" -f `
    $layout.WindowWidth, $layout.WindowHeight, `
    $layout.TargetWidth, $layout.TargetHeight, ($scale * 100))
Complete-SplitInjectionSession -Context $ctx | Out-Null
exit 0
