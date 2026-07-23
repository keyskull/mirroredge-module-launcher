#Requires -Version 5.1
Set-StrictMode -Version Latest

if (-not ("Win32ProcessKill" -as [type])) {
    Add-Type @"
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
public static class Win32ProcessKill {
    const uint TOKEN_ADJUST_PRIVILEGES = 0x0020;
    const uint TOKEN_QUERY = 0x0008;
    const uint SE_PRIVILEGE_ENABLED = 0x00000002;
    const uint PROCESS_TERMINATE = 0x0001;
    const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool OpenProcessToken(IntPtr processHandle, uint desiredAccess, out IntPtr tokenHandle);
    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool LookupPrivilegeValue(string lpSystemName, string lpName, out long lpLuid);
    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool AdjustTokenPrivileges(IntPtr tokenHandle, bool disableAllPrivileges,
        ref TOKEN_PRIVILEGES newState, uint bufferLength, IntPtr previousState, IntPtr returnLength);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool TerminateProcess(IntPtr hProcess, uint uExitCode);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr hObject);

    [StructLayout(LayoutKind.Sequential)]
    struct TOKEN_PRIVILEGES {
        public uint PrivilegeCount;
        public long Luid;
        public uint Attributes;
    }

    public static void EnableDebugPrivilege() {
        IntPtr token;
        if (!OpenProcessToken(Process.GetCurrentProcess().Handle,
                TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, out token)) {
            return;
        }
        long luid;
        if (!LookupPrivilegeValue(null, "SeDebugPrivilege", out luid)) {
            CloseHandle(token);
            return;
        }
        var tp = new TOKEN_PRIVILEGES {
            PrivilegeCount = 1,
            Luid = luid,
            Attributes = SE_PRIVILEGE_ENABLED
        };
        AdjustTokenPrivileges(token, false, ref tp, 0, IntPtr.Zero, IntPtr.Zero);
        CloseHandle(token);
    }

    public static int TryTerminate(int processId) {
        EnableDebugPrivilege();
        IntPtr h = OpenProcess(PROCESS_TERMINATE, false, processId);
        if (h == IntPtr.Zero) {
            return Marshal.GetLastWin32Error();
        }
        bool ok = TerminateProcess(h, 1);
        int err = ok ? 0 : Marshal.GetLastWin32Error();
        CloseHandle(h);
        return err;
    }
}
"@
}

if (-not ("Win32ThreadResume" -as [type])) {
    Add-Type @"
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
public static class Win32ThreadResume {
    const uint THREAD_SUSPEND_RESUME = 0x0002;
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenThread(uint dwDesiredAccess, bool bInheritHandle, uint dwThreadId);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint ResumeThread(IntPtr hThread);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr hThread);

    public static int ResumeProcessThreads(int processId) {
        int resumed = 0;
        Process proc;
        try {
            proc = Process.GetProcessById(processId);
        } catch {
            return 0;
        }
        foreach (ProcessThread pt in proc.Threads) {
            IntPtr h = OpenThread(THREAD_SUSPEND_RESUME, false, (uint)pt.Id);
            if (h == IntPtr.Zero) {
                continue;
            }
            bool changed = false;
            for (int i = 0; i < 32; i++) {
                uint prev = ResumeThread(h);
                if (prev == 0xFFFFFFFF) {
                    break;
                }
                if (prev > 0) {
                    changed = true;
                }
                if (prev <= 1) {
                    break;
                }
            }
            CloseHandle(h);
            if (changed) {
                resumed++;
            }
        }
        return resumed;
    }
}
"@
}

function Resume-ProcessThreads {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ProcessId
    )
    return [Win32ThreadResume]::ResumeProcessThreads($ProcessId)
}

function Get-ProcessThreadCount {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ProcessId
    )

    try {
        $cim = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" `
            -ErrorAction Stop
        if ($cim) {
            return [int]$cim.ThreadCount
        }
    } catch {}

    try {
        $proc = Get-Process -Id $ProcessId -ErrorAction Stop
        return $proc.Threads.Count
    } catch {
        return -1
    }
}

function Get-MirrorsEdgeZombieProcesses {
    $zombies = @()
    foreach ($proc in @(Get-Process MirrorsEdge -ErrorAction SilentlyContinue)) {
        $count = Get-ProcessThreadCount -ProcessId $proc.Id
        if ($count -eq 0) {
            $zombies += $proc
        }
    }
    return $zombies
}

function Stop-ProcessForce {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process
    )

    $processId = $Process.Id
    $name = $Process.ProcessName

    $threadCount = Get-ProcessThreadCount -ProcessId $processId
    if ($threadCount -lt 0) {
        $threadCount = $Process.Threads.Count
    }

    if ($threadCount -eq 0) {
        Write-Warning "stop-game: $name pid=$processId is a zombie (0 threads). Resume is useless."
    } else {
        $n = Resume-ProcessThreads -ProcessId $processId
        if ($n -gt 0) {
            Write-Host "resume-threads: $name pid=$processId resumed $n thread(s)"
        }
        Start-Sleep -Milliseconds 500
    }

    if ($threadCount -ne 0) {
        & taskkill.exe /F /PID $processId /T 2>$null | Out-Null
        Start-Sleep -Milliseconds 300
    }

    if (Get-Process -Id $processId -ErrorAction SilentlyContinue) {
        $err = [Win32ProcessKill]::TryTerminate($processId)
        if ($err -eq 0) {
            Write-Host "force-terminate: $name pid=$processId (SeDebugPrivilege)"
        }
    }

    Start-Sleep -Milliseconds 200
    if (Get-Process -Id $processId -ErrorAction SilentlyContinue) {
        try {
            $r = Get-CimInstance Win32_Process -Filter "ProcessId=$processId" |
                Invoke-CimMethod -MethodName Terminate -ErrorAction Stop
            if ($r.ReturnValue -eq 0) {
                Write-Host "wmi-terminate: $name pid=$processId"
            }
        } catch {
            # ignore
        }
    }

    if (Get-Process -Id $processId -ErrorAction SilentlyContinue) {
        if ($threadCount -eq 0) {
            Write-Warning @"
stop-game: $name pid=$processId still listed (zombie EPROCESS).
  Reboot Windows to clear it — blocks d3d9.dll deploy until the PID disappears.
  Or retry elevated: .\tools\stop-game.ps1 -Elevate
"@
        } else {
            Write-Warning "stop-game: $name pid=$processId still alive. Try elevated PowerShell: .\tools\stop-game.ps1 -Elevate"
        }
        return $false
    }
    return $true
}

function Stop-MirrorsEdgeGameProcesses {
    param(
        [switch]$IncludeLauncher,
        [int]$WaitSec = 3
    )

    $names = @("MirrorsEdge")
    if ($IncludeLauncher) {
        $names += @("ModuleLauncher", "mirroredge-module-launcher")
    }

    $prevPreference = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    try {
        foreach ($name in $names) {
            foreach ($proc in @(Get-Process $name -ErrorAction SilentlyContinue)) {
                [void](Stop-ProcessForce -Process $proc)
            }
        }
    } finally {
        $ErrorActionPreference = $prevPreference
    }
    Start-Sleep -Seconds $WaitSec
}
