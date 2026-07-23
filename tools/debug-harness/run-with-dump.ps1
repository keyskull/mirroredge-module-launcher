#Requires -Version 5.1
<#
.SYNOPSIS
  Run a debug harness scenario with ProcDump attached to MirrorsEdge.exe on unhandled exceptions.
#>
param(
    [string]$Scenario = "mp-playthrough-bots",
    [string[]]$ScenarioArgs = @("-BotCount", "2", "-PlaySeconds", "15"),
    [switch]$SkipProcDump
)

$ErrorActionPreference = "Stop"
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dumpDir = Join-Path $env:TEMP "mirroredge-dumps"
New-Item -ItemType Directory -Force -Path $dumpDir | Out-Null

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$pass = $false
$errorText = $null
$dumpProc = $null

try {
    $harnessJob = Start-Job -ScriptBlock {
        param($Repo, $Scenario, $Args)
        Set-Location $Repo
        $runPs1 = Join-Path $Repo "tools\debug-harness\run.ps1"
        & $runPs1 $Scenario @Args 2>&1
    } -ArgumentList $repo, $Scenario, $ScenarioArgs

    if (-not $SkipProcDump) {
        $procDump = Join-Path $env:TEMP "procdump.exe"
        if (-not (Test-Path $procDump)) {
            Write-Host "Downloading procdump.exe to $procDump"
            Invoke-WebRequest -Uri "https://live.sysinternals.com/procdump.exe" `
                -OutFile $procDump -UseBasicParsing
        }

        $deadline = (Get-Date).AddMinutes(12)
        while ((Get-Date) -lt $deadline) {
            $game = Get-Process -Name MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($game -and -not $dumpProc) {
                Write-Host "Attaching procdump to PID $($game.Id) (unhandled exceptions, full dump)"
                $dumpProc = Start-Process -FilePath $procDump -ArgumentList @(
                    "-accepteula", "-ma", "-e", "-p", $game.Id,
                    (Join-Path $dumpDir "MirrorsEdge-crash")
                ) -PassThru -WindowStyle Hidden
            }
            if ($harnessJob.State -ne "Running") { break }
            Start-Sleep -Milliseconds 500
        }
    } else {
        while ($harnessJob.State -eq "Running") {
            Start-Sleep -Milliseconds 500
        }
    }

    $output = Receive-Job $harnessJob -Wait -AutoRemoveJob
    $output | ForEach-Object { Write-Host $_ }

    $resultLine = $output | Where-Object { $_ -match '^harness-result:' } | Select-Object -Last 1
    if ($resultLine) {
        $pass = ($resultLine -replace '^harness-result:\s*', '' | ConvertFrom-Json).pass
    } else {
        $pass = $true
    }
} catch {
    $errorText = $_.Exception.Message
    $pass = $false
} finally {
    if ($dumpProc -and -not $dumpProc.HasExited) {
        Stop-Process -Id $dumpProc.Id -Force -ErrorAction SilentlyContinue
    }
}

$dumps = @(Get-ChildItem $dumpDir -Filter "*.dmp" -ErrorAction SilentlyContinue)
if ($dumps.Count -gt 0) {
    Write-Host "Crash dumps in $dumpDir"
    $dumps | Sort-Object LastWriteTime -Descending | Format-Table Name, Length, LastWriteTime
}

$sw.Stop()
$result = @{
    scenario = $Scenario
    layer    = "L2"
    pass     = [bool]$pass
    durationMs = [int]$sw.ElapsedMilliseconds
    dumpDir  = $dumpDir
    dumpCount = $dumps.Count
}
if ($errorText) { $result.error = $errorText }

Write-Host ("harness-result: {0}" -f ($result | ConvertTo-Json -Compress))
if (-not $pass) { exit 1 }
