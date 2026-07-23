#Requires -Version 5.1
<#
.SYNOPSIS
  Build launcher + mods and deploy to the game runtime (Binaries).

.PARAMETER NoDeploy
  Build only; skip copying to game Binaries.

.PARAMETER DeployProxy
  Also copy dist\d3d9.dll into game Binaries (proxy mode).

.PARAMETER SkipServer
  Skip Go mmultiplayer-server build.

.PARAMETER Debug
  Debug configuration instead of Release.

.PARAMETER VerifySdk
  Run pure-instruction SDK binary verification after build. Requires MirrorsEdge.exe.
#>
param(
    [switch]$NoDeploy,
    [switch]$Deploy,
    [switch]$DeployProxy,
    [switch]$NoDeployProxy,
    [switch]$SkipServer,
    [switch]$Debug,
    [switch]$VerifySdk
)

$ErrorActionPreference = "Stop"

$Root = $PSScriptRoot
$Dist = Join-Path $Root "dist"

& (Join-Path $Root "tools\Sync-ProductVersion.ps1") -RepoRoot $Root | Out-Null

$CoreDist = Join-Path $Dist "modules\core"
$EngineDist = Join-Path $Dist "modules\engine"
$ModulesDist = Join-Path $Dist "modules"
$ConsoleDist = Join-Path $Dist "modules\mm-console"
$MultiplayerDist = Join-Path $Dist "modules\multiplayer"
$TrainerDist = Join-Path $Dist "modules\trainer"
$DollyDist = Join-Path $Dist "modules\dolly"

function Find-MSBuild {
    $cmd = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($cmd -and (Test-Path $cmd.Source)) {
        return $cmd.Source
    }

    foreach ($base in @("${env:ProgramFiles(x86)}", $env:ProgramFiles)) {
        $vswhere = Join-Path $base "Microsoft Visual Studio\Installer\vswhere.exe"
        if (-not (Test-Path $vswhere)) {
            continue
        }

        $installs = & $vswhere -all -products * -requires Microsoft.Component.MSBuild -property installationPath
        foreach ($install in @($installs)) {
            if (-not $install) {
                continue
            }

            $candidate = Join-Path $install "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $fallbacks = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    Write-Host ""
    Write-Host "MSBuild was not found on this machine." -ForegroundColor Red
    Write-Host "Install Visual Studio 2022 with the 'Desktop development with C++' workload." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Cyan
    Write-Host "  Full IDE:  https://visualstudio.microsoft.com/vs/"
    Write-Host "  Build Tools only (smaller):"
    Write-Host "    winget install Microsoft.VisualStudio.2022.BuildTools"
    Write-Host "    Then in Visual Studio Installer, add MSVC v143 + Windows 10/11 SDK."
    Write-Host ""
    Write-Host "Reopen PowerShell after install, or use 'Developer PowerShell for VS 2022'." -ForegroundColor DarkGray
    throw "MSBuild not found."
}

function Find-Go {
    $go = Get-Command go -ErrorAction SilentlyContinue
    if ($go) { return $go.Source }
    $fallback = "C:\Program Files\Go\bin\go.exe"
    if (Test-Path $fallback) { return $fallback }
    return $null
}

function Test-DxSdkAvailable {
    $vendorInclude = Join-Path $Root "third_party\dxsdk\include\d3dx9.h"
    if (Test-Path $vendorInclude) {
        return $true
    }

    $legacyInclude = "C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Include\d3dx9.h"
    if (Test-Path $legacyInclude) {
        return $true
    }

    return $false
}

function Assert-DxSdkAvailable {
    if (Test-DxSdkAvailable) {
        return
    }

    Write-Host ""
    Write-Host "DirectX SDK (D3DX) headers were not found." -ForegroundColor Red
    Write-Host "Install one of:" -ForegroundColor Yellow
    Write-Host "  .\scripts\setup-dxsdk.ps1   (repo-local NuGet copy, no admin)"
    Write-Host "  Legacy June 2010 DirectX SDK under Program Files (x86)"
    Write-Host ""
    throw "D3DX headers not found."
}

function Resolve-DeploySettings {
    param([string]$Root)

    $settings = @{
        DeployPath   = $null
        DeployProxy  = $false
        TestMachine  = $null
    }

    $configPath = Join-Path $Root "deploy.config.json"
    if (Test-Path $configPath) {
        $utf8 = [System.Text.UTF8Encoding]::new($false)
        $json = ([System.IO.File]::ReadAllText($configPath, $utf8) | ConvertFrom-Json)
        if ($json.deployPath) {
            $settings.DeployPath = [string]$json.deployPath
        } elseif ($json.gameBinaries) {
            $settings.DeployPath = [string]$json.gameBinaries
        }
        if ($null -ne $json.deployProxy) {
            $settings.DeployProxy = [bool]$json.deployProxy
        }
        if ($json.testMachine) {
            $settings.TestMachine = [string]$json.testMachine
        }
    }

    if ($env:ME_DEPLOY_PATH) {
        $settings.DeployPath = $env:ME_DEPLOY_PATH
    } elseif ($env:ME_GAME_BINARIES) {
        $settings.DeployPath = $env:ME_GAME_BINARIES
    }

    if (-not $settings.DeployPath) {
        $settings.DeployPath = Split-Path $Root -Parent
    }

    return $settings
}

function Resolve-BinariesDirectory {
    param([string]$DeployRoot)

    $directExe = Join-Path $DeployRoot "MirrorsEdge.exe"
    if (Test-Path $directExe) {
        return $DeployRoot
    }

    $binaries = Join-Path $DeployRoot "Binaries"
    $binariesExe = Join-Path $binaries "MirrorsEdge.exe"
    if (Test-Path $binariesExe) {
        return $binaries
    }

    return $binaries
}

function Clear-ModulesDirectory {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        return
    }

    Write-Host "==> Cleaning modules tree: $Path" -ForegroundColor DarkGray
    Remove-Item $Path -Recurse -Force
}

function Test-DeployPath {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return $false
    }

    if (Test-Path (Join-Path $Path "MirrorsEdge.exe")) {
        return $true
    }

    return Test-Path (Join-Path $Path "Binaries\MirrorsEdge.exe")
}

function Invoke-Deploy {
    param(
        [string]$DeployRoot,
        [string]$Exe,
        [string]$ManagerDll,
        [string]$ProxyDll,
        [string[]]$ModuleSpecs,
        [bool]$CopyProxy,
        [string]$SettingsJson = "",
        [string]$TestMachine = ""
    )

    if (-not (Test-Path $DeployRoot)) {
        $drive = Split-Path $DeployRoot -Qualifier
        if ($drive -and -not (Test-Path ($drive + "\"))) {
            throw "Deploy drive does not exist: $DeployRoot. Edit deploy.config.json (deployPath) or set ME_DEPLOY_PATH."
        }
        New-Item -ItemType Directory -Force -Path $DeployRoot | Out-Null
    }

    if (-not (Test-DeployPath $DeployRoot)) {
        Write-Host "==> Warning: MirrorsEdge.exe not found under $DeployRoot" -ForegroundColor Yellow
        Write-Host "    Set deployPath in deploy.config.json or ME_DEPLOY_PATH env var." -ForegroundColor Yellow
    }

    Clear-ModulesDirectory -Path (Join-Path $DeployRoot "modules")
    Clear-ModulesDirectory -Path (Join-Path $DeployRoot "dist\modules")

    $deployManager = Join-Path $DeployRoot "modules\module_manager"
    New-Item -ItemType Directory -Force -Path $deployManager | Out-Null

    $deployDist = Join-Path $DeployRoot "dist"
    $deployDistManager = Join-Path $deployDist "modules\module_manager"
    New-Item -ItemType Directory -Force -Path $deployDistManager | Out-Null

    foreach ($launcherName in @("ModuleLauncher.exe", "mirroredge-module-launcher.exe")) {
        $launcherTarget = Join-Path $DeployRoot $launcherName
        try {
            Copy-Item $Exe $launcherTarget -Force
        } catch {
            Write-Host "==> Warning: could not update $launcherName (file in use)" -ForegroundColor Yellow
            Write-Host "    $($_.Exception.Message)" -ForegroundColor DarkYellow
        }
    }

    foreach ($spec in $ModuleSpecs) {
        $parts = $spec -split '\|', 3
        if ($parts.Count -lt 2) { continue }
        $modId = $parts[0]
        $dllPath = $parts[1]
        $extraFiles = @()
        if ($parts.Count -ge 3 -and $parts[2]) {
            $extraFiles = $parts[2] -split ';'
        }

        $deployMod = Join-Path $DeployRoot "modules\$modId"
        $deployDistMod = Join-Path $deployDist "modules\$modId"
        New-Item -ItemType Directory -Force -Path $deployMod | Out-Null
        New-Item -ItemType Directory -Force -Path $deployDistMod | Out-Null

        $dllName = Split-Path $dllPath -Leaf
        Copy-Item $dllPath (Join-Path $deployMod $dllName) -Force
        Copy-Item $dllPath (Join-Path $deployDistMod $dllName) -Force

        foreach ($extra in $extraFiles) {
            if (-not (Test-Path $extra)) { continue }
            $extraName = Split-Path $extra -Leaf
            Copy-Item $extra (Join-Path $deployMod $extraName) -Force
            if ($modId -eq "multiplayer" -and $extraName -like "*server*.exe") {
                Copy-Item $extra (Join-Path $DeployRoot $extraName) -Force
            }
        }
    }

    if ($ManagerDll -and (Test-Path $ManagerDll)) {
        Copy-Item $ManagerDll (Join-Path $deployManager "module_manager.dll") -Force
        Copy-Item $ManagerDll (Join-Path $deployDistManager "module_manager.dll") -Force
    }
    Copy-Item $ProxyDll (Join-Path $deployDist "d3d9.dll") -Force

    $deployedCoreConfig = Join-Path $deployDist "modules\core.config.json"
    if (Test-Path $deployedCoreConfig) {
        Copy-Item $deployedCoreConfig (Join-Path $DeployRoot "modules\core.config.json") -Force
    }

    $settingsSource = $SettingsJson
    if (-not $settingsSource -or -not (Test-Path $settingsSource)) {
        $settingsSource = Join-Path $deployDist "settings.json"
    }
    if (-not (Test-Path $settingsSource)) {
        $settingsSource = Join-Path (Split-Path $deployDist -Parent) "settings.json"
    }
    if (Test-Path $settingsSource) {
        $deploySettingsPath = Join-Path $DeployRoot "settings.json"
        if ($TestMachine) {
            try {
                $settingsRoot = Get-Content $settingsSource -Raw | ConvertFrom-Json
                if (-not $settingsRoot.diagnostics) {
                    $settingsRoot | Add-Member -NotePropertyName diagnostics -NotePropertyValue ([pscustomobject]@{})
                }
                $settingsRoot.diagnostics | Add-Member -NotePropertyName testMachine `
                    -NotePropertyValue $TestMachine -Force
                $settingsRoot | ConvertTo-Json -Depth 8 | Set-Content -Path $deploySettingsPath -Encoding UTF8
            } catch {
                Copy-Item $settingsSource $deploySettingsPath -Force
            }
        } else {
            Copy-Item $settingsSource $deploySettingsPath -Force
        }
    }

    $versionJson = Join-Path $Root "version.json"
    if (Test-Path $versionJson) {
        Copy-Item $versionJson (Join-Path $DeployRoot "VERSION.json") -Force
    }
    $changelog = Join-Path $Root "CHANGELOG.md"
    if (Test-Path $changelog) {
        Copy-Item $changelog (Join-Path $DeployRoot "CHANGELOG.md") -Force
    }

    $binariesDir = Resolve-BinariesDirectory -DeployRoot $DeployRoot
    $deployModules = Join-Path $DeployRoot "modules\core"
    foreach ($stale in @(
        (Join-Path $binariesDir "Client.dll"),
        (Join-Path $binariesDir "imgui.dll"),
        (Join-Path $DeployRoot "modules\imgui\imgui.dll"),
        (Join-Path $deployModules "imgui.dll"),
        (Join-Path $deployManager "imgui.dll"),
        (Join-Path $deployModules "Client.dll"),
        (Join-Path $DeployRoot "PlayWithMod.bat"),
        (Join-Path $binariesDir "ModuleLauncher.exe"),
        (Join-Path $binariesDir "mirroredge-module-launcher.exe")
    )) {
        if (Test-Path $stale) {
            Remove-Item $stale -Force
            Write-Host "==> Removed stale: $stale" -ForegroundColor DarkGray
        }
    }

    $launcherBat = Join-Path $DeployRoot "ModuleLauncher.bat"
    @'
@echo off
setlocal EnableExtensions
cd /d "%~dp0"
start "" "%~dp0ModuleLauncher.exe"
'@ | Set-Content -Path $launcherBat -Encoding ASCII

    if ($CopyProxy) {
        $binariesDir = Resolve-BinariesDirectory -DeployRoot $DeployRoot
        New-Item -ItemType Directory -Force -Path $binariesDir | Out-Null
        Copy-Item $ProxyDll (Join-Path $binariesDir "d3d9.dll") -Force
        Write-Host "==> Deployed d3d9 proxy to: $binariesDir" -ForegroundColor Yellow
    } elseif (Test-Path (Join-Path $binariesDir "d3d9.dll")) {
        $proxyBackup = Join-Path $binariesDir "d3d9.dll.mmproxy.bak"
        if (Test-Path $proxyBackup) { Remove-Item $proxyBackup -Force }
        Rename-Item (Join-Path $binariesDir "d3d9.dll") $proxyBackup -Force
        Write-Host "==> Disabled d3d9 proxy in Binaries (inject mode)." -ForegroundColor Yellow
    }

    Write-Host "==> Deployed to: $DeployRoot" -ForegroundColor Yellow
    Write-Host "    ModuleLauncher.bat  (recommended entry point)" -ForegroundColor DarkGray
    Write-Host "    ModuleLauncher.exe" -ForegroundColor DarkGray
    Write-Host "    settings.json" -ForegroundColor DarkGray
    Write-Host "    modules\core\core.dll" -ForegroundColor DarkGray
    Write-Host "    modules\engine\engine.dll" -ForegroundColor DarkGray
    Write-Host "    modules\core.config.json (legacy)" -ForegroundColor DarkGray
    Write-Host "    modules\mm-console\mm-console.dll" -ForegroundColor DarkGray
    Write-Host "    modules\multiplayer\multiplayer.dll" -ForegroundColor DarkGray
    Write-Host "    modules\trainer\trainer.dll" -ForegroundColor DarkGray
    Write-Host "    modules\dolly\dolly.dll" -ForegroundColor DarkGray
    Write-Host "    modules\module_manager\module_manager.dll" -ForegroundColor DarkGray
    Write-Host "    dist\d3d9.dll" -ForegroundColor DarkGray
}

$deploySettings = Resolve-DeploySettings -Root $Root
$shouldDeploy = -not $NoDeploy
if ($Deploy) { $shouldDeploy = $true }

$copyProxy = $DeployProxy.IsPresent -or $deploySettings.DeployProxy
if (-not $copyProxy -and -not $NoDeployProxy) {
    $copyProxy = $true
}

$msbuild = Find-MSBuild
Assert-DxSdkAvailable
$config = if ($Debug) { "Debug" } else { "Release" }

Clear-ModulesDirectory -Path $ModulesDist
New-Item -ItemType Directory -Force -Path $CoreDist, $EngineDist, $ConsoleDist, $MultiplayerDist, $TrainerDist, $DollyDist | Out-Null

if (-not $SkipServer) {
    $go = Find-Go
    if ($go) {
        Write-Host "==> Building multiplayer-server (Win32)..." -ForegroundColor Cyan
        $serverDir = Join-Path $Root "mods\multiplayer\server"
        $serverOut = Join-Path $MultiplayerDist "multiplayer-server.exe"
        Push-Location $serverDir
        try {
            $prevEap = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            & $go mod tidy 2>&1 | Out-Null
            & $go build -ldflags="-s -w" -o $serverOut .
            if ($LASTEXITCODE -ne 0) { throw "go build exited with $LASTEXITCODE" }
        } catch {
            Write-Host "==> multiplayer-server build failed: $_" -ForegroundColor Yellow
        } finally {
            $ErrorActionPreference = $prevEap
            Pop-Location
        }
    } else {
        Write-Host "==> Skipping multiplayer-server (Go not installed). Use -SkipServer to silence." -ForegroundColor Yellow
    }
}

$resumeLib = Join-Path $Root "tools\lib\Resume-ProcessThreads.ps1"
if (Test-Path $resumeLib) {
    . $resumeLib
    $earlyZombies = @(Get-MirrorsEdgeZombieProcesses | ForEach-Object { $_.Id })
    if ($earlyZombies.Count -gt 0) {
        $zombieMsg = "Mirror's Edge zombie EPROCESS (PID $($earlyZombies -join ', '), 0 threads). Reboot Windows to clear it - blocks d3d9.dll deploy until the PID disappears."
        Write-Host "==> WARNING: $zombieMsg" -ForegroundColor Yellow
        Write-Host "==> Continuing build without d3d9 deploy (modules-only)." -ForegroundColor Yellow
        $copyProxy = $false
    }
}

$gameProc = Get-Process -Name "MirrorsEdge" -ErrorAction SilentlyContinue
if ($gameProc) {
    if (Test-Path $resumeLib) {
        . $resumeLib
        foreach ($proc in @($gameProc)) {
            $n = Resume-ProcessThreads -ProcessId $proc.Id
            if ($n -gt 0) {
                Write-Host "==> Resumed $n suspended thread(s) on MirrorsEdge (PID $($proc.Id))" -ForegroundColor Yellow
            }
        }
        Start-Sleep -Milliseconds 500
        Stop-MirrorsEdgeGameProcesses -WaitSec 2
        $gameProc = Get-Process -Name "MirrorsEdge" -ErrorAction SilentlyContinue
    }
    if ($gameProc) {
        $zombiePids = @(Get-MirrorsEdgeZombieProcesses | ForEach-Object { $_.Id })
        if ($zombiePids.Count -eq 0) {
            $zombiePids = @($gameProc | Where-Object {
                    (Get-ProcessThreadCount -ProcessId $_.Id) -eq 0
                } | ForEach-Object { $_.Id })
        }
        if ($zombiePids.Count -gt 0) {
            $zombieMsg = "Mirror's Edge zombie EPROCESS (PID $($zombiePids -join ', '), 0 threads). Reboot Windows to clear it - blocks d3d9.dll deploy until the PID disappears."
            Write-Host "==> WARNING: $zombieMsg" -ForegroundColor Yellow
            Write-Host "==> Continuing build without d3d9 deploy (modules-only)." -ForegroundColor Yellow
            $copyProxy = $false
        } else {
            throw "Mirror's Edge is running (PID $($gameProc.Id)). Close the game before building - d3d9.dll is locked and the linker fails with LNK1104. Try: .\tools\stop-game.ps1"
        }
    }
}

Write-Host "==> Building solution (Win32 $config)..." -ForegroundColor Cyan
& $msbuild (Join-Path $Root "ModuleLauncher.sln") /p:Configuration=$config /p:Platform=x86 /m /v:m

$exe = Join-Path $Dist "ModuleLauncher.exe"
$coreDll = Join-Path $CoreDist "core.dll"
$engineDll = Join-Path $EngineDist "engine.dll"
$consoleDll = Join-Path $ConsoleDist "mm-console.dll"
$multiplayerDll = Join-Path $MultiplayerDist "multiplayer.dll"
$trainerDll = Join-Path $TrainerDist "trainer.dll"
$dollyDll = Join-Path $DollyDist "dolly.dll"
$managerDll = Join-Path $Dist "modules\module_manager\module_manager.dll"
$proxyDll = Join-Path $Dist "d3d9.dll"

if (-not (Test-Path $exe)) {
    throw "Build output not found: $exe"
}
foreach ($required in @($coreDll, $engineDll, $consoleDll, $multiplayerDll, $trainerDll, $dollyDll, $proxyDll, $managerDll)) {
    if (-not (Test-Path $required)) {
        throw "Build output not found: $required"
    }
}

Copy-Item $exe (Join-Path $Dist "mirroredge-module-launcher.exe") -Force
$settingsJson = Join-Path $Root "settings.json"
if (Test-Path $settingsJson) {
    Copy-Item $settingsJson (Join-Path $Dist "settings.json") -Force
}
$versionJson = Join-Path $Root "version.json"
if (Test-Path $versionJson) {
    Copy-Item $versionJson (Join-Path $Dist "VERSION.json") -Force
}
$changelog = Join-Path $Root "CHANGELOG.md"
if (Test-Path $changelog) {
    Copy-Item $changelog (Join-Path $Dist "CHANGELOG.md") -Force
}
$coreConfig = Join-Path $Root "runtime\core\core.config.json"
if (Test-Path $coreConfig) {
    Copy-Item $coreConfig (Join-Path $ModulesDist "core.config.json") -Force
}

Write-Host "==> Output:" -ForegroundColor Green
Write-Host "  $exe"
Write-Host "  $coreDll"
Write-Host "  $engineDll"
Write-Host "  $consoleDll"
Write-Host "  $multiplayerDll"
Write-Host "  $trainerDll"
Write-Host "  $dollyDll"
Write-Host "  $managerDll"
Write-Host "  $proxyDll"
$serverExe = Join-Path $MultiplayerDist "multiplayer-server.exe"
if (Test-Path $serverExe) {
    Write-Host "  $serverExe"
}

# Optional SDK binary verification. Generated layout references are checked in CI.
if ($VerifySdk) {
    Write-Host ""
    Write-Host "==> SDK binary verification..." -ForegroundColor Cyan
    $verifyScript = Join-Path $Root "tools\sdk-verify\verify-sdk.ps1"
    if (Test-Path $verifyScript) {
        $prevErrorAction = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $verifyScript -BinaryOnly -CheckLiteReference
        if ($LASTEXITCODE -ne 0) {
            Write-Host "==> SDK verification found issues (non-fatal)." -ForegroundColor Yellow
        }
        $ErrorActionPreference = $prevErrorAction
    } else {
        Write-Host "==> verify-sdk.ps1 not found -- skipping." -ForegroundColor DarkGray
    }
}

if ($shouldDeploy) {
    $serverExtra = @()
    if (Test-Path $serverExe) { $serverExtra = @($serverExe) }
    $moduleSpecs = @(
        "core|$coreDll|$(Join-Path $Root 'runtime\core\mod_manifest.json')"
        "engine|$engineDll|"
        "mm-console|$consoleDll|"
        ("multiplayer|$multiplayerDll|" + (@((Join-Path $Root 'mods\multiplayer\mod_manifest.json')) + $serverExtra -join ';'))
        "trainer|$trainerDll|$(Join-Path $Root 'mods\trainer\mod_manifest.json')"
        "dolly|$dollyDll|$(Join-Path $Root 'mods\dolly\mod_manifest.json')"
    )
    Invoke-Deploy -DeployRoot $deploySettings.DeployPath `
        -Exe $exe `
        -ManagerDll $managerDll `
        -ProxyDll $proxyDll `
        -ModuleSpecs $moduleSpecs `
        -CopyProxy $copyProxy `
        -SettingsJson $settingsJson `
        -TestMachine $deploySettings.TestMachine
} else {
    Write-Host "==> Skipped deploy (use default build or deploy.ps1 to deploy)." -ForegroundColor DarkGray
}
