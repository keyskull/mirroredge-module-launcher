# Test all new safe wrappers added to MeSdk
param(
    [int]$TimeoutSec = 120,
    [string]$GamePath = $(if ($env:ME_DEPLOY_PATH) { $env:ME_DEPLOY_PATH } else { "" })
)

$ErrorActionPreference = "Stop"
Write-Host "=== Safe Wrapper Access Test ===" -ForegroundColor Cyan

# ---- Launch ModuleLauncher (deploys proxy, waits for module_manager) ----
Write-Host "[1/6] Launching ModuleLauncher..." -ForegroundColor Yellow
$launcher = Join-Path $GamePath "ModuleLauncher.exe"
$launcherProc = Start-Process -FilePath $launcher -ArgumentList "/auto" -WorkingDirectory $GamePath -PassThru
Start-Sleep -Seconds 3
Write-Host "ModuleLauncher PID: $($launcherProc.Id)" -ForegroundColor Gray

# ---- Launch the game separately ----
Write-Host "[2/6] Launching MirrorsEdge.exe..." -ForegroundColor Yellow
$gameExe = Join-Path (Join-Path $GamePath "Binaries") "MirrorsEdge.exe"
$gameProc = Start-Process -FilePath $gameExe -WorkingDirectory (Join-Path $GamePath "Binaries") -PassThru
Write-Host "Game PID: $($gameProc.Id)" -ForegroundColor Gray

# ---- Wait for game window ----
Write-Host "Waiting for game window (max 30s)..." -ForegroundColor Gray
$hwnd = 0
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    try {
        $procs = Get-Process -Name "MirrorsEdge" -ErrorAction Stop
        if ($procs -and $procs.Count -gt 0) {
            $hwnd = $procs[0].MainWindowHandle
            if ($hwnd -ne 0) { break }
        }
    } catch { }
}
if (-not $hwnd -or $hwnd -eq 0) {
    Write-Host "ERROR: Game window not found" -ForegroundColor Red
    exit 1
}
Write-Host "Game window: $hwnd" -ForegroundColor Green

# ---- Wait for core pipe ----
Write-Host "[3/6] Waiting for core IPC pipe (max 180s)..." -ForegroundColor Yellow
$pipeName = "\\.\pipe\mmod-core-ctrl"
$client = $null
$reader = $null
$writer = $null
$pipeDeadline = [DateTime]::Now.AddSeconds(180)
Write-Host "Waiting for core IPC pipe (max 180s, DXVK shader compile may take time)..." -ForegroundColor Gray
for ($i = 0; $i -lt 360; $i++) {
    try {
        $client = New-Object System.IO.Pipes.NamedPipeClientStream(".", "mmod-core-ctrl")
        $client.Connect(500)
        $reader = New-Object System.IO.StreamReader($client)
        $writer = New-Object System.IO.StreamWriter($client)
        $writer.AutoFlush = $true
        break
    } catch {
        # Show progress every 15 seconds
        if ($i % 30 -eq 0 -and $i -gt 0) { Write-Host "  ... still waiting ($([int]($i/2))s)" -ForegroundColor DarkGray }
        Start-Sleep -Milliseconds 500
    }
}
if (-not $client -or -not $client.IsConnected) {
    Write-Host "ERROR: Core pipe never connected (RDP may block D3D9 init)" -ForegroundColor Red
    if ($gameProc -and -not $gameProc.HasExited) {
        Write-Host "Game is still running but core didn't initialize." -ForegroundColor DarkYellow
    }
    exit 1
}
Write-Host "Core pipe connected" -ForegroundColor Green

# Helper: send a command and read response
function Send-CoreCmd($cmd) {
    $writer.WriteLine($cmd)
    $reader.ReadLine()
}

# ---- PING check ----
$pong = Send-CoreCmd "PING"
Write-Host "PING -> $pong" -ForegroundColor Gray
if ($pong -ne "PONG") {
    Write-Host "ERROR: Core not responding" -ForegroundColor Red
    exit 1
}

# ---- Load a level if needed ----
Write-Host "[4/6] Loading tutorial_p..." -ForegroundColor Yellow
$result = Send-CoreCmd "CONSOLE open tutorial_p"
Write-Host "LOAD_MAP: $result" -ForegroundColor Gray

Write-Host "Waiting for level load (25s)..." -ForegroundColor Gray
Start-Sleep -Seconds 25

# ---- Check status ----
$status = Send-CoreCmd "GET_STATUS"
$statusShort = $status.Substring(0, [Math]::Min(300, $status.Length))
Write-Host "Status: $statusShort" -ForegroundColor Gray

# ---- Run the test ----
Write-Host "[5/6] Running TEST_SAFE_WRAPPERS..." -ForegroundColor Yellow
$testResult = Send-CoreCmd "TEST_SAFE_WRAPPERS"
Write-Host ""
Write-Host "=== Test Results ===" -ForegroundColor Cyan
Write-Host $testResult
Write-Host ""

# ---- Parse and summarize ----
Write-Host "[6/6] Summary" -ForegroundColor Yellow
try {
    $json = $testResult | ConvertFrom-Json
    if ($json.error) {
        Write-Host "ERROR: $($json.error)" -ForegroundColor Red
    } else {
        $pass = $json.summary.passed
        $fail = $json.summary.failed
        Write-Host "Passed: $pass  Failed: $fail" -ForegroundColor $(if ($fail -eq 0) {"Green"} else {"Red"})
        foreach ($prop in @("pri","gri","team_info","checkpoint","stashpoint","nav_point","map_info")) {
            $r = $json.$prop
            $color = if ($r.ok) {"Green"} else {"DarkGray"}
            Write-Host "  $prop : ok=$($r.ok) $($r.detail)" -ForegroundColor $color
        }
        Write-Host "  lookup_world: $($json.lookup_world)" -ForegroundColor Gray
        Write-Host "  lookup_controller: $($json.lookup_controller)" -ForegroundColor Gray
        if ($fail -eq 0) {
            Write-Host "`nALL TESTS PASSED" -ForegroundColor Green
        } else {
            Write-Host "`nSOME TESTS FAILED (see details above)" -ForegroundColor Red
        }
    }
} catch {
    Write-Host "Could not parse JSON; raw output above" -ForegroundColor Red
}

# ---- Cleanup ----
$writer.Dispose()
$reader.Dispose()
$client.Dispose()

Write-Host "`nTest complete." -ForegroundColor Cyan
