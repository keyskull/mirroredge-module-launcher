#Requires -Version 5.1
<#
  Run every harness scenario (cold stop between game sessions).
#>
$ErrorActionPreference = "Continue"

$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$runRoot = Join-Path $PSScriptRoot "run.ps1"
$lib = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
$stopGame = Join-Path $repo "tools\stop-game.ps1"
Import-Module $lib -Force

Initialize-HarnessTestMachine -RepoRoot $repo | Out-Null
Initialize-HarnessTestLogGit -RepoRoot $repo | Out-Null
Write-HarnessCoordination -Status "full-suite-started" -RepoRoot $repo
$runStartedAt = Get-Date
$gitAtStart = Get-HarnessGitSnapshot -RepoRoot $repo

$scenarios = @(
    "verify-harness",
    "ui-launcher",
    "smoke-split",
    "borderless-window",
    "inject-mp",
    "mod-deps",
    "mp-core-functional",
    "multiplayer-functional",
    "trainer-functional",
    "mod-manager-config",
    "dolly-functional",
    "mp-functional",
    "mp-gui-test",
    "ui-module-manager",
    "visual-test",
    "mod-full",
    "mp-playthrough-bots",
    "user-flow",
    "user-full-session"
)

$noGameScenarios = @("verify-harness", "ui-launcher")

function Stop-Session {
    if (Test-Path $stopGame) {
        & $stopGame -IncludeLauncher 2>&1 | Out-Null
    } else {
        Stop-MirrorsEdgeProcesses -IncludeLauncher
    }
    Start-Sleep -Seconds 5
    $zombieIds = @(Get-MirrorsEdgeZombieProcessIds)
    if ($zombieIds.Count -gt 0) {
        Write-Host "FATAL: zombie MirrorsEdge EPROCESS (PID $($zombieIds -join ', ')). Reboot or .\tools\stop-game.ps1 -Elevate" `
            -ForegroundColor Red
    }
    return $zombieIds
}

$results = @()
$suiteBlocked = $false
$blockReason = ""
$firstGameBuildPending = $true

foreach ($name in $scenarios) {
    if ($suiteBlocked) {
        Write-Host ""
        Write-Host "========== $name (SKIPPED) ==========" -ForegroundColor DarkYellow
        $results += [pscustomobject]@{
            Scenario = $name
            Pass     = $false
            Attempts = 0
            Error    = "skipped: $blockReason"
        }
        Write-HarnessCoordination -Status "scenario-skipped" -Scenario $name `
            -Summary @{ pass = $false; skipped = $true; reason = $blockReason } -RepoRoot $repo
        continue
    }

    if ($name -notin $noGameScenarios) {
        $zombieIds = @(Stop-Session)
        if ($zombieIds.Count -gt 0) {
            $suiteBlocked = $true
            $blockReason = "zombie EPROCESS PID $($zombieIds -join ', ')"
            $results += [pscustomobject]@{
                Scenario = $name
                Pass     = $false
                Attempts = 0
                Error    = "blocked: $blockReason"
            }
            Write-HarnessCoordination -Status "suite-blocked" -Scenario $name `
                -Summary @{ pass = $false; zombie = $zombieIds } -RepoRoot $repo
            continue
        }
    }

    Write-Host ""
    Write-Host "========== $name ==========" -ForegroundColor Cyan

    $attempts = 0
    $passed = $false
    $lastErr = ""
    $skipBuild = $true
    if ($name -notin $noGameScenarios -and $firstGameBuildPending) {
        $skipBuild = $false
        $firstGameBuildPending = $false
        Write-Host "run-all: full build+deploy for first game scenario ($name)"
    }

    while ($attempts -lt 2 -and -not $passed) {
        $attempts++
        if ($attempts -gt 1) {
            Write-Host "RETRY $name (attempt $attempts)" -ForegroundColor Yellow
            $zombieIds = @(Stop-Session)
            if ($zombieIds.Count -gt 0) {
                $lastErr = "zombie EPROCESS PID $($zombieIds -join ', ') on retry"
                break
            }
        }

        if ($skipBuild) {
            $out = & $runRoot $name -SkipBuild 2>&1
        } else {
            $out = & $runRoot $name 2>&1
        }
        $out | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -eq 0) {
            $passed = $true
        } else {
            $resultLine = $out | Select-String '^harness-result: ' | Select-Object -Last 1
            if ($resultLine) {
                try {
                    $parsed = ($resultLine.Line -replace '^harness-result: ', '') |
                        ConvertFrom-Json
                    if ($parsed.error) {
                        $lastErr = [string]$parsed.error
                    }
                } catch {}
            }
            if (-not $lastErr) {
                $lastErr = ($out | Select-Object -Last 8) -join " | "
            }
        }
    }

    if (-not $passed -and $name -notin $noGameScenarios) {
        $zombieIds = @(Get-MirrorsEdgeZombieProcessIds)
        if ($zombieIds.Count -gt 0) {
            $suiteBlocked = $true
            $blockReason = "zombie EPROCESS PID $($zombieIds -join ', ') after $name"
            if (-not $lastErr) {
                $lastErr = $blockReason
            }
        }
    }

    $results += [pscustomobject]@{
        Scenario = $name
        Pass     = $passed
        Attempts = $attempts
        Error    = $(if ($passed) { "" } else { $lastErr })
    }

    Write-HarnessCoordination -Status $(if ($passed) { "scenario-pass" } else { "scenario-fail" }) `
        -Scenario $name -Summary @{ pass = $passed; attempts = $attempts; error = $lastErr } `
        -RepoRoot $repo
}

Stop-Session | Out-Null

Write-Host ""
Write-Host "========== HARNESS SUMMARY ==========" -ForegroundColor Cyan
$results | Format-Table -AutoSize

$fail = @($results | Where-Object { -not $_.Pass })
$passCount = @($results | Where-Object { $_.Pass }).Count
$skipped = @($results | Where-Object { $_.Attempts -eq 0 }).Count
Write-Host "PASS: $passCount / $($results.Count)"
if ($skipped -gt 0) {
    Write-Host "SKIPPED/BLOCKED: $skipped (suite blocked: $suiteBlocked)" -ForegroundColor Yellow
}

function Invoke-PublishAndExit {
    param([int]$ExitCode)
    try {
        Publish-HarnessTestLog -Suite "run-all-scenarios" -StartedAt $runStartedAt `
            -GitAtStart $gitAtStart -RepoRoot $repo -Summary @{
            passCount  = $passCount
            totalCount = $results.Count
            results    = @($results)
        } | Out-Null
        if ($env:MMOD_HARNESS_LOG_PUSH -ne "0") {
            Push-HarnessTestLog -RepoRoot $repo | Out-Null
        }
    } catch {
        Write-Host "test-logs: publish failed ($($_.Exception.Message))" -ForegroundColor Yellow
    }
    exit $ExitCode
}

if ($fail.Count -gt 0) {
    Write-Host "FAILED: $($fail.Scenario -join ', ')" -ForegroundColor Red
    Write-HarnessCoordination -Status "full-suite-failed" -Summary @{
        pass    = $passCount
        total   = $results.Count
        fail    = @($fail.Scenario)
        blocked = $suiteBlocked
    } -RepoRoot $repo
    Invoke-PublishAndExit -ExitCode 1
}

Write-HarnessCoordination -Status "full-suite-passed" -Summary @{
    pass  = $passCount
    total = $results.Count
} -RepoRoot $repo

Write-Host "ALL SCENARIOS PASSED" -ForegroundColor Green
Invoke-PublishAndExit -ExitCode 0
