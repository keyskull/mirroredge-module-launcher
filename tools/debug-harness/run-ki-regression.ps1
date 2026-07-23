#Requires -Version 5.1
<#
  Known-issue regression pack (KI-002 Alt+Tab, KI-003 IME). Not part of run-all-scenarios by default.
#>
$ErrorActionPreference = "Continue"

$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$runRoot = Join-Path $PSScriptRoot "run.ps1"
$lib = Join-Path $PSScriptRoot "lib\DebugHarness.psm1"
Import-Module $lib -Force

Initialize-HarnessTestMachine -RepoRoot $repo | Out-Null
Initialize-HarnessTestLogGit -RepoRoot $repo | Out-Null
$runStartedAt = Get-Date
$gitAtStart = Get-HarnessGitSnapshot -RepoRoot $repo

$scenarios = @("alt-tab-menu", "ime-roundtrip")
$stopGame = Join-Path $repo "tools\stop-game.ps1"

function Stop-Session {
    if (Test-Path $stopGame) {
        & $stopGame -IncludeLauncher 2>&1 | Out-Null
    } else {
        Stop-MirrorsEdgeProcesses -IncludeLauncher
    }
    Start-Sleep -Seconds 5
    return @(Get-MirrorsEdgeZombieProcessIds)
}

$results = @()
$first = $true
foreach ($name in $scenarios) {
    $zombieIds = @(Stop-Session)
    if ($zombieIds.Count -gt 0) {
        throw "zombie EPROCESS PID $($zombieIds -join ', '); reboot before KI regression"
    }

    Write-Host ""
    Write-Host "========== $name ==========" -ForegroundColor Cyan

    if ($first) {
        $first = $false
        & $runRoot $name 2>&1 | ForEach-Object { Write-Host $_ }
    } else {
        & $runRoot $name -SkipBuild 2>&1 | ForEach-Object { Write-Host $_ }
    }

    $passed = ($LASTEXITCODE -eq 0)
    $results += [pscustomobject]@{
        Scenario = $name
        Pass     = $passed
        Attempts = 1
        Error    = $(if ($passed) { "" } else { "exit $LASTEXITCODE" })
    }
    if (-not $passed) {
        break
    }
}

Stop-Session | Out-Null

$passCount = @($results | Where-Object { $_.Pass }).Count
Write-Host ""
Write-Host "KI regression: $passCount / $($results.Count) pass"

try {
    Publish-HarnessTestLog -Suite "run-ki-regression" -StartedAt $runStartedAt `
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

if ($passCount -ne $results.Count) {
    exit 1
}
exit 0
