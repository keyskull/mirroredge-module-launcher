$Repo = Split-Path $PSScriptRoot -Parent
$GameRoot = $env:ME_DEPLOY_PATH
if (-not $GameRoot) {
  $cfg = Join-Path (Split-Path $PSScriptRoot -Parent) "deploy.config.json"
  if (Test-Path $cfg) {
    $GameRoot = (Get-Content $cfg -Raw | ConvertFrom-Json).deployPath
  }
}
if (-not $GameRoot) { throw "Set ME_DEPLOY_PATH or deploy.config.json deployPath" }

Add-Type -TypeDefinition @"
using System; using System.IO; using System.IO.Pipes;
public class Pipe {
    public static string P(string n,string c) {
        try {
            var p=new NamedPipeClientStream(".",n,PipeDirection.InOut);
            p.Connect(3000);
            var w=new StreamWriter(p); w.AutoFlush=true;
            var r=new StreamReader(p);
            w.WriteLine(c);
            System.Threading.Thread.Sleep(500);
            string s=r.ReadLine();
            p.Close();
            return s;
        } catch(Exception e) { return "ERR:"+e.Message; }
    }
}
"@

# Kill existing
Get-Process "MirrorsEdge","ModuleLauncher","multiplayer-server" -EA SilentlyContinue | Stop-Process -Force
Start-Sleep 2

# Launch
$l = Start-Process "$GameRoot\ModuleLauncher.exe" -PassThru
Start-Sleep 5

# Wait for hooks
for ($i = 0; $i -lt 120; $i++) {
    $s = [Pipe]::P("mirroredge_module_manager_control", "GET_STATUS")
    if ($s -match '"hooksInstalled":\s*true') {
        Write-Host "hooks ready ($i s)"
        break
    }
    Start-Sleep 1
}

# Test INJECT_KEY
Write-Host "Testing INJECT_KEY..."
$r = [Pipe]::P("mirroredge_module_control", "INJECT_KEY 0x0D")
Write-Host "Enter down: $r"
$r = [Pipe]::P("mirroredge_module_control", "INJECT_KEY 0x0D UP")
Write-Host "Enter up: $r"
$r = [Pipe]::P("mirroredge_module_control", "INJECT_KEY 0x1B")
Write-Host "Esc down: $r"

Start-Sleep 3

Write-Host "--- DIAG ---"
Get-Content "$env:TEMP\mirroredge-inject.log" -EA SilentlyContinue

# Cleanup
Get-Process "MirrorsEdge","ModuleLauncher" -EA SilentlyContinue | Stop-Process -Force
Write-Host "done"
