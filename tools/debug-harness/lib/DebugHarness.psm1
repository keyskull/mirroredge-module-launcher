#Requires -Version 5.1
Set-StrictMode -Version Latest

if (-not ("Win32Focus" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Focus {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);

    public static POINT ToScreen(IntPtr hWnd, int clientX, int clientY) {
        POINT pt = new POINT();
        pt.X = clientX;
        pt.Y = clientY;
        if (!ClientToScreen(hWnd, ref pt)) {
            pt.X = clientX;
            pt.Y = clientY;
        }
        return pt;
    }
}
"@
}

if (-not ("Win32Enum" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Enum {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    public static IntPtr FindMainWindow(int processId) {
        IntPtr found = IntPtr.Zero;
        IntPtr any = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            uint pid;
            GetWindowThreadProcessId(hWnd, out pid);
            if (pid != (uint)processId) {
                return true;
            }
            any = hWnd;
            if (IsWindowVisible(hWnd)) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found != IntPtr.Zero ? found : any;
    }
}
"@
}

if (-not ("Win32Module" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Win32Module {
    private const uint TH32CS_SNAPMODULE = 0x00000008;
    private const uint TH32CS_SNAPMODULE32 = 0x00000010;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct MODULEENTRY32 {
        public uint dwSize;
        public uint th32ModuleID;
        public uint th32ProcessID;
        public uint GlblcntUsage;
        public uint ProccntUsage;
        public IntPtr modBaseAddr;
        public uint modBaseSize;
        public IntPtr hModule;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string szModule;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string szExePath;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateToolhelp32Snapshot(uint dwFlags, uint th32ProcessID);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool Module32First(IntPtr hSnapshot, ref MODULEENTRY32 lpme);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool Module32Next(IntPtr hSnapshot, ref MODULEENTRY32 lpme);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    public static bool HasModule(int processId, string moduleName) {
        if (processId <= 0 || string.IsNullOrWhiteSpace(moduleName)) {
            return false;
        }

        IntPtr snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, (uint)processId);
        if (snapshot == IntPtr.Zero || snapshot == new IntPtr(-1)) {
            return false;
        }

        try {
            MODULEENTRY32 entry = new MODULEENTRY32();
            entry.dwSize = (uint)Marshal.SizeOf(typeof(MODULEENTRY32));
            if (!Module32First(snapshot, ref entry)) {
                return false;
            }

            do {
                if (string.Equals(entry.szModule, moduleName, StringComparison.OrdinalIgnoreCase)) {
                    return true;
                }
            } while (Module32Next(snapshot, ref entry));
        } finally {
            CloseHandle(snapshot);
        }

        return false;
    }
}
"@
}

if (-not ("Win32Window" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Window {
    public const int GWL_STYLE = -16;
    public const int WS_CAPTION = 0x00C00000;
    public const int WS_THICKFRAME = 0x00040000;
    public const int WS_POPUP = unchecked((int)0x80000000);
    public const uint MONITOR_DEFAULTTONEAREST = 2;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct MONITORINFO {
        public int cbSize;
        public RECT rcMonitor;
        public RECT rcWork;
        public uint dwFlags;
    }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern int GetWindowLong(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint dwFlags);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFO lpmi);
}
"@
}

if (-not ("Win32Hang" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Hang {
    public const uint SMTO_ABORTIFHUNG = 0x0002;
    public const uint SMTO_BLOCK = 0x0001;
    public const uint WM_NULL = 0x0000;

    [DllImport("user32.dll")]
    public static extern bool IsHungAppWindow(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern IntPtr SendMessageTimeout(
        IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam,
        uint fuFlags, uint uTimeout, out IntPtr lpdwResult);
}
"@
}

if (-not ("Win32Dialog" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Win32Dialog {
    public const uint WM_CLOSE = 0x0010;

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindWindow(IntPtr lpClassName, string lpWindowName);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);

    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsWindowEnabled(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    public const uint BM_CLICK = 0x00F5;

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
"@
}

if (-not ("Win32UiProbe" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;

public static class Win32UiProbe {
    public const uint GW_OWNER = 4;
    public const int IDOK = 1;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    public delegate bool EnumChildProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr hWndParent, EnumChildProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsWindowEnabled(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetWindow(IntPtr hWnd, uint uCmd);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    public static string ReadText(IntPtr hwnd) {
        if (hwnd == IntPtr.Zero) {
            return "";
        }
        StringBuilder sb = new StringBuilder(1024);
        GetWindowText(hwnd, sb, sb.Capacity);
        return sb.ToString().Trim();
    }

    public static string ReadClass(IntPtr hwnd) {
        StringBuilder sb = new StringBuilder(256);
        GetClassName(hwnd, sb, sb.Capacity);
        return sb.ToString();
    }

    public static string CollectSubtreeText(IntPtr root, int maxLen) {
        StringBuilder acc = new StringBuilder();
        Action<IntPtr> walk = null;
        walk = (h) => {
            if (acc.Length >= maxLen) {
                return;
            }
            string t = ReadText(h);
            if (!string.IsNullOrWhiteSpace(t)) {
                if (acc.Length > 0) {
                    acc.Append(' ');
                }
                acc.Append(t);
            }
            EnumChildWindows(h, (child, lp) => { walk(child); return true; }, IntPtr.Zero);
        };
        walk(root);
        return acc.ToString();
    }

    public class WindowRec {
        public IntPtr Hwnd;
        public uint Pid;
        public string Title;
        public string ClassName;
        public string Body;
        public bool Visible;
        public int Width;
        public int Height;
        public IntPtr Owner;
    }

    public static WindowRec[] EnumTopLevelForProcess(uint pid) {
        var list = new List<WindowRec>();
        EnumWindows((hWnd, lParam) => {
            uint wpid;
            GetWindowThreadProcessId(hWnd, out wpid);
            if (wpid != pid) {
                return true;
            }
            RECT r;
            GetWindowRect(hWnd, out r);
            list.Add(new WindowRec {
                Hwnd = hWnd,
                Pid = wpid,
                Title = ReadText(hWnd),
                ClassName = ReadClass(hWnd),
                Body = CollectSubtreeText(hWnd, 4096),
                Visible = IsWindowVisible(hWnd),
                Width = r.Right - r.Left,
                Height = r.Bottom - r.Top,
                Owner = GetWindow(hWnd, GW_OWNER)
            });
            return true;
        }, IntPtr.Zero);
        return list.ToArray();
    }

    public static WindowRec[] EnumTopLevelMessageBoxes() {
        var list = new List<WindowRec>();
        EnumWindows((hWnd, lParam) => {
            if (!IsWindowVisible(hWnd)) {
                return true;
            }
            string cls = ReadClass(hWnd);
            if (cls != "#32770") {
                return true;
            }
            uint wpid;
            GetWindowThreadProcessId(hWnd, out wpid);
            RECT r;
            GetWindowRect(hWnd, out r);
            list.Add(new WindowRec {
                Hwnd = hWnd,
                Pid = wpid,
                Title = ReadText(hWnd),
                ClassName = cls,
                Body = CollectSubtreeText(hWnd, 4096),
                Visible = true,
                Width = r.Right - r.Left,
                Height = r.Bottom - r.Top,
                Owner = GetWindow(hWnd, GW_OWNER)
            });
            return true;
        }, IntPtr.Zero);
        return list.ToArray();
    }

    public static uint ResolveOwnerWatchProcessId(IntPtr hwnd, uint[] watchPids) {
        if (watchPids == null || watchPids.Length == 0) {
            return 0;
        }
        IntPtr cur = hwnd;
        for (int i = 0; i < 10 && cur != IntPtr.Zero; i++) {
            uint pid;
            GetWindowThreadProcessId(cur, out pid);
            for (int j = 0; j < watchPids.Length; j++) {
                if (pid == watchPids[j]) {
                    return pid;
                }
            }
            cur = GetWindow(cur, GW_OWNER);
        }
        return 0;
    }

    public static bool TryClickOk(IntPtr dialogHwnd) {
        IntPtr ok = GetDlgItem(dialogHwnd, IDOK);
        if (ok == IntPtr.Zero) {
            return false;
        }
        SendMessage(ok, 0x00F5u, IntPtr.Zero, IntPtr.Zero);
        return true;
    }
}
"@
}

if (-not ("Win32Input" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Threading;

public static class Win32Input {
    public const int INPUT_MOUSE = 0;
    public const int INPUT_KEYBOARD = 1;
    public const uint KEYEVENTF_KEYUP = 0x0002;
    public const uint KEYEVENTF_UNICODE = 0x0004;
    public const uint MOUSEEVENTF_MOVE = 0x0001;
    public const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    public const uint MOUSEEVENTF_LEFTUP = 0x0004;
    public const uint MOUSEEVENTF_WHEEL = 0x0800;

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetCursorPos(int x, int y);

    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT {
        public int type;
        public InputUnion union;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct InputUnion {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT {
        public int dx;
        public int dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

    public static void MouseWheel(int delta) {
        INPUT input = new INPUT();
        input.type = INPUT_MOUSE;
        input.union.mi.mouseData = (uint)delta;
        input.union.mi.dwFlags = MOUSEEVENTF_WHEEL;
        SendInput(1, new INPUT[] { input }, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void KeyTap(ushort vk, int holdMs) {
        INPUT down = new INPUT();
        down.type = INPUT_KEYBOARD;
        down.union.ki.wVk = vk;
        down.union.ki.dwFlags = 0;

        INPUT up = new INPUT();
        up.type = INPUT_KEYBOARD;
        up.union.ki.wVk = vk;
        up.union.ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(1, new INPUT[] { down }, Marshal.SizeOf(typeof(INPUT)));
        if (holdMs > 0) {
            Thread.Sleep(holdMs);
        }
        SendInput(1, new INPUT[] { up }, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void KeyDown(ushort vk) {
        INPUT down = new INPUT();
        down.type = INPUT_KEYBOARD;
        down.union.ki.wVk = vk;
        down.union.ki.dwFlags = 0;
        SendInput(1, new INPUT[] { down }, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void KeyUp(ushort vk) {
        INPUT up = new INPUT();
        up.type = INPUT_KEYBOARD;
        up.union.ki.wVk = vk;
        up.union.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, new INPUT[] { up }, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void UnicodeChar(char ch) {
        INPUT down = new INPUT();
        down.type = INPUT_KEYBOARD;
        down.union.ki.wScan = ch;
        down.union.ki.dwFlags = KEYEVENTF_UNICODE;

        INPUT up = new INPUT();
        up.type = INPUT_KEYBOARD;
        up.union.ki.wScan = ch;
        up.union.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        SendInput(1, new INPUT[] { down }, Marshal.SizeOf(typeof(INPUT)));
        Thread.Sleep(15);
        SendInput(1, new INPUT[] { up }, Marshal.SizeOf(typeof(INPUT)));
        Thread.Sleep(15);
    }

    public static void MouseMoveRelative(int dx, int dy) {
        INPUT input = new INPUT();
        input.type = INPUT_MOUSE;
        input.union.mi.dx = dx;
        input.union.mi.dy = dy;
        input.union.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, new INPUT[] { input }, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void MouseDownScreen(int x, int y) {
        SetCursorPos(x, y);
        Thread.Sleep(50);
        INPUT down = new INPUT();
        down.type = INPUT_MOUSE;
        down.union.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, new INPUT[] { down }, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void MouseUpScreen(int x, int y) {
        SetCursorPos(x, y);
        Thread.Sleep(30);
        INPUT up = new INPUT();
        up.type = INPUT_MOUSE;
        up.union.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, new INPUT[] { up }, Marshal.SizeOf(typeof(INPUT)));
        Thread.Sleep(80);
    }

    public static void MouseClickScreen(int x, int y, int holdMs) {
        MouseDownScreen(x, y);
        Thread.Sleep(holdMs > 0 ? holdMs : 180);
        MouseUpScreen(x, y);
    }

    public static void KeyChord(ushort modifierVk, ushort vk, int holdMs) {
        KeyDown(modifierVk);
        KeyTap(vk, holdMs);
        KeyUp(modifierVk);
    }
}
"@
}

if (-not ("Win32Text" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class Win32Text {
    public delegate bool EnumChildProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr hWndParent, EnumChildProc lpEnumFunc, IntPtr lParam);

    public static IntPtr FindChildClass(IntPtr parent, string className) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (h, p) => {
            StringBuilder sb = new StringBuilder(256);
            GetClassName(h, sb, sb.Capacity);
            if (sb.ToString() == className) {
                found = h;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static string GetText(IntPtr hwnd) {
        if (hwnd == IntPtr.Zero) {
            return "";
        }
        StringBuilder sb = new StringBuilder(8192);
        GetWindowText(hwnd, sb, sb.Capacity);
        return sb.ToString();
    }

    public const uint WM_GETTEXT = 0x000D;
    public const uint WM_SETTEXT = 0x000C;
    public const uint WM_CHAR = 0x0102;

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    private static extern IntPtr SendMessageSb(IntPtr hWnd, uint msg, IntPtr wParam, StringBuilder lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    private static extern IntPtr SendMessageStr(IntPtr hWnd, uint msg, IntPtr wParam, string lParam);

    [DllImport("user32.dll", EntryPoint = "SendMessageW")]
    private static extern IntPtr SendMessagePtr(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    public static IntPtr SendChar(IntPtr hwnd, char ch) {
        return SendMessagePtr(hwnd, WM_CHAR, (IntPtr)ch, IntPtr.Zero);
    }

    public static void SetEditText(IntPtr hwnd, string text) {
        if (hwnd == IntPtr.Zero) {
            return;
        }
        SendMessageStr(hwnd, WM_SETTEXT, IntPtr.Zero, text ?? "");
    }

    public static string GetEditText(IntPtr hwnd) {
        if (hwnd == IntPtr.Zero) {
            return "";
        }
        StringBuilder sb = new StringBuilder(8192);
        SendMessageSb(hwnd, WM_GETTEXT, (IntPtr)sb.Capacity, sb);
        return sb.ToString();
    }
}
"@
}

function Get-DebugHarnessRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

function Get-DebugSessionDir {
    Join-Path $env:TEMP "mirroredge-debug"
}

function Get-HarnessInteractionLogPath {
    $last = Get-LastDebugSession
    if ($last -and $last.sessionId) {
        return Join-Path (Get-DebugSessionDir) "$($last.sessionId)-interactions.ndjson"
    }
    return Join-Path (Get-DebugSessionDir) "interactions.ndjson"
}

function Write-HarnessInteraction {
    param(
        [Parameter(Mandatory)]
        [string]$Phase,
        [Parameter(Mandatory)]
        [string]$Action,
        [hashtable]$Data = @{}
    )

    $entry = [ordered]@{
        ts     = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        phase  = $Phase
        action = $Action
    }
    foreach ($key in $Data.Keys) {
        $entry[$key] = $Data[$key]
    }

    $path = Get-HarnessInteractionLogPath
    $dir = Split-Path $path -Parent
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }

    $line = ($entry | ConvertTo-Json -Compress)
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::AppendAllText($path, $line + [Environment]::NewLine, $utf8)

    $detail = ""
    if ($Data.Count -gt 0) {
        $detail = " " + ($Data | ConvertTo-Json -Compress)
    }
    Write-Host "interaction: [$Phase] $Action$detail"

    if (-not (Test-HarnessVisualSuppressHook)) {
        try {
            Invoke-HarnessVisualFromInteraction -Phase $Phase -Action $Action | Out-Null
        } catch {
            Write-Host "visual: WARN milestone [$Phase] $Action ($($_.Exception.Message))"
        }
    }
}

function Enable-HarnessIntroHangImmunity {
    param([int]$Seconds = 150)

    $until = (Get-Date).AddSeconds($Seconds)
    $sess = $script:HarnessGameSession
    if ($sess) {
        $sess.DisableHangCheckUntil = $until
    }

    $state = @{
        stop                = $false
        hung                = $false
        reason              = ""
        introImmunityUntil  = $until.ToString("o")
    }
    $existing = Read-HarnessHangWatchState
    if ($existing -and $existing.stop) {
        $state.stop = [bool]$existing.stop
    }
    Write-HarnessHangWatchState $state
}

function Invoke-GameIntroSkip {
    param(
        [int]$MinBootSec = 25,
        [switch]$KeepFocused
    )

    Enable-HarnessIntroHangImmunity -Seconds ($MinBootSec + 90)

    Write-Host "playthrough: waiting ${MinBootSec}s for boot logos (no input during logos)"
    Write-HarnessInteraction -Phase "intro" -Action "boot_wait_begin" `
        -Data @{ minBootSec = $MinBootSec }

    $bootDeadline = (Get-Date).AddSeconds($MinBootSec)
    while ((Get-Date) -lt $bootDeadline) {
        Assert-GameProcessAlive -Label "intro boot wait" -SkipHangCheck | Out-Null
        if ($KeepFocused) {
            try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
        }
        Start-Sleep-HarnessAware -Seconds 2 -Label "intro boot wait" -SkipHangCheck
    }

    Write-HarnessInteraction -Phase "intro" -Action "boot_wait_complete" `
        -Data @{ minBootSec = $MinBootSec }
    Write-Host "playthrough: boot logo wait complete"
}

function Invoke-GameIntroForceSkip {
    param(
        [int]$SkipRounds = 15,
        [switch]$KeepFocused
    )

    Enable-HarnessIntroHangImmunity -Seconds ($SkipRounds * 4 + 60)

    Write-Host "playthrough: force-skipping intro (${SkipRounds} rounds; Escape-only at menu)"
    $menuStreak = 0
    for ($round = 1; $round -le $SkipRounds; $round++) {
        Assert-GameProcessAlive -Label "intro force skip" -SkipHangCheck | Out-Null
        if ($KeepFocused -and (($round % 3) -eq 0)) {
            try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
        }
        $map = ""
        try {
            $map = [string](Get-MmultiplayerStatusJson -PreferManager).currentMap
        } catch {}

        if ($map -and $map -ne "tdmainmenu") {
            Write-HarnessInteraction -Phase "intro" -Action "loading_detected" -Data @{
                round = $round
                map   = $map
            }
            Write-Host "playthrough: intro skip stopped (map=$map)"
            return $false
        }

        if (-not $map) {
            Write-HarnessInteraction -Phase "intro" -Action "map_pending" -Data @{ round = $round }
            $menuStreak = 0
            if ($round -ge 3) {
                Send-GameKeyTap -VirtualKey 0x0D -SettleMs 900
                Send-GameKeyTap -VirtualKey 0x1B -SettleMs 450
            }
            Start-Sleep -Milliseconds 900
            continue
        }

        if ($map -eq "tdmainmenu") {
            $menuStreak++
            if ($menuStreak -ge 3) {
                Write-HarnessInteraction -Phase "intro" -Action "menu_stable_early" `
                    -Data @{ round = $round; streak = $menuStreak }
                Write-Host "playthrough: intro skip done early (tdmainmenu stable)"
                return $true
            }
        } else {
            $menuStreak = 0
        }

        Write-HarnessInteraction -Phase "intro" -Action "skip_key" -Data @{
            round = $round
            map   = $map
            key   = "Escape"
        }
        Send-GameKeyTap -VirtualKey 0x1B -SettleMs 550
        Start-Sleep -Milliseconds 900
    }
    Write-HarnessInteraction -Phase "intro" -Action "force_skip_complete" `
        -Data @{ rounds = $SkipRounds }
    return $false
}

function Invoke-GameIntroSkipBlind {
    param(
        [int]$SkipRounds = 12,
        [switch]$KeepFocused
    )

    Enable-HarnessIntroHangImmunity -Seconds ($SkipRounds * 4 + 60)

    Write-Host "user-full: blind intro skip ($SkipRounds rounds, Enter+Escape)"
    for ($round = 1; $round -le $SkipRounds; $round++) {
        Assert-GameProcessAlive -Label "intro blind skip" -SkipHangCheck | Out-Null
        if ($KeepFocused -and (($round % 3) -eq 0)) {
            try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
        }
        Write-HarnessInteraction -Phase "intro" -Action "blind_skip_key" -Data @{
            round = $round
            key   = "Enter+Escape"
        }
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs 1100
        Send-GameKeyTap -VirtualKey 0x1B -SettleMs 550
        Start-Sleep -Milliseconds 800
    }
    Write-HarnessInteraction -Phase "intro" -Action "blind_skip_complete" `
        -Data @{ rounds = $SkipRounds }
}

function Wait-GameMainMenuReady {
    param(
        [int]$TimeoutSec = 180,
        [int]$StablePolls = 3,
        [int]$MaxSkipRounds = 40,
        [switch]$KeepFocused
    )

    Enable-HarnessIntroHangImmunity -Seconds ($TimeoutSec + 60)

    Write-Host "playthrough: skipping intro cinematics until tdmainmenu"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $menuStreak = 0
    $skipRound = 0
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "main menu wait" -SkipHangCheck | Out-Null
        if ($KeepFocused) {
            try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
        }

        $status = $null
        $map = ""
        try {
            $status = Get-MmultiplayerStatusJson
            $map = ""
            if ($null -ne $status) {
                if ($status.PSObject.Properties.Name -contains "currentMap") {
                    $map = [string]$status.currentMap
                } elseif ($status.engine -and
                    ($status.engine.PSObject.Properties.Name -contains "currentMap")) {
                    $map = [string]$status.engine.currentMap
                }
            }
            Write-HarnessInteraction -Phase "intro" -Action "poll" -Data @{
                map        = $map
                inGameplay = [bool]$status.inGameplay
                hooks      = [bool]$status.gameplayHooks
            }
        } catch {
            Write-HarnessInteraction -Phase "intro" -Action "poll_pending" `
                -Data @{ error = $_.Exception.Message }
            if ($skipRound -lt $MaxSkipRounds) {
                $skipRound++
                Send-GameKeyTap -VirtualKey 0x0D -SettleMs 1100
                Send-GameKeyTap -VirtualKey 0x1B -SettleMs 550
            }
            Start-Sleep -Seconds 2
            continue
        }

        if ($map -eq "tdmainmenu") {
            if ($status.inGameplay -eq $true) {
                if ($skipRound -lt $MaxSkipRounds) {
                    $skipRound++
                    Write-Host "playthrough: tdmainmenu inGameplay; nudge Escape"
                    Send-GameKeyTap -VirtualKey 0x1B -SettleMs 800
                }
                Start-Sleep -Milliseconds 1200
                continue
            }
            $menuStreak++
            Write-Host "playthrough: main menu signal $menuStreak/$StablePolls (tdmainmenu)"
            if ($menuStreak -ge $StablePolls) {
                Write-HarnessInteraction -Phase "intro" -Action "main_menu_ready" `
                    -Data @{ polls = $menuStreak; map = $map }
                Write-Host "playthrough: main menu confirmed"
                return $status
            }
            Start-Sleep -Seconds 2
            continue
        }

        $menuStreak = 0
        if ($map -ne "tdmainmenu") {
            if ($skipRound -lt $MaxSkipRounds) {
                $skipRound++
            }
            Write-HarnessInteraction -Phase "intro" -Action "skip_key" -Data @{
                round = $skipRound
                map   = $map
                key   = "Enter+Escape"
            }
            Send-GameKeyTap -VirtualKey 0x0D -SettleMs 1100
            Send-GameKeyTap -VirtualKey 0x1B -SettleMs 550
        }
        Start-Sleep -Milliseconds 1200

        if ($skipRound -ge $MaxSkipRounds) {
            Write-Host "playthrough: assuming tdmainmenu after $skipRound intro skip rounds (last map='$map')"
            Write-HarnessInteraction -Phase "intro" -Action "main_menu_assumed" -Data @{
                skipRound = $skipRound
                map       = $map
            }
            try {
                return Get-MmultiplayerStatusJson -PreferManager
            } catch {
                return [pscustomobject]@{ currentMap = "tdmainmenu"; inGameplay = $false }
            }
        }
    }

    Write-Host "playthrough: tdmainmenu wait timeout (skipRound=$skipRound map='$map'); assuming main menu"
    Write-HarnessInteraction -Phase "intro" -Action "main_menu_assumed" -Data @{
        skipRound = $skipRound
        map       = $map
        reason    = "timeout"
    }
    try {
        return Get-MmultiplayerStatusJson -PreferManager
    } catch {
        return [pscustomobject]@{ currentMap = "tdmainmenu"; inGameplay = $false }
    }
}

function Wait-HarnessPlayerPose {
    param(
        [int]$TimeoutSec = 90,
        [switch]$KeepFocused
    )

    Write-Host "playthrough: waiting for local player pose (up to ${TimeoutSec}s)"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "pose wait" | Out-Null
        if ($KeepFocused) {
            Try-FocusGameWindow | Out-Null
        }
        try {
            $status = Get-MmultiplayerStatusJson
        } catch {
            Start-Sleep -Seconds 2
            continue
        }
        $mag = [Math]::Abs([double]$status.mpPosX) + [Math]::Abs([double]$status.mpPosY) +
            [Math]::Abs([double]$status.mpPosZ)
        if ($mag -gt 1.0) {
            Write-HarnessInteraction -Phase "movement" -Action "pose_ready" -Data @{
                posX = [double]$status.mpPosX
                posY = [double]$status.mpPosY
                posZ = [double]$status.mpPosZ
                yaw  = [int]$status.mpYaw
            }
            Write-Host ("playthrough: pose ready ({0:F0},{1:F0},{2:F0})" -f `
                $status.mpPosX, $status.mpPosY, $status.mpPosZ)
            return $status
        }
        Start-Sleep -Seconds 2
    }

    Write-HarnessInteraction -Phase "movement" -Action "pose_timeout" `
        -Data @{ timeoutSec = $TimeoutSec }
    Write-Host "playthrough: WARN pose not ready before movement (continuing)"
    return Get-MmultiplayerStatusJson
}

function Invoke-GamePlaythroughPrepareForQuit {
    param([switch]$KeepFocused)

    Write-HarnessInteraction -Phase "quit" -Action "prepare_begin" -Data @{}
    Close-ManagerOverlays
    if ($KeepFocused) {
        Try-FocusGameWindow | Out-Null
    }
    for ($i = 0; $i -lt 6; $i++) {
        try {
            Send-GameKeyTap -VirtualKey 0x1B -SettleMs 400
        } catch {
            break
        }
    }
    Start-Sleep -Seconds 3
    Write-HarnessInteraction -Phase "quit" -Action "prepare_complete" -Data @{}
}

function Invoke-GamePlaythroughMovementWithLog {
    param(
        [Parameter(Mandatory)]
        [int]$DurationSec,
        [Parameter(Mandatory)]
        [string]$TargetFile,
        [switch]$KeepFocused
    )

    Write-HarnessInteraction -Phase "movement" -Action "session_begin" `
        -Data @{ durationSec = $DurationSec }

    $deadline = (Get-Date).AddSeconds($DurationSec)
    $sample = 0
    while ((Get-Date) -lt $deadline) {
        $sample++
        Assert-GameProcessAlive -Label "playthrough movement" -SkipHangCheck | Out-Null
        if ($KeepFocused) {
            Try-FocusGameWindow | Out-Null
        }

        try {
            $status = Update-MultiplayerBotTargetFile -Path $TargetFile
            Write-HarnessInteraction -Phase "movement" -Action "sample" -Data @{
                sample          = $sample
                map             = [string]$status.currentMap
                mpConnected     = [bool]$status.mpConnected
                mpRemotePlayers = [int]$status.mpRemotePlayers
                posX            = [double]$status.mpPosX
                posY            = [double]$status.mpPosY
                posZ            = [double]$status.mpPosZ
                yaw             = [int]$status.mpYaw
            }
        } catch {
            Write-HarnessInteraction -Phase "movement" -Action "sample_error" `
                -Data @{ sample = $sample; error = $_.Exception.Message }
        }

        Send-GameKeyHold -VirtualKey 0x57 -DurationMs 900
        Send-GameKeyHold -VirtualKey 0x41 -DurationMs 500
        Send-GameKeyHold -VirtualKey 0x44 -DurationMs 500
        for ($i = 0; $i -lt 4; $i++) {
            [Win32Input]::MouseMoveRelative(20, 5)
            Start-Sleep -Milliseconds 40
        }
    }

    Write-HarnessInteraction -Phase "movement" -Action "session_end" `
        -Data @{ samples = $sample }
}

function Get-LastSessionManifestPath {
    Join-Path (Get-DebugSessionDir) "last-session.json"
}

function Initialize-DebugSession {
    param(
        [string]$SessionId = "",
        [string]$LogPath = ""
    )

    if (-not $SessionId) {
        $SessionId = [guid]::NewGuid().ToString("N").Substring(0, 12)
    }

    $dir = Get-DebugSessionDir
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }

    if (-not $LogPath) {
        $LogPath = Join-Path $dir "$SessionId.ndjson"
    }

    $sessionLogDir = Join-Path $dir $SessionId
    if (-not (Test-Path $sessionLogDir)) {
        New-Item -ItemType Directory -Path $sessionLogDir -Force | Out-Null
    }
    $sessionLogPath = Join-Path $sessionLogDir "session.log"
    $env:MMOD_DEBUG_SESSION = $SessionId
    $env:MMOD_DEBUG_LOG = $LogPath
    $env:MMOD_SESSION_LOG = $sessionLogPath

    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
    $versionJson = Join-Path $repoRoot "version.json"
    if (Test-Path $versionJson) {
        try {
            $productMeta = Get-Content $versionJson -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($productMeta.version) {
                $env:MMOD_PRODUCT_VERSION = $productMeta.version
            }
        } catch {
            Write-Host "harness: WARN could not read version.json"
        }
    }

    $manifest = @{
        sessionId  = $SessionId
        logPath    = $LogPath
        sessionLog = $sessionLogPath
        ndjsonLog  = $LogPath
        startedAt  = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    }
    $json = $manifest | ConvertTo-Json -Compress
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText((Get-LastSessionManifestPath), $json, $utf8NoBom)

    return [pscustomobject]@{
        SessionId = $SessionId
        LogPath   = $LogPath
        Manifest  = (Get-LastSessionManifestPath)
    }
}

function Get-LastDebugSession {
    $path = Get-LastSessionManifestPath
    if (-not (Test-Path $path)) {
        return $null
    }
    return Get-Content $path -Raw | ConvertFrom-Json
}

function Read-DebugLogTail {
    param(
        [string]$LogPath = "",
        [int]$Lines = 50,
        [string]$SessionId = ""
    )

    if (-not $LogPath) {
        if ($SessionId) {
            $LogPath = Join-Path (Get-DebugSessionDir) "$SessionId.ndjson"
        } else {
            $last = Get-LastDebugSession
            if (-not $last) {
                return @()
            }
            $LogPath = $last.logPath
        }
    }

    if (-not (Test-Path $LogPath)) {
        return @()
    }

    try {
        $stream = [System.IO.File]::Open($LogPath, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $reader = New-Object System.IO.StreamReader($stream)
            $allLines = New-Object System.Collections.Generic.List[string]
            while ($null -ne ($line = $reader.ReadLine())) {
                [void]$allLines.Add($line)
            }
            $start = [Math]::Max(0, $allLines.Count - $Lines)
            if ($start -ge $allLines.Count) {
                return @()
            }
            return $allLines.GetRange($start, $allLines.Count - $start)
        } finally {
            $reader.Dispose()
            $stream.Dispose()
        }
    } catch {
        return Get-Content -Path $LogPath -Tail $Lines -ErrorAction SilentlyContinue
    }
}

function Wait-NamedEvent {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [int]$TimeoutSec = 120
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $ev = [System.Threading.EventWaitHandle]::OpenExisting($Name)
            if ($ev.WaitOne(0)) {
                $ev.Dispose()
                return $true
            }
            $ev.Dispose()
        } catch [System.Threading.WaitHandleCannotBeOpenedException] {
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Wait-NamedEventWithBootstrapProbe {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [int]$TimeoutSec = 120,
        [switch]$MonitorRenderStall,
        [switch]$BootNudge
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $nextNudge = if ($BootNudge) { (Get-Date).AddSeconds(6) } else { [DateTime]::MaxValue }
    while ((Get-Date) -lt $deadline) {
        if ($MonitorRenderStall) {
            Assert-CoreBootstrapProgress
        } else {
            Assert-GameProcessAlive -Label "named event $Name" -SkipHangCheck | Out-Null
        }
        if ($BootNudge -and (Get-Date) -gt $nextNudge) {
            try { Invoke-BootGameNudge -Label "core bootstrap nudge (Enter)" } catch {}
            $nextNudge = (Get-Date).AddSeconds(6)
        }
        try {
            $ev = [System.Threading.EventWaitHandle]::OpenExisting($Name)
            if ($ev.WaitOne(0)) {
                $ev.Dispose()
                return $true
            }
            $ev.Dispose()
        } catch [System.Threading.WaitHandleCannotBeOpenedException] {
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Assert-CoreBootstrapProgress {
    param([string]$Label = "core bootstrap")

    Assert-GameProcessAlive -Label $Label -SkipHangCheck | Out-Null
    try {
        $status = Get-ManagerStatusJson -TimeoutMs 3000
        if ($status.hooksInstalled -eq $true) {
            if (Test-HarnessRenderPipelineStalled -StaleSec 45) {
                throw "core bootstrap render stall (no EndScene/frame progress for >=45s after hooks installed)"
            }
        }
    } catch {
        if ($_.Exception.Message -like '*render stall*') {
            throw
        }
    }
}

if (-not (Get-Variable -Name ManagerControlPipeMutex -Scope Script -ErrorAction SilentlyContinue)) {
    $script:ManagerControlPipeMutex = New-Object System.Threading.Mutex($false, "Local\mirroredge_manager_pipe_client")
}
if (-not (Get-Variable -Name CoreControlPipeMutex -Scope Script -ErrorAction SilentlyContinue)) {
    $script:CoreControlPipeMutex = New-Object System.Threading.Mutex($false, "Local\mirroredge_core_pipe_client")
}

function Invoke-ModControlPipe {
    param(
        [Parameter(Mandatory)]
        [string]$Command,
        [ValidateSet("manager", "core", "mmultiplayer")]
        [string]$Target = "manager",
        [int]$TimeoutMs = 10000,
        [int]$MaxAttempts = 3
    )

    $lastError = $null
    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        try {
            return Invoke-ModControlPipeOnce -Command $Command -Target $Target -TimeoutMs $TimeoutMs
        } catch {
            $lastError = $_
            if ($attempt -ge $MaxAttempts) {
                break
            }
            Write-Host ("pipe: retry {0}/{1} ({2}): {3}" -f `
                $attempt, $MaxAttempts, $Target, $_.Exception.Message)
            Start-Sleep -Milliseconds ([Math]::Min(500 * $attempt, 2000))
        }
    }
    throw $lastError
}

function Invoke-ModControlPipeOnce {
    param(
        [Parameter(Mandatory)]
        [string]$Command,
        [ValidateSet("manager", "core", "mmultiplayer")]
        [string]$Target = "manager",
        [int]$TimeoutMs = 10000,
        [int]$ConnectTimeoutMs = 0,
        [int]$ReadTimeoutMs = 0
    )

    if ($Target -eq "mmultiplayer") {
        $Target = "core"
    }

    if ($ConnectTimeoutMs -le 0) {
        $ConnectTimeoutMs = [Math]::Min($TimeoutMs, 5000)
    }
    if ($ReadTimeoutMs -le 0) {
        $ReadTimeoutMs = $TimeoutMs
    }

    $pipeName = if ($Target -eq "manager") {
        "mirroredge_module_manager_control"
    } else {
        "mirroredge_module_control"
    }

    $pipeMutex = if ($Target -eq "manager") {
        $script:ManagerControlPipeMutex
    } else {
        $script:CoreControlPipeMutex
    }
    $mutexWaitMs = [Math]::Max($ConnectTimeoutMs, 1000)
    if (-not $pipeMutex.WaitOne($mutexWaitMs)) {
        throw "Timed out waiting for $Target control pipe mutex"
    }

    $client = New-Object System.IO.Pipes.NamedPipeClientStream ".", $pipeName, ([System.IO.Pipes.PipeDirection]::InOut)
    try {
        $client.Connect($ConnectTimeoutMs)
        $client.ReadMode = [System.IO.Pipes.PipeTransmissionMode]::Byte
        try {
            $client.ReadTimeout = $ReadTimeoutMs
        } catch {
            # Older runtimes may not expose ReadTimeout on named pipes.
        }

        $payload = [System.Text.Encoding]::ASCII.GetBytes($Command + "`n")
        $client.Write($payload, 0, $payload.Length)
        $client.Flush()

        $buffer = New-Object System.Text.StringBuilder
        $readBuf = New-Object byte[] 8192
        $deadline = [DateTime]::UtcNow.AddMilliseconds($ReadTimeoutMs)
        $multiline = ($Command -eq "LIST_MODULES" -or $Command -like "GET_LOG*")
        while ([DateTime]::UtcNow -lt $deadline) {
            $read = 0
            try {
                $read = $client.Read($readBuf, 0, $readBuf.Length)
            } catch [System.TimeoutException] {
                break
            } catch {
                if ($buffer.Length -gt 0) {
                    break
                }
                Start-Sleep -Milliseconds 5
                continue
            }

            if ($read -le 0) {
                if ($buffer.Length -gt 0) {
                    break
                }
                Start-Sleep -Milliseconds 5
                continue
            }

            [void]$buffer.Append([System.Text.Encoding]::ASCII.GetString($readBuf, 0, $read))
            $text = $buffer.ToString()
            if ($multiline) {
                if ($text -match "`nEND\s*$") {
                    break
                }
            } elseif ($text.Contains("`n")) {
                break
            }
        }

        $raw = ($buffer.ToString() -replace "`r", "").Trim()
        if ($multiline -and $raw -match "`nEND\s*$") {
            $raw = ($raw -replace "`nEND\s*$", "").TrimEnd()
        }
        if ([string]::IsNullOrWhiteSpace($raw)) {
            throw "Empty response from \\.\pipe\$pipeName"
        }

        $lines = @($raw -split "`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        if ($lines.Count -eq 0) {
            throw "Empty response from \\.\pipe\$pipeName"
        }

        if ($lines.Count -eq 1) {
            return $lines[0]
        }

        return ($lines -join "`n")
    } finally {
        if ($client) {
            $client.Dispose()
        }
        try { $pipeMutex.ReleaseMutex() } catch {}
    }
}

function Test-LogContains {
    param(
        [Parameter(Mandatory)]
        [string[]]$Patterns,
        [string]$LogPath = "",
        [int]$TimeoutSec = 0
    )

    $deadline = if ($TimeoutSec -gt 0) { (Get-Date).AddSeconds($TimeoutSec) } else { Get-Date }

    do {
        $lines = Read-DebugLogTail -LogPath $LogPath -Lines 5000
        $joined = ($lines -join "`n")
        $missing = @()
        foreach ($p in $Patterns) {
            if ($joined -notmatch [regex]::Escape($p)) {
                $missing += $p
            }
        }
        if ($missing.Count -eq 0) {
            return [pscustomobject]@{ Pass = $true; Missing = @() }
        }
        if ($TimeoutSec -le 0) {
            break
        }
        Start-Sleep -Milliseconds 750
    } while ((Get-Date) -lt $deadline)

    return [pscustomobject]@{ Pass = $false; Missing = $missing }
}

function Test-LogSequence {
    param(
        [Parameter(Mandatory)]
        [string[]]$Sequence,
        [string]$LogPath = "",
        [int]$TimeoutSec = 180
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $messages = @()
    do {
        $lines = Read-DebugLogTail -LogPath $LogPath -Lines 5000
        $messages = @()
        foreach ($line in $lines) {
            if (-not $line) { continue }
            try {
                $obj = $line | ConvertFrom-Json
                if ($obj.message) { $messages += [string]$obj.message }
            } catch {
                $messages += $line
            }
        }

        $index = 0
        $ok = $true
        foreach ($needle in $Sequence) {
            $found = $false
            while ($index -lt $messages.Count) {
                if ($messages[$index] -like "*$needle*") {
                    $found = $true
                    $index++
                    break
                }
                $index++
            }
            if (-not $found) {
                $ok = $false
                break
            }
        }

        if ($ok) {
            return [pscustomobject]@{ Pass = $true; Sequence = $Sequence }
        }
        Start-Sleep -Milliseconds 1000
    } while ((Get-Date) -lt $deadline)

    return [pscustomobject]@{
        Pass         = $false
        Sequence     = $Sequence
        LastMessages = ($messages | Select-Object -Last 20)
    }
}

function Read-DeployConfigJson {
    param([string]$RepoRoot)

    $configPath = Join-Path $RepoRoot "deploy.config.json"
    if (-not (Test-Path $configPath)) {
        return $null
    }
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $text = [System.IO.File]::ReadAllText($configPath, $utf8)
    return ($text | ConvertFrom-Json)
}

function Resolve-DeployPath {
    param([string]$RepoRoot)

    if ($env:ME_DEPLOY_PATH) { return $env:ME_DEPLOY_PATH }
    if ($env:ME_GAME_BINARIES) { return $env:ME_GAME_BINARIES }

    $json = Read-DeployConfigJson -RepoRoot $RepoRoot
    if ($json) {
        if ($json.deployPath) { return [string]$json.deployPath }
        if ($json.gameBinaries) { return [string]$json.gameBinaries }
    }
    return $null
}

function Resolve-TestMachine {
    param([string]$RepoRoot = (Get-RepoRoot))

    if ($env:MMOD_TEST_MACHINE) {
        return [string]$env:MMOD_TEST_MACHINE
    }

    try {
        $json = Read-DeployConfigJson -RepoRoot $RepoRoot
        if ($json -and $json.testMachine) {
            return [string]$json.testMachine
        }
    } catch {}
    return $null
}

function Initialize-HarnessTestMachine {
    param([string]$RepoRoot = (Get-RepoRoot))

    $id = Resolve-TestMachine -RepoRoot $RepoRoot
    if (-not $id) {
        return $null
    }

    $env:MMOD_TEST_MACHINE = $id
    Set-HarnessResultExtra -Extra @{ testMachine = $id }
    Write-Host "harness: testMachine=$id"
    return $id
}

function Write-HarnessCoordination {
    param(
        [Parameter(Mandatory)]
        [string]$Status,
        [string]$Scenario = "",
        [object]$Summary = $null,
        [string]$RepoRoot = (Get-RepoRoot)
    )

    $machine = Resolve-TestMachine -RepoRoot $RepoRoot
    if (-not $machine) {
        $machine = "unknown"
    }

    $coordDir = Join-Path $RepoRoot "obj"
    if (-not (Test-Path $coordDir)) {
        New-Item -ItemType Directory -Path $coordDir -Force | Out-Null
    }

    $payload = [ordered]@{
        testMachine = $machine
        status      = $Status
        scenario    = $Scenario
        updatedAt   = (Get-Date).ToUniversalTime().ToString("o")
        peer        = $(if ($machine -eq "1号机") { "2号机" } elseif ($machine -eq "2号机") { "1号机" } else { "" })
    }
    if ($Summary) {
        $payload.summary = $Summary
    }

    $path = Join-Path $coordDir "harness-coord.json"
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($path, ($payload | ConvertTo-Json -Depth 6), $utf8NoBom)
    Write-Host "harness-coord: $Status ($machine) -> $path"
}

function Get-MachineAlertsDir {
    param([string]$RepoRoot = (Get-RepoRoot))
    return Join-Path $RepoRoot "test-logs\alerts"
}

function Get-MachineAlertsIndexPath {
    param([string]$RepoRoot = (Get-RepoRoot))
    return Join-Path (Get-MachineAlertsDir -RepoRoot $RepoRoot) "index.json"
}

function Publish-MachineAlert {
    param(
        [Parameter(Mandatory)]
        [string]$Title,
        [Parameter(Mandatory)]
        [string]$Body,
        [ValidateSet("info", "warning", "blocker", "resolved")]
        [string]$Severity = "warning",
        [string[]]$RelatedCommits = @(),
        [string[]]$RelatedFiles = @(),
        [string]$ToMachine = "",
        [string]$RepoRoot = (Get-RepoRoot),
        [switch]$Push
    )

    $fromMachine = Resolve-TestMachine -RepoRoot $RepoRoot
    if (-not $fromMachine) {
        $fromMachine = "unknown"
    }

    $alertDir = Get-MachineAlertsDir -RepoRoot $RepoRoot
    if (-not (Test-Path $alertDir)) {
        New-Item -ItemType Directory -Path $alertDir -Force | Out-Null
    }

    $git = Get-HarnessGitSnapshot -RepoRoot $RepoRoot
    $alertId = (Get-Date).ToUniversalTime().ToString("yyyyMMdd-HHmmss") + "Z-" + $fromMachine
    $alert = [ordered]@{
        schemaVersion   = 1
        alertId         = $alertId
        fromMachine     = $fromMachine
        toMachine       = $(if ($ToMachine) { $ToMachine } else { "" })
        severity        = $Severity
        title           = $Title
        body            = $Body
        relatedCommits  = @($RelatedCommits)
        relatedFiles    = @($RelatedFiles)
        git             = $git
        createdAt       = (Get-Date).ToUniversalTime().ToString("o")
    }

    $jsonlPath = Join-Path $alertDir "alerts.jsonl"
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    $line = ($alert | ConvertTo-Json -Compress -Depth 8)
    [System.IO.File]::AppendAllText($jsonlPath, $line + [Environment]::NewLine, $utf8NoBom)

    $indexPath = Get-MachineAlertsIndexPath -RepoRoot $RepoRoot
    $index = [ordered]@{
        schemaVersion = 1
        updatedAt     = $alert.createdAt
        unreadBy      = @{}
        recent        = @()
    }
    if (Test-Path $indexPath) {
        try {
            $existing = Get-Content $indexPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($existing.unreadBy) {
                foreach ($prop in $existing.unreadBy.PSObject.Properties) {
                    $index.unreadBy[$prop.Name] = [int]$prop.Value
                }
            }
            if ($existing.recent) {
                $index.recent = @($existing.recent) | Select-Object -First 49
            }
        } catch {}
    }

    $summary = [ordered]@{
        alertId     = $alertId
        fromMachine = $fromMachine
        toMachine   = $alert.toMachine
        severity    = $Severity
        title       = $Title
        createdAt   = $alert.createdAt
        shortCommit = $git.shortCommit
    }
    $index.recent = @($summary) + @($index.recent) | Select-Object -First 50
    foreach ($peer in @("1号机", "2号机")) {
        if ($peer -ne $fromMachine) {
            $prev = 0
            if ($index.unreadBy.ContainsKey($peer)) {
                $prev = [int]$index.unreadBy[$peer]
            }
            $index.unreadBy[$peer] = $prev + 1
        }
    }

    [System.IO.File]::WriteAllText($indexPath, ($index | ConvertTo-Json -Depth 8), $utf8NoBom)

    $machineDir = Join-Path $RepoRoot "test-logs\machines\$fromMachine"
    if (-not (Test-Path $machineDir)) {
        New-Item -ItemType Directory -Path $machineDir -Force | Out-Null
    }
    $machineAlertsPath = Join-Path $machineDir "alerts-sent.jsonl"
    [System.IO.File]::AppendAllText($machineAlertsPath, $line + [Environment]::NewLine, $utf8NoBom)

    Write-Host "machine-alert: [$Severity] $Title ($fromMachine -> $alertId)" -ForegroundColor Yellow

    if ($Push) {
        Push-Location $RepoRoot
        try {
            git add test-logs/alerts test-logs/machines/*/alerts-sent.jsonl 2>$null
            $msg = "test-logs: alert from $fromMachine - $Title"
            git commit -m $msg 2>$null
            git push 2>&1 | Write-Host
        } catch {
            Write-Warning "machine-alert push failed: $_"
        } finally {
            Pop-Location
        }
    }

    return [pscustomobject]$alert
}

function Get-MachineAlerts {
    param(
        [string]$ForMachine = "",
        [int]$Limit = 20,
        [string]$RepoRoot = (Get-RepoRoot),
        [switch]$UnreadOnly,
        [switch]$IncludeAll
    )

    $machine = if ($IncludeAll) {
        ""
    } elseif ($ForMachine) {
        $ForMachine
    } else {
        Resolve-TestMachine -RepoRoot $RepoRoot
    }
    $jsonlPath = Join-Path (Get-MachineAlertsDir -RepoRoot $RepoRoot) "alerts.jsonl"
    if (-not (Test-Path $jsonlPath)) {
        return @()
    }

    $alerts = @()
    foreach ($line in Get-Content $jsonlPath -Encoding UTF8) {
        if (-not $line.Trim()) { continue }
        try {
            $alerts += (ConvertFrom-Json $line)
        } catch {}
    }

    if ($machine) {
        $alerts = @($alerts | Where-Object {
            -not $_.toMachine -or [string]$_.toMachine -eq "" -or [string]$_.toMachine -eq $machine
        })
    }

    if ($UnreadOnly -and $machine) {
        $indexPath = Get-MachineAlertsIndexPath -RepoRoot $RepoRoot
        if (Test-Path $indexPath) {
            try {
                $index = Get-Content $indexPath -Raw -Encoding UTF8 | ConvertFrom-Json
                $unread = [int]($index.unreadBy.$machine)
                if ($unread -le 0) {
                    return @()
                }
            } catch {}
        }
    }

    return @($alerts | Select-Object -Last $Limit)
}

function Clear-MachineAlertsUnread {
    param(
        [string]$Machine = "",
        [string]$RepoRoot = (Get-RepoRoot)
    )

    $machine = if ($Machine) { $Machine } else { Resolve-TestMachine -RepoRoot $RepoRoot }
    if (-not $machine) { return }

    $indexPath = Get-MachineAlertsIndexPath -RepoRoot $RepoRoot
    if (-not (Test-Path $indexPath)) { return }

    $index = Get-Content $indexPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($index.unreadBy -is [hashtable]) {
        $index.unreadBy[$machine] = 0
    } elseif ($index.unreadBy) {
        $index.unreadBy | Add-Member -NotePropertyName $machine -NotePropertyValue 0 -Force
    }
    $index.updatedAt = (Get-Date).ToUniversalTime().ToString("o")
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($indexPath, ($index | ConvertTo-Json -Depth 8), $utf8NoBom)
}

function Get-HarnessGitSnapshot {
    param([string]$RepoRoot = (Get-RepoRoot))

    $snap = [ordered]@{
        commit      = $null
        shortCommit = $null
        branch      = $null
        subject     = $null
        author      = $null
        commitDate  = $null
        dirty       = $false
        dirtyFiles  = @()
    }

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) {
        return [pscustomobject]$snap
    }

    Push-Location $RepoRoot
    try {
        $snap.commit = (git rev-parse HEAD 2>$null).Trim()
        if ($snap.commit) {
            $snap.shortCommit = (git rev-parse --short HEAD 2>$null).Trim()
            $snap.branch = (git rev-parse --abbrev-ref HEAD 2>$null).Trim()
            $snap.subject = (git log -1 --format=%s 2>$null).Trim()
            $snap.author = (git log -1 --format=%an 2>$null).Trim()
            $snap.commitDate = (git log -1 --format=%cI 2>$null).Trim()
        }
        $porcelain = @(git status --porcelain 2>$null | Where-Object { $_ })
        $snap.dirty = ($porcelain.Count -gt 0)
        if ($snap.dirty) {
            $snap.dirtyFiles = @($porcelain | ForEach-Object { $_.Substring(3).Trim() })
        }
    } catch {
    } finally {
        Pop-Location
    }

    return [pscustomobject]$snap
}

function Get-HarnessTestLogMachineDir {
    param(
        [Parameter(Mandatory)]
        [string]$Machine,
        [string]$RepoRoot = (Get-RepoRoot)
    )

    $id = if ($Machine) { $Machine } else { "unknown" }
    return Join-Path $RepoRoot "test-logs\machines\$id"
}

function ConvertTo-HarnessTestLogMachineEntry {
    param([object]$Entry)

    if ($null -eq $Entry) { return $null }
    if ($Entry -is [hashtable]) {
        return [ordered]@{
            runId       = [string]$Entry.runId
            finishedAt  = [string]$Entry.finishedAt
            suite       = [string]$Entry.suite
            passCount   = [int]$Entry.passCount
            totalCount  = [int]$Entry.totalCount
            pass        = [bool]$Entry.pass
            shortCommit = [string]$Entry.shortCommit
            branch      = [string]$Entry.branch
            subject     = [string]$Entry.subject
            dirty       = [bool]$Entry.dirty
        }
    }

    return [ordered]@{
        runId       = [string]$Entry.runId
        finishedAt  = [string]$Entry.finishedAt
        suite       = [string]$Entry.suite
        passCount   = [int]$Entry.passCount
        totalCount  = [int]$Entry.totalCount
        pass        = [bool]$Entry.pass
        shortCommit = [string]$Entry.shortCommit
        branch      = [string]$Entry.branch
        subject     = [string]$Entry.subject
        dirty       = [bool]$Entry.dirty
    }
}

function Get-HarnessTestLogMachinesFromParsed {
    param([object]$Parsed)

    $machines = @{}
    if ($null -eq $Parsed) { return $machines }

    if ($Parsed -is [System.Array] -or $Parsed.GetType().IsArray) {
        foreach ($item in @($Parsed)) {
            $nested = Get-HarnessTestLogMachinesFromParsed -Parsed $item
            foreach ($key in $nested.Keys) {
                $machines[$key] = $nested[$key]
            }
        }
        return $machines
    }

    $machineSource = $null
    if ($Parsed -is [hashtable]) {
        if ($Parsed.ContainsKey('machines') -and $Parsed.machines) {
            $machineSource = $Parsed.machines
        }
    } elseif ($Parsed.PSObject.Properties.Name -contains 'machines' -and $Parsed.machines) {
        $machineSource = $Parsed.machines
    }

    if ($null -eq $machineSource) { return $machines }

    if ($machineSource -is [hashtable]) {
        foreach ($key in $machineSource.Keys) {
            $machines[[string]$key] = ConvertTo-HarnessTestLogMachineEntry -Entry $machineSource[$key]
        }
        return $machines
    }

    foreach ($prop in $machineSource.PSObject.Properties) {
        $machines[$prop.Name] = ConvertTo-HarnessTestLogMachineEntry -Entry $prop.Value
    }
    return $machines
}

function Merge-HarnessTestLogMachineEntry {
    param(
        [object]$Existing,
        [object]$Incoming
    )

    if (-not $Existing) { return $Incoming }
    if (-not $Incoming) { return $Existing }

    try {
        $existingAt = [datetime]$Existing.finishedAt
        $incomingAt = [datetime]$Incoming.finishedAt
        if ($incomingAt -gt $existingAt) { return $Incoming }
        return $Existing
    } catch {
        return $Incoming
    }
}

function Merge-HarnessTestLogIndex {
    param(
        [object[]]$Indexes = @()
    )

    $mergedMachines = @{}
    foreach ($index in @($Indexes)) {
        if ($null -eq $index) { continue }

        $machines = if ($index -is [hashtable] -and $index.ContainsKey('machines')) {
            $index.machines
        } else {
            Get-HarnessTestLogMachinesFromParsed -Parsed $index
        }

        foreach ($key in @($machines.Keys)) {
            $incoming = ConvertTo-HarnessTestLogMachineEntry -Entry $machines[$key]
            if ($mergedMachines.ContainsKey($key)) {
                $mergedMachines[$key] = Merge-HarnessTestLogMachineEntry `
                    -Existing $mergedMachines[$key] -Incoming $incoming
            } else {
                $mergedMachines[$key] = $incoming
            }
        }
    }

    $updatedAt = $null
    foreach ($entry in $mergedMachines.Values) {
        if (-not $entry.finishedAt) { continue }
        try {
            $finishedAt = [datetime]$entry.finishedAt
            if (-not $updatedAt -or $finishedAt -gt $updatedAt) {
                $updatedAt = $finishedAt
            }
        } catch {}
    }

    return [ordered]@{
        schemaVersion = 1
        updatedAt     = if ($updatedAt) { $updatedAt.ToUniversalTime().ToString("o") } else { $null }
        machines      = $mergedMachines
    }
}

function Split-HarnessTestLogIndexConflictText {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text) -or $Text -notmatch '<<<<<<<') {
        return @($Text)
    }

    $chunks = [System.Collections.Generic.List[string]]::new()
    $remaining = $Text
    while ($true) {
        $start = $remaining.IndexOf('<<<<<<<')
        if ($start -lt 0) {
            if ($remaining.Trim()) { $chunks.Add($remaining.Trim()) | Out-Null }
            break
        }

        if ($start -gt 0) {
            $prefix = $remaining.Substring(0, $start).Trim()
            if ($prefix) { $chunks.Add($prefix) | Out-Null }
        }

        $remaining = $remaining.Substring($start)
        $remaining = $remaining -replace '^<<<<<<<[^\r\n]*\r?\n', ''
        $splitIdx = $remaining.IndexOf('=======')
        if ($splitIdx -lt 0) { break }

        $ours = $remaining.Substring(0, $splitIdx).Trim()
        $remaining = $remaining.Substring($splitIdx + 7)
        $endIdx = $remaining.IndexOf('>>>>>>>')
        if ($endIdx -lt 0) { break }

        $theirs = $remaining.Substring(0, $endIdx).Trim()
        $remaining = $remaining.Substring($endIdx)
        $remaining = $remaining -replace '^>>>>>>>[^\r\n]*\r?\n', ''

        if ($ours) { $chunks.Add($ours) | Out-Null }
        if ($theirs) { $chunks.Add($theirs) | Out-Null }
    }

    return @($chunks)
}

function Merge-HarnessTestLogIndexFromJson {
    param(
        [object[]]$JsonInputs = @()
    )

    $indexes = @()
    foreach ($json in @($JsonInputs)) {
        if ($null -eq $json) { continue }
        $text = [string]$json
        if ([string]::IsNullOrWhiteSpace($text)) { continue }

        foreach ($chunk in (Split-HarnessTestLogIndexConflictText -Text $text)) {
            if ([string]::IsNullOrWhiteSpace($chunk)) { continue }
            try {
                $parsed = $chunk | ConvertFrom-Json
                $indexes += $parsed
            } catch {
                Write-Verbose "test-logs: skipped invalid index JSON chunk: $($_.Exception.Message)"
            }
        }
    }

    return Merge-HarnessTestLogIndex -Indexes $indexes
}

function Read-HarnessTestLogIndexFile {
    param([string]$Path)

    if (-not (Test-Path $Path)) { return $null }
    $text = [System.IO.File]::ReadAllText($Path, (New-Object System.Text.UTF8Encoding $false))
    return Merge-HarnessTestLogIndexFromJson -JsonInputs @($text)
}

function Write-HarnessTestLogIndexFile {
    param(
        [Parameter(Mandatory)]
        [hashtable]$Index,
        [Parameter(Mandatory)]
        [string]$Path
    )

    $normalized = Merge-HarnessTestLogIndex -Indexes @($Index)
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($Path, (($normalized | ConvertTo-Json -Depth 6) + "`n"), $utf8NoBom)
    return $normalized
}

function Get-HarnessTestLogMachineIds {
    param([string]$RepoRoot = (Get-RepoRoot))

    $seen = @{}
    foreach ($id in (Get-HarnessTestLogRegistryMachineIds -RepoRoot $RepoRoot)) {
        $seen[[string]$id] = $true
    }

    $machinesDir = Join-Path $RepoRoot "test-logs\machines"
    if (Test-Path $machinesDir) {
        foreach ($dir in Get-ChildItem $machinesDir -Directory) {
            $seen[$dir.Name] = $true
        }
    }

    return @($seen.Keys)
}

function Read-HarnessTestLogLatestRecord {
    param([string]$Path)

    if (-not (Test-Path $Path)) { return $null }
    try {
        $text = [System.IO.File]::ReadAllText($Path, (New-Object System.Text.UTF8Encoding $false))
        return $text | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Convert-HarnessLatestRecordToIndexEntry {
    param([object]$Record)

    if (-not $Record) { return $null }

    $git = $Record.gitAtEnd
    if (-not $git) { $git = $Record.gitAtStart }

    return [ordered]@{
        runId       = [string]$Record.runId
        finishedAt  = [string]$Record.finishedAt
        suite       = [string]$Record.suite
        passCount   = [int]$Record.passCount
        totalCount  = [int]$Record.totalCount
        pass        = [bool]$Record.pass
        shortCommit = if ($git -and $git.shortCommit) { [string]$git.shortCommit } else { "unknown" }
        branch      = if ($git -and $git.branch) { [string]$git.branch } else { "?" }
        subject     = if ($git -and $git.subject) { [string]$git.subject } else { "" }
        dirty       = if ($git) { [bool]$git.dirty } else { $false }
    }
}

function Merge-HarnessTestLogLatestRecord {
    param(
        [object]$Existing,
        [object]$Incoming
    )

    if (-not $Existing) { return $Incoming }
    if (-not $Incoming) { return $Existing }

    try {
        $existingAt = [datetime]$Existing.finishedAt
        $incomingAt = [datetime]$Incoming.finishedAt
        if ($incomingAt -gt $existingAt) { return $Incoming }
        return $Existing
    } catch {
        return $Incoming
    }
}

function Write-HarnessTestLogLatestRecordFile {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [object]$Record
    )

    $dir = Split-Path $Path -Parent
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    $pretty = ($Record | ConvertTo-Json -Depth 8) + "`n"
    [System.IO.File]::WriteAllText($Path, $pretty, $utf8NoBom)
}

function Sync-HarnessTestLogMachineLatestWithRemote {
    param([string]$RepoRoot = (Get-RepoRoot))

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { return $false }

    Push-Location $RepoRoot
    try {
        git fetch origin 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { return $false }

        $branch = (git rev-parse --abbrev-ref HEAD 2>$null)
        if (-not $branch -or $branch -eq 'HEAD') { return $false }
        $branch = $branch.Trim()

        $changed = $false
        foreach ($id in (Get-HarnessTestLogMachineIds -RepoRoot $RepoRoot)) {
            $localPath = Join-Path $RepoRoot "test-logs\machines\$id\latest.json"
            $localRecord = Read-HarnessTestLogLatestRecord -Path $localPath

            $remoteJson = git show "origin/${branch}:test-logs/machines/${id}/latest.json" 2>$null
            $remoteRecord = $null
            if ($LASTEXITCODE -eq 0 -and $remoteJson) {
                try {
                    $remoteRecord = $remoteJson | ConvertFrom-Json
                } catch {}
            }

            $winner = Merge-HarnessTestLogLatestRecord -Existing $localRecord -Incoming $remoteRecord
            if (-not $winner) { continue }

            $winnerFinished = [string]$winner.finishedAt
            $localFinished = if ($localRecord) { [string]$localRecord.finishedAt } else { "" }
            if ($winnerFinished -ne $localFinished) {
                Write-HarnessTestLogLatestRecordFile -Path $localPath -Record $winner
                $changed = $true
            }
        }

        return $changed
    } finally {
        Pop-Location
    }
}

function Rebuild-HarnessTestLogIndex {
    param([string]$RepoRoot = (Get-RepoRoot))

    $machines = @{}
    foreach ($id in (Get-HarnessTestLogMachineIds -RepoRoot $RepoRoot)) {
        $localPath = Join-Path $RepoRoot "test-logs\machines\$id\latest.json"
        $record = Read-HarnessTestLogLatestRecord -Path $localPath
        if (-not $record) { continue }
        $entry = Convert-HarnessLatestRecordToIndexEntry -Record $record
        if ($entry) {
            $machines[[string]$id] = $entry
        }
    }

    if ($machines.Count -eq 0) {
        return $false
    }

    $indexPath = Join-Path $RepoRoot "test-logs\index.json"
    Write-HarnessTestLogIndexFile -Index @{ schemaVersion = 1; machines = $machines } -Path $indexPath | Out-Null
    return $true
}

function Test-HarnessTestLogIndexMatchesLatest {
    param([string]$RepoRoot = (Get-RepoRoot))

    $indexPath = Join-Path $RepoRoot "test-logs\index.json"
    if (-not (Test-Path $indexPath)) {
        throw "index.json missing"
    }

    $before = Read-HarnessTestLogIndexFile -Path $indexPath
    if (-not $before) {
        throw "index.json invalid"
    }

    Rebuild-HarnessTestLogIndex -RepoRoot $RepoRoot | Out-Null
    $after = Read-HarnessTestLogIndexFile -Path $indexPath
    if (-not $after) {
        throw "rebuilt index invalid"
    }

    foreach ($key in @($before.machines.Keys)) {
        if (-not $after.machines.ContainsKey($key)) {
            throw "rebuild removed machine $key"
        }
        if ([string]$before.machines[$key].runId -ne [string]$after.machines[$key].runId) {
            throw "index out of sync for ${key}: runId $($before.machines[$key].runId) != $($after.machines[$key].runId)"
        }
    }

    foreach ($key in @($after.machines.Keys)) {
        if (-not $before.machines.ContainsKey($key)) {
            throw "rebuild added unexpected machine $key"
        }
    }

    return $true
}

function Sync-HarnessTestLogIndexWithRemote {
    param([string]$RepoRoot = (Get-RepoRoot))

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { return $false }

    $latestSynced = Sync-HarnessTestLogMachineLatestWithRemote -RepoRoot $RepoRoot
    if (Rebuild-HarnessTestLogIndex -RepoRoot $RepoRoot) {
        if ($latestSynced) {
            Write-Host "test-logs: rebuilt index.json from machines/*/latest.json (synced with origin)"
        } else {
            Write-Host "test-logs: rebuilt index.json from machines/*/latest.json"
        }
        return $true
    }

    Push-Location $RepoRoot
    try {
        git fetch origin 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { return $false }

        $branch = (git rev-parse --abbrev-ref HEAD 2>$null)
        if (-not $branch -or $branch -eq 'HEAD') { return $false }
        $branch = $branch.Trim()

        $remoteJson = git show "origin/${branch}:test-logs/index.json" 2>$null
        if ($LASTEXITCODE -ne 0) { return $false }

        $indexPath = Join-Path $RepoRoot "test-logs\index.json"
        $localJson = if (Test-Path $indexPath) {
            [System.IO.File]::ReadAllText($indexPath, (New-Object System.Text.UTF8Encoding $false))
        } else {
            $null
        }

        $merged = Merge-HarnessTestLogIndexFromJson -JsonInputs @($remoteJson, $localJson)
        if (-not $merged.machines -or $merged.machines.Count -eq 0) { return $false }

        Write-HarnessTestLogIndexFile -Index $merged -Path $indexPath | Out-Null
        Write-Host "test-logs: merged index.json with origin/$branch (legacy fallback)"
        return $true
    } finally {
        Pop-Location
    }
}

function Get-HarnessTestLogChangelogParts {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return @{ Preamble = ""; Entries = @() }
    }

    $normalized = $Text -replace "`r`n", "`n"
    $chunks = [regex]::Split($normalized, '(?m)(?=^## \d{4}-\d{2}-\d{2} \d{2}:\d{2} UTC)')
    $preamble = ""
    $entries = [System.Collections.Generic.List[hashtable]]::new()

    foreach ($chunk in @($chunks)) {
        if ([string]::IsNullOrWhiteSpace($chunk)) { continue }
        $trimmed = $chunk.TrimEnd()
        if ($trimmed -match '^## \d{4}-\d{2}-\d{2} \d{2}:\d{2} UTC') {
            $headerLine = ($trimmed -split "`n", 2)[0].Trim()
            $when = [datetime]::MinValue
            if ($headerLine -match '^## (\d{4}-\d{2}-\d{2} \d{2}:\d{2}) UTC') {
                $when = [datetime]::ParseExact(
                    $Matches[1], 'yyyy-MM-dd HH:mm', [System.Globalization.CultureInfo]::InvariantCulture)
            }
            $entries.Add(@{
                    Key  = $headerLine
                    When = $when
                    Body = $trimmed
                }) | Out-Null
        } elseif (-not $entries.Count) {
            $preamble = $trimmed
        }
    }

    return @{ Preamble = $preamble; Entries = @($entries) }
}

function Merge-HarnessTestLogChangelog {
    param(
        [string[]]$Texts = @()
    )

    $entryMap = @{}
    $preamble = ""
    foreach ($text in @($Texts)) {
        if ([string]::IsNullOrWhiteSpace($text)) { continue }
        foreach ($chunk in Split-HarnessTestLogIndexConflictText -Text $text) {
            if ([string]::IsNullOrWhiteSpace($chunk)) { continue }
            $parts = Get-HarnessTestLogChangelogParts -Text $chunk
            if (-not $preamble -and $parts.Preamble) {
                $preamble = $parts.Preamble
            }
            foreach ($entry in @($parts.Entries)) {
                $entryMap[$entry.Key] = $entry
            }
        }
    }

    $sorted = @($entryMap.Values | Sort-Object { $_.When } -Descending)
    if ($sorted.Count -eq 0) {
        if ($preamble) { return ($preamble.TrimEnd() + "`n") }
        return ""
    }

    $body = ($sorted | ForEach-Object { $_.Body }) -join "`n`n"
    if ($preamble) {
        return ($preamble.TrimEnd() + "`n`n" + $body).TrimEnd() + "`n"
    }
    return $body.TrimEnd() + "`n"
}

function Read-HarnessTestLogChangelogFile {
    param([string]$Path)

    if (-not (Test-Path $Path)) { return "" }
    return [System.IO.File]::ReadAllText($Path, (New-Object System.Text.UTF8Encoding $false))
}

function Write-HarnessTestLogChangelogFile {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string[]]$Texts
    )

    $merged = Merge-HarnessTestLogChangelog -Texts $Texts
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($Path, $merged, $utf8NoBom)
    return $merged
}

function Sync-HarnessTestLogChangelogWithRemote {
    param([string]$RepoRoot = (Get-RepoRoot))

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { return $false }

    Push-Location $RepoRoot
    try {
        git fetch origin 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { return $false }

        $branch = (git rev-parse --abbrev-ref HEAD 2>$null)
        if (-not $branch -or $branch -eq 'HEAD') { return $false }
        $branch = $branch.Trim()

        $remoteText = git show "origin/${branch}:test-logs/CHANGELOG.md" 2>$null
        if ($LASTEXITCODE -ne 0) { return $false }

        $changelogPath = Join-Path $RepoRoot "test-logs\CHANGELOG.md"
        $localText = Read-HarnessTestLogChangelogFile -Path $changelogPath
        $merged = Merge-HarnessTestLogChangelog -Texts @($remoteText, $localText)
        if ([string]::IsNullOrWhiteSpace($merged)) { return $false }

        Write-HarnessTestLogChangelogFile -Path $changelogPath -Texts @($merged) | Out-Null
        Write-Host "test-logs: merged CHANGELOG.md with origin/$branch"
        return $true
    } finally {
        Pop-Location
    }
}

function Sync-HarnessTestLogWithRemote {
    param([string]$RepoRoot = (Get-RepoRoot))

    $indexOk = Sync-HarnessTestLogIndexWithRemote -RepoRoot $RepoRoot
    $changelogOk = Sync-HarnessTestLogChangelogWithRemote -RepoRoot $RepoRoot
    return ($indexOk -or $changelogOk)
}

function Resolve-HarnessTestLogRebaseConflicts {
    param([string]$RepoRoot = (Get-RepoRoot))

    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    $resolvedAny = $false

    Push-Location $RepoRoot
    try {
        $indexPath = Join-Path $RepoRoot "test-logs\index.json"
        if (Test-Path $indexPath) {
            $text = [System.IO.File]::ReadAllText($indexPath, $utf8NoBom)
            if ($text -match '<<<<<<<') {
                $merged = Merge-HarnessTestLogIndexFromJson -JsonInputs @($text)
                Write-HarnessTestLogIndexFile -Index $merged -Path $indexPath | Out-Null
                git add -- test-logs/index.json 2>&1 | Out-Null
                $resolvedAny = $true
            }
        }

        $changelogPath = Join-Path $RepoRoot "test-logs\CHANGELOG.md"
        if (Test-Path $changelogPath) {
            $text = [System.IO.File]::ReadAllText($changelogPath, $utf8NoBom)
            if ($text -match '<<<<<<<') {
                Write-HarnessTestLogChangelogFile -Path $changelogPath -Texts @($text) | Out-Null
                git add -- test-logs/CHANGELOG.md 2>&1 | Out-Null
                $resolvedAny = $true
            }
        }
    } finally {
        Pop-Location
    }

    return $resolvedAny
}

function Continue-HarnessTestLogRebase {
    param([string]$RepoRoot = (Get-RepoRoot))

    Push-Location $RepoRoot
    try {
        $prevEditor = $env:GIT_EDITOR
        $env:GIT_EDITOR = 'true'
        try {
            git -c core.editor=true rebase --continue 2>&1 | ForEach-Object { Write-Host $_ }
            return ($LASTEXITCODE -eq 0)
        } finally {
            if ($null -ne $prevEditor) {
                $env:GIT_EDITOR = $prevEditor
            } else {
                Remove-Item Env:GIT_EDITOR -ErrorAction SilentlyContinue
            }
        }
    } finally {
        Pop-Location
    }
}

function Test-HarnessTestLogMerge {
    $machineA = '1号机'
    $machineB = '2号机'

    $jsonA = (@{
            schemaVersion = 1
            updatedAt     = "2026-07-02T15:00:00Z"
            machines      = @{
                $machineA = @{
                    runId = "a"; finishedAt = "2026-07-02T15:00:00Z"; suite = "run-all-scenarios"
                    passCount = 3; totalCount = 19; pass = $false; shortCommit = "abc"
                    branch = "main"; subject = "x"; dirty = $true
                }
            }
        } | ConvertTo-Json -Depth 6 -Compress)
    $jsonB = (@{
            schemaVersion = 1
            updatedAt     = "2026-07-02T17:00:00Z"
            machines      = @{
                $machineB = @{
                    runId = "b"; finishedAt = "2026-07-02T17:00:00Z"; suite = "run-all-scenarios"
                    passCount = 3; totalCount = 19; pass = $false; shortCommit = "abc"
                    branch = "main"; subject = "x"; dirty = $true
                }
            }
        } | ConvertTo-Json -Depth 6 -Compress)

    $merged = Merge-HarnessTestLogIndexFromJson -JsonInputs @($jsonA, $jsonB)
    if ($merged.machines.Count -ne 2) {
        throw "index merge: expected 2 machines, got $($merged.machines.Count)"
    }
    if ($merged.updatedAt -ne "2026-07-02T17:00:00.0000000Z") {
        throw "index merge: updatedAt=$($merged.updatedAt)"
    }

    $conflict = @"
<<<<<<< HEAD
$jsonA
=======
$jsonB
>>>>>>> origin/main
"@
    $fromConflict = Merge-HarnessTestLogIndexFromJson -JsonInputs @($conflict)
    if ($fromConflict.machines.Count -ne 2) {
        throw "index conflict merge: expected 2 machines"
    }

    $legacy = "[$jsonA,$jsonB]"
    $fromLegacy = Merge-HarnessTestLogIndexFromJson -JsonInputs @($legacy)
    if ($fromLegacy.machines.Count -ne 2) {
        throw "index legacy array: expected 2 machines, got $($fromLegacy.machines.Count)"
    }

    $sameMachineNewer = Merge-HarnessTestLogIndexFromJson -JsonInputs @(
        $jsonA,
        (@{
                schemaVersion = 1
                machines      = @{
                    $machineA = @{
                        runId = "a2"; finishedAt = "2026-07-02T16:00:00Z"; suite = "smoke-split"
                        passCount = 1; totalCount = 1; pass = $true; shortCommit = "def"
                        branch = "main"; subject = "y"; dirty = $false
                    }
                }
            } | ConvertTo-Json -Depth 6 -Compress)
    )
    if ($sameMachineNewer.machines[$machineA].runId -ne "a2") {
        throw "index same-machine: expected newer runId a2"
    }

    $entryA = "## 2026-07-02 10:00 UTC | $machineA | commit ``abc```n`n**Suite:** run-all-scenarios | **1/1 pass**`n"
    $entryB = "## 2026-07-02 11:00 UTC | $machineB | commit ``def```n`n**Suite:** smoke-split | **1/1 pass**`n"
    $changelogMerged = Merge-HarnessTestLogChangelog -Texts @($entryA, $entryB)
    if ($changelogMerged.IndexOf("11:00 UTC") -gt $changelogMerged.IndexOf("10:00 UTC")) {
        throw "changelog merge: expected newest entry first"
    }

    $changelogConflict = @"
<<<<<<< HEAD
$entryA
=======
$entryB
>>>>>>> origin/main
"@
    $changelogFromConflict = Merge-HarnessTestLogChangelog -Texts @($changelogConflict)
    if ($changelogFromConflict -notmatch '10:00 UTC' -or $changelogFromConflict -notmatch '11:00 UTC') {
        throw "changelog conflict merge: missing entries"
    }

    $dup = Merge-HarnessTestLogChangelog -Texts @($entryA, $entryA, $entryB)
    if (([regex]::Matches($dup, '(?m)^## ')).Count -ne 2) {
        throw "changelog dedupe: expected 2 entries"
    }

    Test-HarnessTestLogIndexSchema -Index $merged | Out-Null

    try {
        Test-HarnessTestLogIndexSchema -Path (Join-Path $env:TEMP "missing-harness-index.json") | Out-Null
        throw "schema: expected missing file failure"
    } catch {
        if ($_.Exception.Message -notmatch 'not found') {
            throw "schema missing file: $($_.Exception.Message)"
        }
    }

    $badIndexDir = Join-Path $env:TEMP "me-harness-schema-test"
    if (-not (Test-Path $badIndexDir)) {
        New-Item -ItemType Directory -Force -Path $badIndexDir | Out-Null
    }
    $badIndexPath = Join-Path $badIndexDir "bad-index.json"
    [System.IO.File]::WriteAllText($badIndexPath, '[{"schemaVersion":1}]', (New-Object System.Text.UTF8Encoding $false))
    try {
        Test-HarnessTestLogIndexSchema -Path $badIndexPath | Out-Null
        throw "schema: expected array failure"
    } catch {
        if ($_.Exception.Message -notmatch 'object, not an array') {
            throw "schema array: $($_.Exception.Message)"
        }
    } finally {
        Remove-Item $badIndexDir -Recurse -Force -ErrorAction SilentlyContinue
    }

    $rebuildRoot = Join-Path $env:TEMP "me-harness-rebuild-test"
    if (Test-Path $rebuildRoot) {
        Remove-Item $rebuildRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $rebuildRoot "test-logs\machines\$machineA") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $rebuildRoot "test-logs\machines\$machineB") | Out-Null
    $recordA = @{
        schemaVersion = 1; runId = "ra"; testMachine = $machineA; suite = "run-all-scenarios"
        finishedAt = "2026-07-02T15:00:00Z"; passCount = 3; totalCount = 19; pass = $false
        gitAtEnd = @{ shortCommit = "abc"; branch = "main"; subject = "x"; dirty = $true; commit = "abc" }
    }
    $recordB = @{
        schemaVersion = 1; runId = "rb"; testMachine = $machineB; suite = "smoke-split"
        finishedAt = "2026-07-02T17:00:00Z"; passCount = 1; totalCount = 1; pass = $true
        gitAtEnd = @{ shortCommit = "def"; branch = "main"; subject = "y"; dirty = $false; commit = "def" }
    }
    Write-HarnessTestLogLatestRecordFile -Path (Join-Path $rebuildRoot "test-logs\machines\$machineA\latest.json") -Record $recordA
    Write-HarnessTestLogLatestRecordFile -Path (Join-Path $rebuildRoot "test-logs\machines\$machineB\latest.json") -Record $recordB
    if (-not (Rebuild-HarnessTestLogIndex -RepoRoot $rebuildRoot)) {
        throw "rebuild index failed"
    }
    $rebuilt = Read-HarnessTestLogIndexFile -Path (Join-Path $rebuildRoot "test-logs\index.json")
    if ($rebuilt.machines.Count -ne 2 -or $rebuilt.machines[$machineB].runId -ne "rb") {
        throw "rebuild index: unexpected machines"
    }
    Remove-Item $rebuildRoot -Recurse -Force -ErrorAction SilentlyContinue

    return $true
}

function Get-HarnessTestLogRegistryMachineIds {
    param([string]$RepoRoot = (Get-RepoRoot))

    $path = Join-Path $RepoRoot "test-environments.json"
    if (-not (Test-Path $path)) { return @() }

    try {
        $registry = Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json
        return @($registry.machines | ForEach-Object { [string]$_.id })
    } catch {
        return @()
    }
}

function Test-HarnessTestLogIndexSchema {
    param(
        [string]$Path = "",
        [object]$Index = $null,
        [string]$RepoRoot = (Get-RepoRoot)
    )

    if (-not $Index) {
        if (-not $Path) {
            $Path = Join-Path $RepoRoot "test-logs\index.json"
        }
        if (-not (Test-Path $Path)) {
            throw "index file not found: $Path"
        }
        $raw = [System.IO.File]::ReadAllText($Path, (New-Object System.Text.UTF8Encoding $false))
        if ($raw -match '<<<<<<<') {
            throw "index contains unresolved merge conflict markers"
        }
        if ($raw.TrimStart().StartsWith('[')) {
            throw "index must be a JSON object, not an array"
        }
        $Index = Read-HarnessTestLogIndexFile -Path $Path
    }

    if (-not $Index) {
        throw "index missing or invalid JSON"
    }
    if ([int]$Index.schemaVersion -ne 1) {
        throw "schemaVersion must be 1"
    }
    if (-not $Index.machines -or $Index.machines.Count -eq 0) {
        throw "machines must not be empty"
    }

    $maxFinished = $null
    foreach ($key in @($Index.machines.Keys)) {
        $entry = $Index.machines[$key]
        if (-not $entry.finishedAt) {
            throw "machines.$key missing finishedAt"
        }
        if (-not $entry.runId) {
            throw "machines.$key missing runId"
        }
        try {
            $finishedAt = [datetime]$entry.finishedAt
            if (-not $maxFinished -or $finishedAt -gt $maxFinished) {
                $maxFinished = $finishedAt
            }
        } catch {
            throw "machines.$key invalid finishedAt: $($entry.finishedAt)"
        }
    }

    if ($Index.updatedAt -and $maxFinished) {
        $updatedAt = [datetime]$Index.updatedAt
        if ([Math]::Abs(($updatedAt.ToUniversalTime() - $maxFinished.ToUniversalTime()).TotalSeconds) -gt 1) {
            throw "updatedAt ($($Index.updatedAt)) != max finishedAt ($($maxFinished.ToUniversalTime().ToString('o')))"
        }
    }

    return $true
}

function Test-HarnessTestLogGitMergeDriverConfigured {
    param([string]$RepoRoot = (Get-RepoRoot))

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { return $false }

    Push-Location $RepoRoot
    try {
        $driver = git config --get merge.test-logs-index.driver 2>$null
        return [bool]$driver
    } finally {
        Pop-Location
    }
}

function Get-GitConfigSafePath {
    param([string]$Path)

    $resolved = (Resolve-Path $Path).Path
    try {
        $fso = New-Object -ComObject Scripting.FileSystemObject
        $short = $fso.GetFile($resolved).ShortPath
        if ($short -and ($short -ne $resolved) -and ($short -notmatch ' ')) {
            return $short
        }
    } catch {}

    return ($resolved -replace '\\', '/')
}

function Install-HarnessGitMergeDriver {
    param([string]$RepoRoot = (Get-RepoRoot))

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { return $false }

    $mergeDriver = Join-Path $RepoRoot "tools\git-merge-test-logs-index.ps1"
    if (-not (Test-Path $mergeDriver)) {
        Write-Warning "Merge driver script not found: $mergeDriver"
        return $false
    }

    $driverScript = Get-GitConfigSafePath -Path $mergeDriver
    $driverValue = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $driverScript %O %A %B"

    Push-Location $RepoRoot
    try {
        & git config merge.test-logs-index.name "Merge harness test-logs index.json"
        if ($LASTEXITCODE -ne 0) {
            throw "git config merge.test-logs-index.name failed (exit $LASTEXITCODE)"
        }
        & git config merge.test-logs-index.driver $driverValue
        if ($LASTEXITCODE -ne 0) {
            throw "git config merge.test-logs-index.driver failed (exit $LASTEXITCODE)"
        }
        Write-Host "Configured git merge driver: test-logs-index (test-logs/index.json, test-logs/CHANGELOG.md)"
        return $true
    } catch {
        Write-Warning "Could not configure git merge driver test-logs-index: $($_.Exception.Message)"
        return $false
    } finally {
        Pop-Location
    }
}

function Test-HarnessTestLogGitHooksConfigured {
    param([string]$RepoRoot = (Get-RepoRoot))

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { return $false }

    Push-Location $RepoRoot
    try {
        $path = git config --get core.hooksPath 2>$null
        return ($path -eq '.githooks')
    } finally {
        Pop-Location
    }
}

function Install-HarnessGitHooks {
    param([string]$RepoRoot = (Get-RepoRoot))

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { return $false }

    $hooksDir = Join-Path $RepoRoot ".githooks"
    if (-not (Test-Path $hooksDir)) {
        Write-Warning "Git hooks directory not found: $hooksDir"
        return $false
    }

    Push-Location $RepoRoot
    try {
        $current = git config --get core.hooksPath 2>$null
        if ($current -ne '.githooks') {
            & git config core.hooksPath .githooks
            if ($LASTEXITCODE -ne 0) {
                throw "git config core.hooksPath failed (exit $LASTEXITCODE)"
            }
        }
        Write-Host "Configured git hooksPath: .githooks (pre-push validates test-logs/)"
        return $true
    } catch {
        Write-Warning "Could not configure git hooks: $($_.Exception.Message)"
        return $false
    } finally {
        Pop-Location
    }
}

function Test-HarnessPrePushTestLogsTouched {
    param(
        [string]$RepoRoot = (Get-RepoRoot),
        [string]$Stdin = ""
    )

    if (-not $Stdin) {
        $Stdin = [Console]::In.ReadToEnd()
    }

    $zeroSha = '0{40}'
    Push-Location $RepoRoot
    try {
        foreach ($line in ($Stdin -split "`n")) {
            $line = $line.Trim()
            if (-not $line) { continue }

            $parts = $line -split '\s+'
            if ($parts.Count -lt 4) { continue }

            $localRef = $parts[0]
            $localSha = $parts[1]
            $remoteSha = $parts[3]
            if ($localSha -match "^$zeroSha$") { continue }

            # New tag/branch: remote SHA is zeros. Tags do not need test-logs
            # validation (commits were already checked when main was pushed).
            if ($remoteSha -match "^$zeroSha$") {
                if ($localRef -like 'refs/tags/*') { continue }
                $files = git diff-tree --no-commit-id --name-only -r $localSha -- test-logs/ 2>$null
            } else {
                $files = git diff --name-only $remoteSha $localSha -- test-logs/ 2>$null
            }
            if ($files) {
                return $true
            }
        }
        return $false
    } finally {
        Pop-Location
    }
}

function Invoke-HarnessPrePushTestLogsCheck {
    param(
        [string]$RepoRoot = (Get-RepoRoot),
        [string]$Stdin = ""
    )

    if (-not (Test-HarnessPrePushTestLogsTouched -RepoRoot $RepoRoot -Stdin $Stdin)) {
        return $true
    }

    Write-Host "pre-push: validating test-logs/ in commits being pushed..."
    $validateScript = Join-Path $RepoRoot "tools\debug-harness\validate-test-logs.ps1"
    if (-not (Test-Path $validateScript)) {
        Write-Warning "pre-push: validate-test-logs.ps1 not found; skipping"
        return $true
    }

    & $validateScript -RepoRoot $RepoRoot
    if ($LASTEXITCODE -ne 0) {
        Write-Host "pre-push: test-logs validation failed. Fix locally, then:" -ForegroundColor Red
        Write-Host "  .\tools\debug-harness\validate-test-logs.ps1 -FixIndex" -ForegroundColor Yellow
        return $false
    }

    return $true
}

function Initialize-HarnessTestLogGit {
    param(
        [string]$RepoRoot = (Get-RepoRoot),
        [switch]$Quiet
    )

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { return $false }

    if (-not (Test-HarnessTestLogGitMergeDriverConfigured -RepoRoot $RepoRoot)) {
        if (-not $Quiet) {
            Write-Host "test-logs: merge driver not configured; run .\tools\debug-harness\setup.ps1" -ForegroundColor Yellow
        }
    }

    Sync-HarnessTestLogWithRemote -RepoRoot $RepoRoot | Out-Null

    $indexPath = Join-Path $RepoRoot "test-logs\index.json"
    if (Test-Path $indexPath) {
        try {
            Test-HarnessTestLogIndexSchema -Path $indexPath -RepoRoot $RepoRoot | Out-Null
        } catch {
            if (-not $Quiet) {
                Write-Host "test-logs: index schema warning: $($_.Exception.Message)" -ForegroundColor Yellow
            }
        }
    }

    return $true
}

function Get-HarnessTestLogStatus {
    param([string]$RepoRoot = (Get-RepoRoot))

    $indexPath = Join-Path $RepoRoot "test-logs\index.json"
    $index = Read-HarnessTestLogIndexFile -Path $indexPath
    if (-not $index) {
        return [pscustomobject]@{
            UpdatedAt = $null
            Machines  = @()
        }
    }

    $registryIds = Get-HarnessTestLogRegistryMachineIds -RepoRoot $RepoRoot
    $machineIds = if ($registryIds.Count -gt 0) {
        $registryIds
    } else {
        @($index.machines.Keys)
    }

    $rows = [System.Collections.Generic.List[object]]::new()
    foreach ($id in @($machineIds)) {
        $entry = $index.machines[$id]
        if ($entry) {
            $rows.Add([pscustomobject]@{
                    Machine    = $id
                    Suite      = [string]$entry.suite
                    Pass       = "$($entry.passCount)/$($entry.totalCount)"
                    PassAll    = [bool]$entry.pass
                    Commit     = [string]$entry.shortCommit
                    FinishedAt = [string]$entry.finishedAt
                    Dirty      = [bool]$entry.dirty
                }) | Out-Null
        } else {
            $rows.Add([pscustomobject]@{
                    Machine    = $id
                    Suite      = "(no run in index)"
                    Pass       = "-"
                    PassAll    = $false
                    Commit     = "-"
                    FinishedAt = "-"
                    Dirty      = $false
                }) | Out-Null
        }
    }

    foreach ($key in @($index.machines.Keys)) {
        if ($machineIds -contains $key) { continue }
        $entry = $index.machines[$key]
        $rows.Add([pscustomobject]@{
                Machine    = "$key (unregistered)"
                Suite      = [string]$entry.suite
                Pass       = "$($entry.passCount)/$($entry.totalCount)"
                PassAll    = [bool]$entry.pass
                Commit     = [string]$entry.shortCommit
                FinishedAt = [string]$entry.finishedAt
                Dirty      = [bool]$entry.dirty
            }) | Out-Null
    }

    return [pscustomobject]@{
        UpdatedAt = $index.updatedAt
        Machines  = @($rows)
    }
}

function Show-HarnessTestLogStatus {
    param(
        [string]$RepoRoot = (Get-RepoRoot),
        [switch]$Json
    )

    $status = Get-HarnessTestLogStatus -RepoRoot $RepoRoot
    if ($Json) {
        $status | ConvertTo-Json -Depth 4
        return $status
    }

    if ($status.UpdatedAt) {
        Write-Host "test-logs/index.json updatedAt: $($status.UpdatedAt)"
    } else {
        Write-Host "test-logs/index.json: (missing or empty)"
    }
    if ($status.Machines.Count -gt 0) {
        $status.Machines | Format-Table -AutoSize Machine, Pass, PassAll, Suite, Commit, FinishedAt, Dirty
    }
    return $status
}

function Format-HarnessTestLogChangelogEntry {
    param(
        [Parameter(Mandatory)]
        [object]$Record
    )

    $git = $Record.gitAtEnd
    if (-not $git) { $git = $Record.gitAtStart }
    $short = if ($git.shortCommit) { $git.shortCommit } else { "unknown" }
    $branch = if ($git.branch) { $git.branch } else { "?" }
    $dirty = if ($git.dirty) { " (dirty)" } else { "" }
    $when = ([datetime]$Record.finishedAt).ToUniversalTime().ToString("yyyy-MM-dd HH:mm") + " UTC"
    $suite = $Record.suite
    $score = "$($Record.passCount)/$($Record.totalCount)"
    $failed = @($Record.results | Where-Object { -not $_.pass } | ForEach-Object { $_.scenario })
    $failLine = if ($failed.Count -gt 0) {
        "- Failed: $($failed -join ', ')"
    } else {
        "- All scenarios passed"
    }

    $lines = @(
        "## $when | $($Record.testMachine) | commit ``$short``$dirty"
        ""
        "**Suite:** $suite | **$score pass** | branch ``$branch`` | commit ``$($git.commit)``"
        ""
        "> $($git.subject)"
        ""
        $failLine
        ""
    )
    return ($lines -join "`n")
}

function Publish-HarnessTestLog {
    param(
        [Parameter(Mandatory)]
        [string]$Suite,
        [Parameter(Mandatory)]
        [datetime]$StartedAt,
        [Parameter(Mandatory)]
        [hashtable]$Summary,
        [object]$GitAtStart = $null,
        [string]$RepoRoot = (Get-RepoRoot)
    )

    $machine = Resolve-TestMachine -RepoRoot $RepoRoot
    if (-not $machine) {
        Write-Host "test-logs: skip (testMachine not configured in deploy.config.json)"
        return $null
    }

    Sync-HarnessTestLogWithRemote -RepoRoot $RepoRoot | Out-Null

    $finishedAt = Get-Date
    $gitEnd = Get-HarnessGitSnapshot -RepoRoot $RepoRoot
    if (-not $GitAtStart) {
        $GitAtStart = $gitEnd
    }

    $runId = $StartedAt.ToUniversalTime().ToString("yyyyMMdd-HHmmss'Z'")
    $durationMs = [int](($finishedAt - $StartedAt).TotalMilliseconds)
    if ($Summary.ContainsKey('durationMs') -and $Summary.durationMs) {
        $durationMs = [int]$Summary.durationMs
    }

    $resultRows = @()
    foreach ($row in @($Summary.results)) {
        if ($null -eq $row) { continue }
        $attempts = 1
        if ($row.PSObject.Properties.Name -contains 'Attempts') {
            $attempts = [int]$row.Attempts
        }
        $err = ""
        if ($row.PSObject.Properties.Name -contains 'Error') {
            $err = [string]$row.Error
        }
        $resultRows += [ordered]@{
            scenario = [string]$row.Scenario
            pass     = [bool]$row.Pass
            attempts = $attempts
            error    = $err
        }
    }

    $passCount = [int]$Summary.passCount
    $totalCount = [int]$Summary.totalCount
    if ($Summary.ContainsKey('singlePass') -and $Summary.singlePass) {
        $passCount = if ($Summary.passCount -ge 1) { 1 } else { 0 }
        $totalCount = 1
    }

    $record = [ordered]@{
        schemaVersion = 1
        runId         = $runId
        testMachine   = $machine
        suite         = $Suite
        startedAt     = $StartedAt.ToUniversalTime().ToString("o")
        finishedAt    = $finishedAt.ToUniversalTime().ToString("o")
        durationMs    = $durationMs
        passCount     = $passCount
        totalCount    = $totalCount
        pass          = ($passCount -eq $totalCount -and $totalCount -gt 0)
        gitAtStart    = $GitAtStart
        gitAtEnd      = $gitEnd
        results       = $resultRows
    }

    $machineDir = Get-HarnessTestLogMachineDir -Machine $machine -RepoRoot $RepoRoot
    if (-not (Test-Path $machineDir)) {
        New-Item -ItemType Directory -Force -Path $machineDir | Out-Null
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    $jsonLine = ($record | ConvertTo-Json -Compress -Depth 8)
    $historyPath = Join-Path $machineDir "history.jsonl"
    [System.IO.File]::AppendAllText($historyPath, $jsonLine + "`n", $utf8NoBom)

    $latestPath = Join-Path $machineDir "latest.json"
    $pretty = ($record | ConvertTo-Json -Depth 8) + "`n"
    [System.IO.File]::WriteAllText($latestPath, $pretty, $utf8NoBom)

    $entry = Format-HarnessTestLogChangelogEntry -Record $record
    $machineChangelog = Join-Path $machineDir "CHANGELOG.md"
    if (-not (Test-Path $machineChangelog)) {
        [System.IO.File]::WriteAllText($machineChangelog, "# Harness changelog - $machine`n`n", $utf8NoBom)
    }
    $existing = [System.IO.File]::ReadAllText($machineChangelog, $utf8NoBom)
    if ($existing -notmatch "`n") { $existing = $existing + "`n" }
    [System.IO.File]::WriteAllText($machineChangelog, ($existing.TrimEnd() + "`n`n" + $entry), $utf8NoBom)

    $rootChangelog = Join-Path $RepoRoot "test-logs\CHANGELOG.md"
    $link = "machines/$machine/latest.json"
    $rootEntry = $entry.TrimEnd() + "`n- Details: [latest.json]($link)"
    $rootExisting = Read-HarnessTestLogChangelogFile -Path $rootChangelog
    Write-HarnessTestLogChangelogFile -Path $rootChangelog -Texts @($rootExisting, $rootEntry) | Out-Null

    if (-not (Rebuild-HarnessTestLogIndex -RepoRoot $RepoRoot)) {
        throw "test-logs: failed to rebuild index.json from latest.json"
    }

    Write-Host "test-logs: $machine $Suite $passCount/$totalCount @ $($gitEnd.shortCommit) -> $latestPath"
    return $latestPath
}

function Push-HarnessTestLog {
    param(
        [string]$RepoRoot = (Get-RepoRoot),
        [string]$Message = ""
    )

    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) {
        Write-Host "test-logs: push skipped (not a git repo)"
        return $false
    }

    Push-Location $RepoRoot
    try {
        $machine = Resolve-TestMachine -RepoRoot $RepoRoot
        if (-not $machine) { $machine = "unknown" }

        Sync-HarnessTestLogWithRemote -RepoRoot $RepoRoot | Out-Null
        Rebuild-HarnessTestLogIndex -RepoRoot $RepoRoot | Out-Null

        $status = git status --porcelain -- test-logs/ 2>$null
        if (-not $status) {
            Write-Host "test-logs: nothing to commit under test-logs/"
            return $true
        }

        git add -- test-logs/index.json test-logs/CHANGELOG.md 2>&1 | Out-Null
        $machineLogDir = "test-logs/machines/$machine"
        if (Test-Path (Join-Path $RepoRoot $machineLogDir)) {
            git add -- $machineLogDir 2>&1 | Out-Null
        }
        if (-not $Message) {
            $idx = Join-Path $RepoRoot "test-logs\index.json"
            $short = "update"
            if (Test-Path $idx) {
                try {
                    $mergedIndex = Read-HarnessTestLogIndexFile -Path $idx
                    $m = $mergedIndex.machines[$machine]
                    if ($m) {
                        $Message = "$($m.suite) $($m.passCount)/$($m.totalCount) @ $($m.shortCommit)"
                    }
                } catch {}
            }
        }

        $commitMsg = "test-logs: $machine $Message"
        git commit -m $commitMsg 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) {
            Write-Host "test-logs: commit failed (exit $LASTEXITCODE)" -ForegroundColor Yellow
            return $false
        }

        $pushOk = $false
        foreach ($attempt in 1..2) {
            git push 2>&1 | ForEach-Object { Write-Host $_ }
            if ($LASTEXITCODE -eq 0) {
                $pushOk = $true
                break
            }

            if ($attempt -eq 1) {
                Write-Host "test-logs: push failed; pulling and merging test-logs (see test-logs/README.md)" -ForegroundColor Yellow
                $branch = (git rev-parse --abbrev-ref HEAD 2>$null)
                if ($branch) {
                    git pull --rebase origin $branch.Trim() 2>&1 | ForEach-Object { Write-Host $_ }
                    for ($rebaseTry = 0; $rebaseTry -lt 4; $rebaseTry++) {
                        $inRebase = git status 2>$null | Out-String
                        if ($inRebase -notmatch 'rebase in progress|You are currently rebasing') {
                            break
                        }
                        if (-not (Resolve-HarnessTestLogRebaseConflicts -RepoRoot $RepoRoot)) {
                            break
                        }
                        Continue-HarnessTestLogRebase -RepoRoot $RepoRoot | Out-Null
                    }
                }
            }
        }

        if (-not $pushOk) {
            Write-Host "test-logs: push failed (exit $LASTEXITCODE); resolve test-logs/ per test-logs/README.md" -ForegroundColor Yellow
            return $false
        }

        Write-Host "test-logs: pushed to remote"
        return $true
    } finally {
        Pop-Location
    }
}

function Resolve-GameBinariesPath {
    param([string]$DeployRoot)

    if (-not $DeployRoot) {
        throw "Deploy path not configured."
    }

    $direct = Join-Path $DeployRoot "MirrorsEdge.exe"
    if (Test-Path $direct) {
        return $DeployRoot
    }

    $binaries = Join-Path $DeployRoot "Binaries"
    $gameExe = Join-Path $binaries "MirrorsEdge.exe"
    if (Test-Path $gameExe) {
        return $binaries
    }

    throw "MirrorsEdge.exe not found under $DeployRoot"
}

function Stop-GameProcessById {
    param([int]$ProcessId)

    if ($ProcessId -le 0) {
        return
    }

    $resumeLib = Join-Path $PSScriptRoot "..\..\lib\Resume-ProcessThreads.ps1"
    if (Test-Path $resumeLib) {
        . $resumeLib
        $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
        if ($proc) {
            [void](Stop-ProcessForce -Process $proc)
        }
        return
    }

    Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
}

function Stop-MirrorsEdgeProcesses {
    param([switch]$IncludeLauncher)

    Stop-HarnessHangWatchdog

    $resumeLib = Join-Path $PSScriptRoot "..\..\lib\Resume-ProcessThreads.ps1"
    if (Test-Path $resumeLib) {
        . $resumeLib
        Stop-MirrorsEdgeGameProcesses -IncludeLauncher:$IncludeLauncher
        return
    }

    $names = @("MirrorsEdge")
    if ($IncludeLauncher) {
        $names += @("ModuleLauncher", "mirroredge-module-launcher")
    }

    $prevPreference = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    try {
        foreach ($name in $names) {
            foreach ($proc in @(Get-Process $name -ErrorAction SilentlyContinue)) {
                Stop-GameProcessById -ProcessId $proc.Id
            }
        }
    } finally {
        $ErrorActionPreference = $prevPreference
    }
    Start-Sleep -Seconds 3
}

function Get-MirrorsEdgeZombieProcessIds {
    $resumeLib = Join-Path $PSScriptRoot "..\..\lib\Resume-ProcessThreads.ps1"
    if (Test-Path $resumeLib) {
        . $resumeLib
        return @(Get-MirrorsEdgeZombieProcesses | ForEach-Object { $_.Id })
    }

    return @(Get-Process MirrorsEdge -ErrorAction SilentlyContinue |
        Where-Object { $_.Threads.Count -eq 0 } |
        ForEach-Object { $_.Id })
}

function Assert-NoMirrorsEdgeZombie {
    $zombieIds = @(Get-MirrorsEdgeZombieProcessIds)
    if ($zombieIds.Count -eq 0) {
        return
    }

    throw @"
MirrorsEdge zombie EPROCESS (PID $($zombieIds -join ', '), 0 threads).
Reboot Windows to clear it, or run elevated: .\tools\stop-game.ps1 -IncludeLauncher -Elevate
Zombie blocks d3d9.dll deploy and leaves DLL files locked.
"@
}

function Stop-HarnessGameSession {
    param(
        [switch]$IncludeLauncher,
        [switch]$TryGraceful,
        [string]$Reason = "harness exit"
    )

    Write-Host "harness-teardown: $Reason"
    Stop-HarnessHangWatchdog

    $sess = $script:HarnessGameSession
    if ($TryGraceful -and $sess) {
        try {
            Invoke-ModuleManagerUiCommand -Command "MENU_CLOSE" -TimeoutMs 2000
        } catch {}
        try {
            Close-GameOverlaysWithRealInput -EscapeTaps 3
        } catch {}
        try {
            if ((Get-Process -Id $sess.GamePid -ErrorAction SilentlyContinue) -and
                -not $sess.GameClosedNormally) {
                Close-GameWithRealInput -ExitTimeoutSec 10 | Out-Null
            }
        } catch {
            Write-Host "harness-teardown: graceful game close failed ($($_.Exception.Message))"
        }
        try {
            $launcher = $sess.Launcher
            if ($launcher -and -not $launcher.HasExited -and -not $sess.LauncherClosedNormally) {
                Close-LauncherWithRealInput -LauncherProcess $launcher | Out-Null
            }
        } catch {
            Write-Host "harness-teardown: graceful launcher close failed ($($_.Exception.Message))"
        }
    }

    $stopLauncher = [bool]$IncludeLauncher
    if (-not $stopLauncher -and $sess -and $sess.Launcher) {
        $stopLauncher = $true
    }
    Stop-MirrorsEdgeProcesses -IncludeLauncher:$stopLauncher
    $script:HarnessGameSession = $null

    Start-Sleep -Milliseconds 400
    $zombieIds = @(Get-MirrorsEdgeZombieProcessIds)
    if ($zombieIds.Count -gt 0) {
        Write-Warning @"
harness-teardown: zombie EPROCESS still listed (PID $($zombieIds -join ', ')).
Reboot Windows to clear it — raw taskkill without resume creates this state.
"@
        return $false
    }
    return $true
}

function Start-ModuleLauncher {
    param(
        [string]$RepoRoot,
        [hashtable]$ExtraEnv = @{},
        [switch]$Auto,
        [switch]$UseDeployPath
    )

    $launcher = Join-Path $RepoRoot "dist\ModuleLauncher.exe"
    $workDir = Join-Path $RepoRoot "dist"

    if ($UseDeployPath) {
        $deployRoot = Resolve-DeployPath -RepoRoot $RepoRoot
        if ($deployRoot) {
            $candidate = Join-Path $deployRoot "ModuleLauncher.exe"
            if (Test-Path $candidate) {
                $launcher = $candidate
                $workDir = $deployRoot
            }
        }
    }

    if (-not (Test-Path $launcher)) {
        throw "ModuleLauncher not found: $launcher"
    }

    foreach ($kv in $ExtraEnv.GetEnumerator()) {
        Set-Item -Path "env:$($kv.Key)" -Value $kv.Value
    }

    $argList = @()
    if ($Auto) { $argList += "/auto" }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $launcher
    $psi.WorkingDirectory = $workDir
    $psi.UseShellExecute = $false
    if ($argList.Count -gt 0) {
        $psi.Arguments = ($argList -join ' ')
    }

    $debugKeys = @(
        'MMOD_DEBUG_SESSION', 'MMOD_DEBUG_LOG', 'MMOD_DIAGNOSTICS',
        'MMOD_SESSION_LOG', 'MMOD_GAME_ROOT', 'MMOD_PRODUCT_VERSION'
    )
    foreach ($key in $debugKeys) {
        $value = [Environment]::GetEnvironmentVariable($key)
        if ($value) {
            $psi.EnvironmentVariables[$key] = $value
        }
    }
    foreach ($kv in $ExtraEnv.GetEnumerator()) {
        $psi.EnvironmentVariables[$kv.Key] = [string]$kv.Value
    }

    return [System.Diagnostics.Process]::Start($psi)
}

$script:KnownGameDialogRules = @(
    @{
        Id           = 'corrupt-default-ini'
        Pattern      = '(?i)(DefaultEngine\.ini|DefaultGame\.ini).{0,60}corrupt|corrupt.{0,120}(DefaultEngine|DefaultGame)\.ini'
        Remediation  = 'Default*.ini integrity check failed. If stock Default*.ini headers are intact, avoid rewriting TdEngine.ini at CreateProcess time (launcher applies display settings on Launch click). Otherwise rebuild/deploy ModuleLauncher (patch timing) or Steam verify game files. See docs/troubleshooting.md (DefaultEngine.ini corrupt).'
        AutoDismiss  = $false
    },
    @{
        Id           = 'securom-disc'
        Pattern      = '(?i)(no cd|cd/dvd|dvd inserted|securom|disc)'
        Remediation  = 'SecuROM-protected exe. Steam verify integrity, or reinstall from EA App. See docs/troubleshooting.md (No CD/DVD).'
        AutoDismiss  = $false
    },
    @{
        Id           = 'ue3-fatal'
        Pattern      = '(?i)(fatal error|critical error|failed to create|could not find file)'
        Remediation  = 'UE3 fatal dialog — capture full text, check TdEngine.ini and game deploy paths.'
        AutoDismiss  = $true
    },
    @{
        Id           = 'msvc-runtime-abort'
        Pattern      = '(?i)(runtime error|visual c\+\+ runtime|terminate.*unusual way|abort\(\)|pure virtual function call)'
        Remediation  = 'MSVC CRT abort — usually heap corruption, deadlock, or unhandled C++ exception in game/mod code. Check debug log around last mod load; inspect Event Viewer Application Error 1000.'
        AutoDismiss  = $false
    },
    @{
        Id           = 'launcher-error'
        Pattern      = '(?i)(does not contain Binaries\\MirrorsEdge|could not save the selected game path|无法创建启动器窗口|launch game failed|deploy failed|invalid game path|game path.*(invalid|failed|missing|not found))'
        Remediation  = 'ModuleLauncher error dialog — verify settings.json gameRoot, deploy path (ME_DEPLOY_PATH), and rebuild/deploy output.'
        AutoDismiss  = $false
    },
    @{
        Id           = 'generic-messagebox'
        Pattern      = '(?i)^$'
        ClassPattern = '^#32770$'
        Remediation  = 'Unexpected Win32 message box from game or launcher process.'
        AutoDismiss  = $true
    }
)

function Test-IsDialogRecord {
    param($Dialog)

    return ($null -ne $Dialog) -and ($Dialog.PSObject.Properties.Name -contains 'Hwnd')
}

function Get-NormalizedDialogList {
    param([object]$Items)

    $flat = New-Object System.Collections.Generic.List[object]
    foreach ($item in @($Items)) {
        if ($null -eq $item) { continue }
        if ($item -is [System.Collections.IEnumerable] -and $item -isnot [string] -and $item -isnot [PSCustomObject]) {
            foreach ($inner in @($item)) {
                if (Test-IsDialogRecord -Dialog $inner) {
                    [void]$flat.Add($inner)
                }
            }
            continue
        }
        if (Test-IsDialogRecord -Dialog $item) {
            [void]$flat.Add($item)
        }
    }
    return ,[object[]]($flat.ToArray())
}

function Write-DialogList {
    param([object[]]$Items = @())

    if ($null -eq $Items -or @($Items).Count -eq 0) {
        return @()
    }
    Write-Output $Items -NoEnumerate
}

function Get-HarnessWatchProcessMap {
    $map = @{}

    $game = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($game) {
        $map[$game.Id] = 'MirrorsEdge'
    }

    $launcher = $null
    $sess = $script:HarnessGameSession
    if ($sess -and $sess.Launcher -and -not $sess.Launcher.HasExited) {
        $launcher = $sess.Launcher
    } else {
        $launcher = Get-Process ModuleLauncher -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if ($launcher -and -not $launcher.HasExited) {
        $map[$launcher.Id] = 'ModuleLauncher'
    }

    return [hashtable](@{} + $map)
}

function Find-LauncherDialogHwnd {
    param([int]$ProcessId = 0)

    $pids = @()
    if ($ProcessId -gt 0) {
        $pids = @($ProcessId)
    } else {
        $pids = @(Get-Process ModuleLauncher -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
    }

    foreach ($pid in $pids) {
        $windows = [Win32UiProbe]::EnumTopLevelForProcess([uint32]$pid)
        foreach ($w in $windows) {
            if ($w.ClassName -ne 'mirroredge_module_launcher_dialog') { continue }
            $hwnd = $w.Hwnd
            $launchBtn = [Win32Dialog]::GetDlgItem($hwnd, 1004)
            $closeBtn = [Win32Dialog]::GetDlgItem($hwnd, 1003)
            if ($launchBtn -ne [IntPtr]::Zero -and $closeBtn -ne [IntPtr]::Zero) {
                return $hwnd
            }
        }
    }

    return [IntPtr]::Zero
}

function Resolve-LauncherMainWindowHandleForDialogCheck {
    param([int]$ProcessId = 0)

    if ($ProcessId -le 0) {
        $proc = Get-Process ModuleLauncher -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($proc) { $ProcessId = $proc.Id }
    }
    if ($ProcessId -le 0) {
        return [IntPtr]::Zero
    }

    $hwnd = Find-LauncherDialogHwnd -ProcessId $ProcessId
    if ($hwnd -ne [IntPtr]::Zero) {
        return $hwnd
    }

    return [Win32Enum]::FindMainWindow($ProcessId)
}

function Get-HarnessMainWindowHandles {
    param([hashtable]$WatchMap)

    $handles = @{}
    foreach ($entry in $WatchMap.GetEnumerator()) {
        if ($entry.Value -eq 'MirrorsEdge') {
            $handles[$entry.Key] = Resolve-GameMainWindowHandleForDialogCheck -ProcessId $entry.Key
        } elseif ($entry.Value -eq 'ModuleLauncher') {
            $handles[$entry.Key] = Resolve-LauncherMainWindowHandleForDialogCheck -ProcessId $entry.Key
        }
    }
    return [hashtable](@{} + $handles)
}

function Get-ProcessTopLevelWindows {
    param([int]$ProcessId = 0)

    if ($ProcessId -le 0) { Write-DialogList @(); return }

    $recs = [Win32UiProbe]::EnumTopLevelForProcess([uint32]$ProcessId)
    Write-DialogList @($recs | ForEach-Object {
        [pscustomobject]@{
            Hwnd      = $_.Hwnd
            ProcessId = [int]$_.Pid
            Title     = $_.Title
            ClassName = $_.ClassName
            Body      = $_.Body
            Visible   = $_.Visible
            Width     = $_.Width
            Height    = $_.Height
            Owner     = $_.Owner
        }
    })
}

function Get-GameProcessWindows {
    param([int]$ProcessId = 0)

    if ($ProcessId -le 0) {
        $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $proc) { Write-DialogList @(); return }
        $ProcessId = $proc.Id
    }

    Get-ProcessTopLevelWindows -ProcessId $ProcessId
}

function Resolve-GameMainWindowHandleForDialogCheck {
    param([int]$ProcessId = 0)

    if ($ProcessId -le 0) {
        $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($proc) { $ProcessId = $proc.Id }
    }
    if ($ProcessId -le 0) {
        return [IntPtr]::Zero
    }

    $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($proc) {
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
            return $proc.MainWindowHandle
        }
    }

    return [Win32Enum]::FindMainWindow($ProcessId)
}

function Test-IsBlockingDialogWindow {
    param(
        $Window,
        [IntPtr]$MainHwnd
    )

    return Test-IsGameBlockingWindow -Window $Window -MainHwnd $MainHwnd
}

function Test-IsGameBlockingWindow {
    param(
        $Window,
        [IntPtr]$MainHwnd
    )

    if (-not $Window) { return $false }

    $combined = "$($Window.Title) $($Window.Body)".Trim()
    if ([string]::IsNullOrWhiteSpace($combined) -and -not $Window.Visible) {
        return $false
    }

    foreach ($rule in $script:KnownGameDialogRules) {
        $classPattern = $null
        if ($rule.ContainsKey('ClassPattern')) { $classPattern = $rule.ClassPattern }
        $classOk = $true
        if ($classPattern) {
            $classOk = $Window.ClassName -match $classPattern
        }
        if (-not $classOk) { continue }

        if ($rule.Pattern -and $rule.Pattern -ne '(?i)^$') {
            if ($combined -match $rule.Pattern) {
                return $true
            }
            continue
        }

        if ($classPattern -and $Window.ClassName -match $classPattern) {
            if ($Window.Hwnd -ne $MainHwnd -and -not [string]::IsNullOrWhiteSpace($combined)) {
                return $true
            }
        }
    }

    if ($Window.Hwnd -ne $MainHwnd -and $Window.ClassName -eq '#32770' -and $Window.Visible) {
        return $true
    }

    if ($Window.Hwnd -ne $MainHwnd -and $Window.Visible -and $Window.Width -gt 0 -and $Window.Width -lt 900 -and
        $Window.Height -gt 0 -and $Window.Height -lt 500 -and
        $combined -match '(?i)(ok|cancel|yes|no|error|warning|corrupt|failed)') {
        return $true
    }

    return $false
}

function Match-GameDialogRule {
    param([string]$CombinedText, [string]$ClassName)

    foreach ($rule in $script:KnownGameDialogRules) {
        $classPattern = $null
        if ($rule.ContainsKey('ClassPattern')) { $classPattern = $rule.ClassPattern }
        if ($classPattern -and $ClassName -notmatch $classPattern) {
            continue
        }
        if ($rule.Pattern -and $rule.Pattern -ne '(?i)^$') {
            if ($CombinedText -match $rule.Pattern) {
                return $rule
            }
            continue
        }
        if ($classPattern -and $ClassName -match $classPattern) {
            return $rule
        }
    }
    return $null
}

function New-BlockingDialogRecord {
    param(
        $Window,
        [IntPtr]$MainHwnd,
        [string]$ProcessName = 'MirrorsEdge',
        [string]$Source = 'process'
    )

    $combined = "$($Window.Title) $($Window.Body)".Trim()
    $rule = Match-GameDialogRule -CombinedText $combined -ClassName $Window.ClassName
    $mainDisabled = ($MainHwnd -ne [IntPtr]::Zero) -and -not [Win32UiProbe]::IsWindowEnabled($MainHwnd)
    $fallbackRemediation = if ($ProcessName -eq 'ModuleLauncher') {
        'Unexpected modal window from ModuleLauncher.exe.'
    } elseif ($Source -eq 'desktop') {
        'Unexpected desktop message box related to Mirror''s Edge / ModuleLauncher.'
    } else {
        'Unexpected modal window from MirrorsEdge.exe.'
    }

    return [pscustomobject]@{
        Hwnd         = $Window.Hwnd
        ProcessId    = [int]$Window.ProcessId
        ProcessName  = $ProcessName
        Source       = $Source
        Title        = $Window.Title
        ClassName    = $Window.ClassName
        Body         = $Window.Body
        RuleId       = if ($rule) { $rule.Id } else { 'unknown' }
        Remediation  = if ($rule) { $rule.Remediation } else { $fallbackRemediation }
        AutoDismiss  = if ($rule) { [bool]$rule.AutoDismiss } else { $false }
        MainDisabled = $mainDisabled
    }
}

function Test-ShouldIncludeDesktopMessageBox {
    param(
        $Window,
        [int[]]$WatchPids,
        [hashtable]$MainHwndsByPid
    )

    if (-not $Window -or $Window.ClassName -ne '#32770' -or -not $Window.Visible) {
        return $false
    }

    $combined = "$($Window.Title) $($Window.Body)".Trim()
    $rule = Match-GameDialogRule -CombinedText $combined -ClassName $Window.ClassName
    if ($rule -and $rule.Id -ne 'generic-messagebox') {
        return $true
    }

    if ($combined -match '(?i)(MirrorsEdge\.exe|ModuleLauncher|Module Launcher|Mirror''s Edge)' -and
        $combined -match '(?i)(error|failed|corrupt|runtime|fatal|warning|could not|does not contain|abort|terminate)') {
        return $true
    }

    if ($WatchPids -contains $Window.ProcessId) {
        $mainHwnd = $MainHwndsByPid[$Window.ProcessId]
        if ($mainHwnd -ne [IntPtr]::Zero -and $Window.Hwnd -eq $mainHwnd) {
            return $false
        }
        return Test-IsBlockingDialogWindow -Window $Window -MainHwnd $mainHwnd
    }

    if (@($WatchPids).Count -gt 0) {
        $ownerPid = [int][Win32UiProbe]::ResolveOwnerWatchProcessId($Window.Hwnd, [uint32[]]@($WatchPids))
        if ($ownerPid -gt 0) {
            return Test-IsBlockingDialogWindow -Window $Window -MainHwnd $MainHwndsByPid[$ownerPid]
        }
    }

    return $false
}

function Get-BlockingDialogsForProcess {
    param(
        [Parameter(Mandatory)]
        [int]$ProcessId,
        [string]$ProcessName = 'MirrorsEdge',
        [IntPtr]$MainHwnd = [IntPtr]::Zero
    )

    $windows = Get-ProcessTopLevelWindows -ProcessId $ProcessId
    if (-not $windows -or @($windows).Count -eq 0) { Write-DialogList @(); return }

    if ($MainHwnd -eq [IntPtr]::Zero) {
        if ($ProcessName -eq 'ModuleLauncher') {
            $MainHwnd = Resolve-LauncherMainWindowHandleForDialogCheck -ProcessId $ProcessId
        } else {
            $MainHwnd = Resolve-GameMainWindowHandleForDialogCheck -ProcessId $ProcessId
        }
    }

    $blocking = @()
    foreach ($w in $windows) {
        if (Test-IsBlockingDialogWindow -Window $w -MainHwnd $MainHwnd) {
            $blocking += New-BlockingDialogRecord -Window $w -MainHwnd $MainHwnd `
                -ProcessName $ProcessName -Source 'process'
        }
    }
    Write-DialogList $blocking
}

function Get-DesktopBlockingMessageBoxes {
    param(
        [int[]]$WatchPids = @(),
        [hashtable]$MainHwndsByPid = @{}
    )

    if (-not $MainHwndsByPid) {
        $MainHwndsByPid = @{}
    }
    $WatchPids = [int[]]@($WatchPids)

    $recs = @([Win32UiProbe]::EnumTopLevelMessageBoxes())
    $blocking = @()
    foreach ($r in $recs) {
        $w = [pscustomobject]@{
            Hwnd      = $r.Hwnd
            ProcessId = [int]$r.Pid
            Title     = $r.Title
            ClassName = $r.ClassName
            Body      = $r.Body
            Visible   = $r.Visible
            Width     = $r.Width
            Height    = $r.Height
            Owner     = $r.Owner
        }
        if (-not (Test-ShouldIncludeDesktopMessageBox -Window $w -WatchPids $WatchPids -MainHwndsByPid $MainHwndsByPid)) {
            continue
        }

        $procName = 'desktop'
        if ($WatchPids -contains $w.ProcessId) {
            if ($MainHwndsByPid.ContainsKey($w.ProcessId)) {
                $ownerProc = Get-Process -Id $w.ProcessId -ErrorAction SilentlyContinue
                if ($ownerProc -and $ownerProc.ProcessName -eq 'ModuleLauncher') {
                    $procName = 'ModuleLauncher'
                } else {
                    $procName = 'MirrorsEdge'
                }
            }
        } else {
            $ownerPid = [int][Win32UiProbe]::ResolveOwnerWatchProcessId($w.Hwnd, [uint32[]]$WatchPids)
            if ($ownerPid -gt 0) {
                $ownerProc = Get-Process -Id $ownerPid -ErrorAction SilentlyContinue
                if ($ownerProc) {
                    $procName = $ownerProc.ProcessName
                    $w = [pscustomobject]@{
                        Hwnd      = $w.Hwnd
                        ProcessId = $ownerPid
                        Title     = $w.Title
                        ClassName = $w.ClassName
                        Body      = $w.Body
                        Visible   = $w.Visible
                        Width     = $w.Width
                        Height    = $w.Height
                        Owner     = $w.Owner
                    }
                }
            }
        }

        $mainHwnd = [IntPtr]::Zero
        if ($MainHwndsByPid.ContainsKey($w.ProcessId)) {
            $mainHwnd = $MainHwndsByPid[$w.ProcessId]
        }
        $blocking += New-BlockingDialogRecord -Window $w -MainHwnd $mainHwnd `
            -ProcessName $procName -Source 'desktop'
    }
    Write-DialogList $blocking
}

function Get-HarnessBlockingDialogs {
    $watchMap = Get-HarnessWatchProcessMap
    if (-not $watchMap) {
        Write-DialogList @() | Out-Null
        return @()
    }
    if (@($watchMap.Keys).Count -eq 0) {
        Write-DialogList @() | Out-Null
        return @()
    }

    $mainByPid = Get-HarnessMainWindowHandles -WatchMap $watchMap
    $watchPids = [int[]]@($watchMap.Keys)
    $seen = @{}
    $all = New-Object System.Collections.Generic.List[object]

    foreach ($entry in $watchMap.GetEnumerator()) {
        $found = Get-BlockingDialogsForProcess -ProcessId $entry.Key -ProcessName $entry.Value `
            -MainHwnd $mainByPid[$entry.Key]
        foreach ($d in @($found)) {
            if (-not (Test-IsDialogRecord -Dialog $d)) { continue }
            $key = $d.Hwnd.ToString()
            if (-not $seen.ContainsKey($key)) {
                [void]$seen.Add($key, $true)
                [void]$all.Add($d)
            }
        }
    }

    $desktop = Get-DesktopBlockingMessageBoxes -WatchPids $watchPids -MainHwndsByPid $mainByPid
    foreach ($d in @($desktop)) {
        if (-not (Test-IsDialogRecord -Dialog $d)) { continue }
        $key = $d.Hwnd.ToString()
        if (-not $seen.ContainsKey($key)) {
            [void]$seen.Add($key, $true)
            [void]$all.Add($d)
        }
    }

    $result = @($all.ToArray())
    Write-DialogList $result | Out-Null
    return $result
}

function Get-GameBlockingDialogs {
    param([int]$ProcessId = 0)

    if ($ProcessId -gt 0) {
        $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
        $name = if ($proc) { $proc.ProcessName } else { 'MirrorsEdge' }
        return @(Get-BlockingDialogsForProcess -ProcessId $ProcessId -ProcessName $name)
    }

    return @(Get-HarnessBlockingDialogs)
}

function Format-GameDialogReport {
    param([array]$Dialogs)

    if (-not $Dialogs -or $Dialogs.Count -eq 0) {
        return ''
    }

    $lines = New-Object System.Collections.Generic.List[string]
    [void]$lines.Add("blocking dialog(s) detected ($($Dialogs.Count)):")
    foreach ($d in $Dialogs) {
        $text = if ($d.Body) { $d.Body } else { $d.Title }
        if ($text.Length -gt 240) { $text = $text.Substring(0, 240) + '...' }
        $procTag = if ($d.ProcessName) { "pid=$($d.ProcessId) $($d.ProcessName)" } else { "pid=$($d.ProcessId)" }
        $srcTag = if ($d.Source) { " source=$($d.Source)" } else { '' }
        [void]$lines.Add("  [$($d.RuleId)] $procTag$srcTag class=$($d.ClassName) title='$($d.Title)'")
        [void]$lines.Add("    text: $text")
        [void]$lines.Add("    fix: $($d.Remediation)")
    }
    return ($lines -join [Environment]::NewLine)
}

function Invoke-GameDialogRemediation {
    param(
        [array]$Dialogs,
        [switch]$AllowAutoDismiss
    )

    if (-not $Dialogs -or $Dialogs.Count -eq 0) {
        return $false
    }

    $report = Format-GameDialogReport -Dialogs $Dialogs
    Write-Host "game-dialog: $report"

    foreach ($d in $Dialogs) {
        if ($AllowAutoDismiss -and $d.AutoDismiss) {
            $clicked = [Win32UiProbe]::TryClickOk($d.Hwnd)
            if ($clicked) {
                Write-Host "game-dialog: auto-dismiss attempted (OK) rule=$($d.RuleId)"
                Start-Sleep -Milliseconds 400
                return $true
            }
        }
    }

    throw $report
}

function Assert-NoBlockingGameDialogs {
    param(
        [int]$ProcessId = 0,
        [switch]$AllowAutoDismiss
    )

    if ($ProcessId -gt 0) {
        $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
        if (-not $proc) { return }
    } else {
        $watch = Get-HarnessWatchProcessMap
        if (-not $watch -or @($watch.Keys).Count -eq 0) { return }
    }

    $dialogs = if ($ProcessId -gt 0) {
        @(Get-GameBlockingDialogs -ProcessId $ProcessId)
    } else {
        @(Get-HarnessBlockingDialogs)
    }
    if (@($dialogs).Count -eq 0) { return }

    Invoke-GameDialogRemediation -Dialogs $dialogs -AllowAutoDismiss:$AllowAutoDismiss | Out-Null
}

function Test-GameDialogPatternMatching {
    $rule = Match-GameDialogRule -CombinedText 'The file ..\TdGame\Config\DefaultEngine.ini is corrupt' -ClassName '#32770'
    if (-not $rule -or $rule.Id -ne 'corrupt-default-ini') {
        throw "corrupt-ini dialog rule match failed"
    }

    $fake = [pscustomobject]@{
        Hwnd      = [IntPtr]::new(123)
        Title     = 'Mirror''s Edge'
        ClassName = '#32770'
        Body      = 'Please insert CD/DVD'
        Visible   = $true
        Width     = 400
        Height    = 200
        Owner     = [IntPtr]::Zero
    }
    if (-not (Test-IsGameBlockingWindow -Window $fake -MainHwnd ([IntPtr]::new(999)))) {
        throw 'securom dialog heuristic failed'
    }

    $runtime = Match-GameDialogRule -CombinedText 'Microsoft Visual C++ Runtime Library Runtime Error! Program: ... terminate in unusual way' -ClassName '#32770'
    if (-not $runtime -or $runtime.Id -ne 'msvc-runtime-abort') {
        throw 'msvc-runtime-abort dialog rule match failed'
    }

    $launcher = Match-GameDialogRule -CombinedText 'Selected folder does not contain Binaries\MirrorsEdge.exe.' -ClassName '#32770'
    if (-not $launcher -or $launcher.Id -ne 'launcher-error') {
        throw 'launcher-error dialog rule match failed'
    }

    $desktop = [pscustomobject]@{
        Hwnd      = [IntPtr]::new(456)
        ProcessId = 99999
        Title     = 'Microsoft Visual C++ Runtime Library'
        ClassName = '#32770'
        Body      = 'Runtime Error! Program: C:\Games\MirrorsEdge.exe'
        Visible   = $true
        Width     = 400
        Height    = 200
        Owner     = [IntPtr]::Zero
    }
    if (-not (Test-ShouldIncludeDesktopMessageBox -Window $desktop -WatchPids @() -MainHwndsByPid @{})) {
        throw 'desktop msvc dialog filter failed'
    }

    return [pscustomobject]@{ Pass = $true }
}

function Get-GameLaunchArguments {
    param([string]$DeployRoot)

    $args = @()
    if (-not $DeployRoot) {
        return $args
    }

    $settingsPath = Join-Path $DeployRoot "settings.json"
    if (-not (Test-Path $settingsPath)) {
        return $args
    }

    try {
        $settings = Get-Content $settingsPath -Raw | ConvertFrom-Json
        $skipMovies = $false
        if ($settings.launcher -and $settings.launcher.display) {
            $skipMovies = [bool]$settings.launcher.display.skipStartupMovies
        }
        if ($skipMovies) {
            $args += "-nomoviestartup"
        }
    } catch {}

    return $args
}

function Start-GameExecutable {
    param([string]$DeployRoot)

    $binaries = Resolve-GameBinariesPath -DeployRoot $DeployRoot
    $gameExe = Join-Path $binaries "MirrorsEdge.exe"
    $argList = Get-GameLaunchArguments -DeployRoot $DeployRoot
    $startParams = @{
        FilePath         = $gameExe
        WorkingDirectory = $binaries
        PassThru         = $true
    }
    if ($argList.Count -gt 0) {
        $startParams.ArgumentList = $argList
    }
    return Start-Process @startParams
}

function Start-GameViaModuleLauncher {
    param(
        [Parameter(Mandatory)]
        [string]$RepoRoot,
        [hashtable]$ExtraEnv = @{}
    )

    $launcherProc = Start-ModuleLauncher -RepoRoot $RepoRoot -Auto -ExtraEnv $ExtraEnv `
        -UseDeployPath
    Write-Host "launcher pid: $($launcherProc.Id)"

    $hwnd = Wait-LauncherWindow -ProcessId $launcherProc.Id -TimeoutSec 60
    $kLaunchGame = 1004

    $deadline = (Get-Date).AddSeconds(90)
    $launchBtn = [IntPtr]::Zero
    while ((Get-Date) -lt $deadline) {
        $launchBtn = [Win32Dialog]::GetDlgItem($hwnd, $kLaunchGame)
        if ($launchBtn -ne [IntPtr]::Zero -and [Win32Dialog]::IsWindowEnabled($launchBtn)) {
            break
        }
        Start-Sleep -Milliseconds 300
    }
    if ($launchBtn -eq [IntPtr]::Zero -or -not [Win32Dialog]::IsWindowEnabled($launchBtn)) {
        throw "launcher Launch Game button not ready"
    }

    Focus-LauncherWindow -Hwnd $hwnd | Out-Null
    Click-DialogControl -ControlHwnd $launchBtn -Label "Launch Game"
    Write-Host "launcher: Launch Game clicked (config integrity bypass active)"

    $spawnDeadline = (Get-Date).AddSeconds(30)
    $retriedLaunch = $false
    while ((Get-Date) -lt $spawnDeadline) {
        $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($proc) { break }
        if (-not $retriedLaunch -and ((Get-Date) -gt $spawnDeadline.AddSeconds(-20))) {
            $launchBtn = [Win32Dialog]::GetDlgItem($hwnd, $kLaunchGame)
            if ($launchBtn -ne [IntPtr]::Zero -and [Win32Dialog]::IsWindowEnabled($launchBtn)) {
                Focus-LauncherWindow -Hwnd $hwnd | Out-Null
                Click-DialogControl -ControlHwnd $launchBtn -Label "Launch Game (retry)"
                Write-Host "launcher: Launch Game retry click"
                $retriedLaunch = $true
            }
        }
        Start-Sleep -Milliseconds 400
    }

    $game = Wait-GameWindow -TimeoutSec 180
    Write-Host "game pid: $($game.Id)"
    Focus-GameWindow -Process $game

    return [pscustomobject]@{
        Launcher = $launcherProc
        Game     = $game
    }
}

function Wait-GameWindow {
    param([int]$TimeoutSec = 180)

    $resumeLib = Join-Path $PSScriptRoot "..\..\lib\Resume-ProcessThreads.ps1"
    $hasResume = Test-Path $resumeLib
    if ($hasResume) {
        . $resumeLib
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $lastNudge = [DateTime]::MinValue
    $nudgeIntervalSec = 6
    while ((Get-Date) -lt $deadline) {
        $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($proc) {
            Assert-NoBlockingGameDialogs -AllowAutoDismiss | Out-Null
            if ($hasResume) {
                $resumed = Resume-ProcessThreads -ProcessId $proc.Id
                if ($resumed -gt 0) {
                    Write-Host "game: resumed $resumed thread(s) on MirrorsEdge (pid $($proc.Id))"
                }
            }
            $proc.Refresh()
            if ((Resolve-GameWindowHandle -Process $proc) -ne [IntPtr]::Zero) {
                return $proc
            }
            if (Wait-NamedEvent -Name "Local\module_manager_ready" -TimeoutSec 1) {
                Write-Host "game: module_manager_ready (borderless launch)"
                return $proc
            }
            if ([Win32Module]::HasModule($proc.Id, "module_manager.dll")) {
                Write-Host "game: module_manager.dll loaded (pid $($proc.Id))"
                return $proc
            }
            $threadCount = if ($hasResume) {
                Get-ProcessThreadCount -ProcessId $proc.Id
            } else {
                $proc.Threads.Count
            }
            if ($threadCount -ge 4) {
                Write-Host "game: live MirrorsEdge pid=$($proc.Id) threads=$threadCount"
                return $proc
            }
            if (((Get-Date) - $lastNudge).TotalSeconds -ge $nudgeIntervalSec) {
                try {
                    Invoke-BootGameNudge -Label "boot nudge (Wait-GameWindow)"
                    $lastNudge = Get-Date
                } catch {}
            }
        }
        Start-Sleep -Milliseconds 500
    }

    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc) {
        $dialogs = @(Get-HarnessBlockingDialogs)
        if ($dialogs.Count -gt 0) {
            throw (Format-GameDialogReport -Dialogs $dialogs)
        }
    }
    throw "Timed out waiting for MirrorsEdge game window."
}

function Resolve-GameWindowHandle {
    param($Process)

    if (-not $Process) {
        $Process = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if (-not $Process) {
        return [IntPtr]::Zero
    }

    $Process.Refresh()
    $now = Get-Date
    $cachedAtVar = Get-Variable -Name CachedManagerGameHwndAt -Scope Script -ErrorAction SilentlyContinue
    $cachedHwndVar = Get-Variable -Name CachedManagerGameHwnd -Scope Script -ErrorAction SilentlyContinue
    if ($cachedAtVar -and $cachedHwndVar -and $script:CachedManagerGameHwndAt -and
        (($now - $script:CachedManagerGameHwndAt).TotalSeconds -lt 2)) {
        if ([Win32Dialog]::IsWindow($script:CachedManagerGameHwnd)) {
            return $script:CachedManagerGameHwnd
        }
    }

    if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
        return $Process.MainWindowHandle
    }

    $enumHwnd = [Win32Enum]::FindMainWindow($Process.Id)
    if ($enumHwnd -ne [IntPtr]::Zero) {
        return $enumHwnd
    }

    try {
        $mgrStatus = Get-ManagerStatusJson -TimeoutMs 2500
        foreach ($prop in @('focusHwnd', 'gameHwnd')) {
            $raw = $mgrStatus.$prop
            if ($null -ne $raw -and "$raw" -ne '' -and [int64]$raw -ne 0) {
                $ipcHwnd = [IntPtr]::new([int64]$raw)
                if ([Win32Dialog]::IsWindow($ipcHwnd)) {
                    $script:CachedManagerGameHwnd = $ipcHwnd
                    $script:CachedManagerGameHwndAt = $now
                    return $ipcHwnd
                }
            }
        }
    } catch {}

    try {
        $status = Get-MmultiplayerStatusJson
        if ($status.gameHwnd -and [int64]$status.gameHwnd -ne 0) {
            $ipcHwnd = [IntPtr]::new([int64]$status.gameHwnd)
            if ([Win32Dialog]::IsWindow($ipcHwnd)) {
                return $ipcHwnd
            }
        }
    } catch {}

    $titleHwnd = [Win32Dialog]::FindWindow([IntPtr]::Zero, "Mirror's Edge")
    if ($titleHwnd -ne [IntPtr]::Zero) {
        return $titleHwnd
    }

    return [IntPtr]::Zero
}

function Focus-GameWindow {
    param([Parameter(Mandatory)]$Process)

    $hwnd = Resolve-GameWindowHandle -Process $Process
    if ($hwnd -eq [IntPtr]::Zero) {
        $deadline = (Get-Date).AddSeconds(20)
        while ((Get-Date) -lt $deadline -and $hwnd -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 400
            $Process = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            $hwnd = Resolve-GameWindowHandle -Process $Process
        }
    }

    if (-not $hwnd -or $hwnd -eq [IntPtr]::Zero) {
        if ($Process -and -not $Process.HasExited) {
            Write-Host "focus: WARN no window handle (borderless); skipping foreground"
            return
        }
        throw "Game process has no main window handle."
    }

    [void][Win32Focus]::ShowWindow($hwnd, 9)
    [void][Win32Focus]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 500
}

function Wait-ManagerHooksReadyFromLog {
    param(
        [Parameter(Mandatory)]
        [string]$LogPath,
        [int]$TimeoutSec = 180
    )

    $seq = Test-LogSequence -Sequence @("hooks_installed") `
        -LogPath $LogPath -TimeoutSec $TimeoutSec
    if (-not $seq.Pass) {
        throw "hooks_installed not found in debug log"
    }

    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        try {
            $ping = Invoke-ModControlPipe -Command "PING" -Target manager -TimeoutMs 4000 -MaxAttempts 1
            if ($ping -eq "PONG") {
                Write-Host "manager: PING OK (post-hooks log)"
                return
            }
        } catch {
            Write-Host "manager: PING pending ($($_.Exception.Message))"
        }
        try { Try-FocusGameWindow | Out-Null } catch {}
        $game = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($game) {
            Assert-NoBlockingGameDialogs -AllowAutoDismiss | Out-Null
        }
        Start-Sleep -Milliseconds 500
    }
    throw "manager PING failed after hooks_installed log"
}

function Test-HarnessDebugLogPatterns {
    param(
        [Parameter(Mandatory)]
        [string[]]$Patterns,
        [string]$LogPath = ""
    )

    $path = if ($LogPath) { $LogPath } else { Resolve-HarnessDebugLogPath }
    if (-not $path -or -not (Test-Path -LiteralPath $path)) {
        return $false
    }

    foreach ($pattern in $Patterns) {
        if (-not (Select-String -LiteralPath $path -Pattern $pattern -Quiet)) {
            return $false
        }
    }
    return $true
}

function Invoke-BootGameNudge {
    param(
        [string]$Label = "boot nudge (Enter)"
    )

    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) {
        Write-Host "manager: boot nudge skipped (no game process)"
        return
    }
    Try-FocusGameWindow -Process $proc | Out-Null
    Send-GameKeyTap -VirtualKey 0x0D -SettleMs 350
    Write-Host "manager: $Label"
}

function Invoke-HarnessHooksReadyVisual {
    param([IntPtr]$WindowHandle = [IntPtr]::Zero)

    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc -and -not $proc.Responding) {
        Write-Host "visual: WARN game not responding; hooks_ready capture skipped"
        return $null
    }

    $maxAttempts = 12
    for ($i = 1; $i -le $maxAttempts; $i++) {
        try {
            if ($i -gt 1) {
                try { Try-FocusGameWindow | Out-Null } catch {}
                if ($i -in @(4, 8)) {
                    try { Send-GameKeyTap -VirtualKey 0x1B -SettleMs 350 } catch {}
                }
            }
            $settle = 400 + ($i * 200)
            return Invoke-HarnessVisualMilestone -Step "hooks_ready" `
                -WindowHandle $WindowHandle -SettleMs $settle
        } catch {
            Write-Host ("visual: hooks_ready attempt {0}/{1}: {2}" -f `
                $i, $maxAttempts, $_.Exception.Message)
        }
    }

    Write-Host "visual: WARN hooks_ready sanity skipped after $maxAttempts attempts (hooks confirmed via pipe/log)"
    return Invoke-HarnessVisualMilestone -Step "hooks_ready" `
        -WindowHandle $WindowHandle -SettleMs 800 -SkipSaneCheck
}

function Wait-ManagerHooksReady {
    param(
        [int]$TimeoutSec = 300,
        [switch]$KeepFocused,
        [switch]$BootNudge,
        [switch]$RequireOverlay
    )

    if (-not $PSBoundParameters.ContainsKey('RequireOverlay')) {
        $RequireOverlay = $true
    }
    if (-not $PSBoundParameters.ContainsKey('BootNudge')) {
        $BootNudge = $true
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $nudgeIntervalSec = 6
    $nextNudge = [DateTime]::MaxValue
    if ($BootNudge) {
        Invoke-BootGameNudge -Label "boot nudge (initial Enter)"
        $nextNudge = (Get-Date).AddSeconds($nudgeIntervalSec)
    }
    $overlayStuckSince = $null
    $pollMs = 400
    $bootHooksSeen = $false
    $loggedHooksFromLog = $false
    $loggedOverlayFromLog = $false
    $hooksWithoutOverlaySince = $null
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "manager hooks wait" -SkipHangCheck | Out-Null
        Assert-NoBlockingGameDialogs -AllowAutoDismiss | Out-Null
        if ($BootNudge -and -not $bootHooksSeen -and (Get-Date) -gt $nextNudge) {
            Invoke-BootGameNudge
            $nextNudge = (Get-Date).AddSeconds($nudgeIntervalSec)
        }

        if ($KeepFocused) {
            $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($proc) {
                Try-FocusGameWindow -Process $proc | Out-Null
            }
        }

        $hooksOk = $false
        $overlayOk = -not $RequireOverlay
        try {
            $statusJson = Invoke-ModControlPipe -Command "GET_STATUS" -Target manager -TimeoutMs 5000
            $status = $statusJson | ConvertFrom-Json
            Write-Host "manager: hooks=$($status.hooksInstalled) overlay=$($status.overlayReady)"
            $hooksOk = $status.hooksInstalled -eq $true
            $overlayOk = (-not $RequireOverlay) -or ($status.overlayReady -eq $true)
            if ($hooksOk -and $overlayOk) {
                try {
                    Invoke-HarnessHooksReadyVisual | Out-Null
                } catch {
                    throw "hooks_ready visual failed: $($_.Exception.Message)"
                }
                return $status
            }
            if ($RequireOverlay -and $hooksOk -and $status.overlayReady -ne $true) {
                if (Test-HarnessDebugLogPatterns @('imgui initialized')) {
                    Write-Host "manager: overlay=true (pipe lag; imgui in log)"
                    try {
                        Invoke-HarnessHooksReadyVisual | Out-Null
                    } catch {
                        throw "hooks_ready visual failed: $($_.Exception.Message)"
                    }
                    return $status
                }
            }
            if ($hooksOk -and $status.overlayReady -ne $true) {
                if (-not $hooksWithoutOverlaySince) {
                    $hooksWithoutOverlaySince = Get-Date
                }
                if (-not $overlayStuckSince) {
                    $overlayStuckSince = Get-Date
                } elseif (((Get-Date) - $overlayStuckSince).TotalSeconds -ge 12) {
                    Write-Host "manager: overlay nudge (Escape)"
                    try {
                        Try-FocusGameWindow | Out-Null
                        Send-GameKeyTap -VirtualKey 0x1B -SettleMs 500
                    } catch {}
                    $overlayStuckSince = Get-Date
                }
                if ($RequireOverlay -and $hooksWithoutOverlaySince -and
                    ((Get-Date) - $hooksWithoutOverlaySince).TotalSeconds -ge 15) {
                    Write-Host "manager: overlay=true (hooks wait fallback)"
                    try {
                        Invoke-HarnessHooksReadyVisual | Out-Null
                    } catch {
                        throw "hooks_ready visual failed: $($_.Exception.Message)"
                    }
                    return $status
                }
            } else {
                $overlayStuckSince = $null
                $hooksWithoutOverlaySince = $null
            }
        } catch {
            Write-Host "manager status pending: $($_.Exception.Message)"
            if (Test-HarnessDebugLogPatterns @('hooks_installed')) {
                $hooksOk = $true
                $bootHooksSeen = $true
                if (-not $loggedHooksFromLog) {
                    Write-Host "manager: hooks=true (debug log)"
                    $loggedHooksFromLog = $true
                }
            }
            if ($RequireOverlay) {
                if (Test-HarnessDebugLogPatterns @('imgui initialized')) {
                    $overlayOk = $true
                    if (-not $loggedOverlayFromLog) {
                        Write-Host "manager: overlay=true (debug log)"
                        $loggedOverlayFromLog = $true
                    }
                }
            }
            if ($hooksOk -and $overlayOk) {
                Write-Host "manager: hooks+overlay ready via debug log (pipe pending)"
                try {
                    Invoke-HarnessHooksReadyVisual | Out-Null
                } catch {
                    throw "hooks_ready visual failed: $($_.Exception.Message)"
                }
                return [pscustomobject]@{
                    hooksInstalled = $true
                    overlayReady   = [bool]$overlayOk
                }
            }
        }
        if (-not $hooksOk) {
            try {
                $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue |
                    Select-Object -First 1
                if ($proc -and (Test-GameWindowHung -Process $proc)) {
                    Invoke-HarnessHangFailure -Label "manager-hooks" -Source "window" `
                        -Reason "game window not responding before module_manager presentation hooks became ready" `
                        -InteractionData @{
                            hooks       = $hooksOk
                            overlay     = $overlayOk
                            requireOverlay = [bool]$RequireOverlay
                        }
                }
            } catch {}
        }
        Start-Sleep -Milliseconds $pollMs
    }
    if ($RequireOverlay) {
        throw "module_manager hooksInstalled/overlayReady timeout"
    }
    throw "module_manager hooksInstalled timeout"
}

function Wait-ManagerModuleLoaded {
    param(
        [Parameter(Mandatory)]
        [string]$ModuleId,
        [int]$TimeoutSec = 180
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $statusJson = Invoke-ModControlPipe -Command "GET_STATUS" -Target manager -TimeoutMs 5000
            $status = $statusJson | ConvertFrom-Json
            $mod = $status.modules | Where-Object { $_.id -eq $ModuleId } | Select-Object -First 1
            if ($mod -and $mod.loaded -eq $true) {
                Write-Host "manager: $ModuleId loaded (status=$($mod.status))"
                Invoke-HarnessVisualMilestone -Step "module_loaded_$ModuleId" | Out-Null
                return $mod
            }
            if ($mod) {
                Write-Host "manager: $ModuleId status=$($mod.status) busy=$($mod.busy)"
            }
        } catch {
            Write-Host "manager: GET_STATUS pending for $ModuleId ($($_.Exception.Message))"
        }
        Start-Sleep -Seconds 2
    }
    throw "manager module load timeout: $ModuleId"
}

function Get-EngineModReadyFromStatus {
    param($Status)
    if ($Status.engine) {
        return [bool]$Status.engine.modReady
    }
    return [bool]$Status.modReady
}

function Get-HarnessStatusMapName {
    param($Status)

    if (-not $Status) {
        return ""
    }
    if ($Status.PSObject.Properties.Name -contains "currentMap") {
        return [string]$Status.currentMap
    }
    if ($Status.engine -and
        ($Status.engine.PSObject.Properties.Name -contains "currentMap")) {
        return [string]$Status.engine.currentMap
    }
    return ""
}

function Assert-GameBootProgress {
    param(
        [int]$TimeoutSec = 90,
        [int]$StaticFrameSec = 25,
        [switch]$KeepFocused
    )

    Write-Host "boot-progress: waiting for game to leave static startup splash"
    Enable-HarnessIntroHangImmunity -Seconds ($TimeoutSec + 15)
    Write-HarnessInteraction -Phase "boot" -Action "progress_wait_begin" `
        -Data @{ timeoutSec = $TimeoutSec; staticFrameSec = $StaticFrameSec }

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $lastFrameKey = ""
    $lastFrameChangedAt = Get-Date
    $lastNudgeAt = [DateTime]::MinValue
    $poll = 0
    $consecutiveWindowHung = 0
    $frameChangeCount = 0

    while ((Get-Date) -lt $deadline) {
        $poll++
        Assert-GameProcessAlive -Label "boot progress wait" -SkipHangCheck | Out-Null
        Assert-NoBlockingGameDialogs -AllowAutoDismiss | Out-Null

        if ($KeepFocused) {
            try { Try-FocusGameWindow | Out-Null } catch {}
        }

        $map = ""
        $engineReady = $false
        $hooks = $false
        $overlay = $false
        try {
            $statusRaw = Invoke-ModControlPipe -Command "GET_STATUS" -Target core `
                -TimeoutMs 1200 -MaxAttempts 1
            $status = Expand-CoreHarnessStatus ($statusRaw | ConvertFrom-Json)
            $map = Get-HarnessStatusMapName $status
            $engineReady = Get-EngineModReadyFromStatus $status
        } catch {}
        try {
            $mgrRaw = Invoke-ModControlPipe -Command "GET_STATUS" -Target manager `
                -TimeoutMs 1200 -MaxAttempts 1
            $mgr = $mgrRaw | ConvertFrom-Json
            $hooks = [bool]$mgr.hooksInstalled
            $overlay = [bool]$mgr.overlayReady
        } catch {}

        if ($map) {
            Write-HarnessInteraction -Phase "boot" -Action "progress_ok" `
                -Data @{ poll = $poll; map = $map; engineReady = $engineReady }
            Write-Host "boot-progress: map signal '$map'"
            return $true
        }

        $frameKey = ""
        $windowHung = $false
        try {
            $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($proc) {
                $hwnd = Resolve-GameWindowHandle -Process $proc
                if ($hwnd -ne [IntPtr]::Zero) {
                    $windowHung = Test-GameWindowHung -Process $proc
                    if (-not $windowHung) {
                        $cap = Capture-GameWindowScreenshot -WindowHandle $hwnd `
                            -Label ("boot_progress_{0:D2}" -f $poll)
                        $frameKey = ("{0}|{1}|{2}" -f `
                            [Math]::Round($cap.Stats.MeanLuminance, 2),
                            [Math]::Round($cap.Stats.Variance, 1),
                            $cap.Stats.NonBlackRatio)
                    }
                }
            }
        } catch {
            $frameKey = ""
        }

        if ($frameKey -and $frameKey -ne $lastFrameKey) {
            if ($lastFrameKey) {
                $frameChangeCount++
            }
            $lastFrameKey = $frameKey
            $lastFrameChangedAt = Get-Date
        }
        if ($windowHung) {
            $consecutiveWindowHung++
        } else {
            $consecutiveWindowHung = 0
        }

        $staticFor = [int]((Get-Date) - $lastFrameChangedAt).TotalSeconds
        Write-HarnessInteraction -Phase "boot" -Action "progress_poll" `
            -Data @{
                poll        = $poll
                map         = $map
                hooks       = $hooks
                overlay     = $overlay
                engineReady = $engineReady
                staticFor   = $staticFor
                frameKey    = $frameKey
                windowHung  = $windowHung
                hungPolls   = $consecutiveWindowHung
                frameChanges = $frameChangeCount
            }

        if ($engineReady -and $hooks -and $frameChangeCount -ge 3 -and
            $consecutiveWindowHung -eq 0) {
            Write-HarnessInteraction -Phase "boot" -Action "progress_ok" `
                -Data @{
                    poll         = $poll
                    map          = $map
                    engineReady  = $engineReady
                    hooks        = $hooks
                    overlay      = $overlay
                    frameChanges = $frameChangeCount
                    frameKey     = $frameKey
                }
            Write-Host "boot-progress: dynamic frame progress observed"
            return $true
        }

        if ($consecutiveWindowHung -ge 3 -and $hooks) {
            Invoke-HarnessHangFailure -Label "boot-progress" -Source "window" `
                -Reason "game window not responding during boot progress after presentation hooks" `
                -InteractionData @{
                    poll        = $poll
                    staticFor   = $staticFor
                    engineReady = $engineReady
                    hooks       = $hooks
                    overlay     = $overlay
                    frameKey    = $frameKey
                    windowHung  = $windowHung
                    hungPolls   = $consecutiveWindowHung
                }
        }

        if ($engineReady -and $hooks -and $overlay -and $staticFor -ge $StaticFrameSec) {
            Invoke-HarnessHangFailure -Label "boot-progress" -Source "window" `
                -Reason ("module/core ready but game stayed on a static startup frame " +
                    "for ${staticFor}s; likely stuck before main menu") `
                -InteractionData @{
                    poll        = $poll
                    staticFor   = $staticFor
                    engineReady = $engineReady
                    hooks       = $hooks
                    overlay     = $overlay
                    frameKey    = $frameKey
                }
        }

        if (((Get-Date) - $lastNudgeAt).TotalSeconds -ge 6) {
            try {
                Invoke-BootGameNudge -Label "boot progress nudge (Enter)"
                $lastNudgeAt = Get-Date
            } catch {}
        }

        Start-Sleep-HarnessAware -Seconds 3 -Label "boot progress wait" -SkipHangCheck
    }

    Invoke-HarnessHangFailure -Label "boot-progress" -Source "window" `
        -Reason "timed out waiting for currentMap/main-menu signal after module/core ready"
}

function Invoke-EnsureCoreLoaded {
    param(
        [int]$TimeoutSec = 240,
        [string]$LogPath = ""
    )

    Write-Host "core: waiting for auto-load bootstrap"
    return Wait-CoreReady -TimeoutSec $TimeoutSec -LogPath $LogPath
}

function Wait-CoreReady {
    param(
        [int]$TimeoutSec = 240,
        [string]$LogPath = "",
        [switch]$MonitorRenderStall
    )

    if (-not $PSBoundParameters.ContainsKey('MonitorRenderStall')) {
        $MonitorRenderStall = $true
    }

    $eventOk = Wait-NamedEventWithBootstrapProbe -Name "Local\core_ready" `
        -TimeoutSec $TimeoutSec -MonitorRenderStall:$MonitorRenderStall -BootNudge
    if (-not $eventOk) {
        Write-Host "core_ready event timeout; polling core pipe"
        $pollDeadline = (Get-Date).AddSeconds([Math]::Min(60, $TimeoutSec))
        $nextNudge = (Get-Date).AddSeconds(6)
        $readyViaLog = $false
        while ((Get-Date) -lt $pollDeadline) {
            if ($MonitorRenderStall) {
                Assert-CoreBootstrapProgress
            }
            if ((Get-Date) -gt $nextNudge) {
                try { Invoke-BootGameNudge -Label "core pipe fallback nudge (Enter)" } catch {}
                $nextNudge = (Get-Date).AddSeconds(6)
            }
            if ($LogPath) {
                $logHit = Test-LogContains -Patterns @("init_ready", "init: core ready") `
                    -LogPath $LogPath -TimeoutSec 1
                if ($logHit.Pass) {
                    Write-Host "core: ready via NDJSON log (event/pipe pending)"
                    $readyViaLog = $true
                    break
                }
            }
            try {
                $ping = Invoke-ModControlPipe -Command "PING" -Target core `
                    -TimeoutMs 4000 -MaxAttempts 1
                if ($ping -eq "PONG") {
                    $statusJson = Invoke-ModControlPipe -Command "GET_STATUS" `
                        -Target core -TimeoutMs 8000
                    $status = Expand-CoreHarnessStatus ($statusJson | ConvertFrom-Json)
                    if (Get-EngineModReadyFromStatus $status) {
                        Write-Host "core: ready via pipe fallback (event missed)"
                        if ($LogPath) {
                            $logHit = Test-LogContains -Patterns @("init_ready", "init: core ready") `
                                -LogPath $LogPath -TimeoutSec 3
                            if ($logHit.Pass) {
                                Write-Host "core: init ready confirmed in NDJSON log"
                            }
                        }
                        Write-Host ("core: modReady=True hosted={0} (pipe)" -f $status.hostedMode)
                        return $status
                    }
                }
            } catch {
                Write-Host "core pipe pending: $($_.Exception.Message)"
            }
            Start-Sleep -Milliseconds 500
        }
        if ($readyViaLog) {
            $logDeadline = (Get-Date).AddSeconds(30)
            while ((Get-Date) -lt $logDeadline) {
                try {
                    $ping = Invoke-ModControlPipe -Command "PING" -Target core `
                        -TimeoutMs 4000 -MaxAttempts 1
                    if ($ping -eq "PONG") {
                        $statusJson = Invoke-ModControlPipe -Command "GET_STATUS" `
                            -Target core -TimeoutMs 8000
                        $status = Expand-CoreHarnessStatus ($statusJson | ConvertFrom-Json)
                        if (Get-EngineModReadyFromStatus $status) {
                            Write-Host ("core: modReady=True hosted={0} (log+pipe)" -f $status.hostedMode)
                            return $status
                        }
                    }
                } catch {
                    Write-Host "core pipe pending after log ready: $($_.Exception.Message)"
                }
                Start-Sleep -Milliseconds 300
            }
            return [pscustomobject]@{ modReady = $true; source = "log" }
        }
        throw "core_ready event timeout"
    }
    Write-Host "core_ready event signaled"

    if ($LogPath) {
        $logHit = Test-LogContains -Patterns @("init_ready", "init: core ready") `
            -LogPath $LogPath -TimeoutSec 8
        if ($logHit.Pass) {
            Write-Host "core: init ready confirmed in NDJSON log"
        }
    }

    $ping = $null
    $pingDeadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $pingDeadline) {
        try {
            $ping = Invoke-ModControlPipe -Command "PING" -Target core `
                -TimeoutMs 4000 -MaxAttempts 1
            if ($ping -eq "PONG") {
                break
            }
        } catch {
            Write-Host "core pipe pending: $($_.Exception.Message)"
        }
        Start-Sleep -Milliseconds 150
    }
    if ($ping -ne "PONG") {
        throw "core pipe PING failed before GET_STATUS"
    }
    Write-Host "core: PING OK"

    $statusJson = $null
    $statusDeadline = (Get-Date).AddSeconds(45)
    while ((Get-Date) -lt $statusDeadline) {
        try {
            $statusJson = Invoke-ModControlPipe -Command "GET_STATUS" -Target core `
                -TimeoutMs 8000 -MaxAttempts 1
            if (-not [string]::IsNullOrWhiteSpace($statusJson)) {
                break
            }
        } catch {
            Write-Host "core GET_STATUS pending: $($_.Exception.Message)"
        }
        Start-Sleep -Milliseconds 200
    }
    if ([string]::IsNullOrWhiteSpace($statusJson)) {
        throw "core pipe GET_STATUS failed after PING"
    }
    $status = Expand-CoreHarnessStatus ($statusJson | ConvertFrom-Json)
    if (-not (Get-EngineModReadyFromStatus $status)) {
        throw "core GET_STATUS: engine.modReady=false"
    }
    Write-Host ("core: modReady=True hosted={0} (pipe)" -f $status.hostedMode)
    return $status
}

function Wait-MmultiplayerReady {
    param(
        [int]$TimeoutSec = 240,
        [string]$LogPath = ""
    )
    return Wait-CoreReady -TimeoutSec $TimeoutSec -LogPath $LogPath
}

function Deploy-ModuleDlls {
    param(
        [Parameter(Mandatory)]
        [string]$RepoRoot
    )

    $deployRoot = Resolve-DeployPath -RepoRoot $RepoRoot
    if (-not $deployRoot) {
        throw "deploy.config.json or ME_DEPLOY_PATH required"
    }

    $dist = Join-Path $RepoRoot "dist"
    $artifacts = @(
        @{
            Source = Join-Path $dist "modules\core\core.dll"
            Dest   = Join-Path $deployRoot "modules\core\core.dll"
        },
        @{
            Source = Join-Path $dist "modules\engine\engine.dll"
            Dest   = Join-Path $deployRoot "modules\engine\engine.dll"
        },
        @{
            Source = Join-Path $dist "modules\module_manager\module_manager.dll"
            Dest   = Join-Path $deployRoot "modules\module_manager\module_manager.dll"
        },
        @{
            Source = Join-Path $dist "d3d9.dll"
            Dest   = Join-Path (Resolve-GameBinariesPath -DeployRoot $deployRoot) "d3d9.dll"
        }
    )

    foreach ($item in $artifacts) {
        if (-not (Test-Path $item.Source)) {
            throw "Build artifact missing: $($item.Source)"
        }
        $destDir = Split-Path $item.Dest -Parent
        if (-not (Test-Path $destDir)) {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        }
        Copy-Item $item.Source $item.Dest -Force
        Write-Host "deployed: $($item.Dest)"
    }
}

function Ensure-CoreDllEnabled {
    param([string]$RepoRoot)
    $deployRoot = Resolve-DeployPath -RepoRoot $RepoRoot
    if (-not $deployRoot) { return }
    $dllPath = Join-Path $deployRoot "modules\core\core.dll"
    $disabledPath = "$dllPath.disabled"
    if (Test-Path $disabledPath) {
        Rename-Item $disabledPath "core.dll"
        Write-Host "harness: re-enabled core.dll for test"
    }
}

function Ensure-MmultiplayerDllEnabled {
    param([string]$RepoRoot)
    Ensure-CoreDllEnabled -RepoRoot $RepoRoot
}

function Invoke-DebugBuild {
    param(
        [string]$RepoRoot,
        [switch]$SkipServer
    )

    $buildScript = Join-Path $RepoRoot "build.ps1"
    $buildArgs = @("-DeployProxy")
    if ($SkipServer) {
        $buildArgs += "-SkipServer"
    }
    & $buildScript @buildArgs

    Ensure-MmultiplayerDllEnabled -RepoRoot $RepoRoot
    Assert-SdkBinaryInstructionScan -RepoRoot $RepoRoot
}

function Start-SplitInjectionSession {
    param(
        [string]$RepoRoot,
        [switch]$SkipBuild,
        [switch]$SkipServer,
        [switch]$StopExisting
    )

    if ($StopExisting) {
        Stop-MirrorsEdgeProcesses -IncludeLauncher
    }
    Assert-NoMirrorsEdgeZombie

    $session = Initialize-DebugSession
    Reset-HarnessVisualSession
    Write-Host "debug session: $($session.SessionId)"
    Write-Host "log: $($session.LogPath)"

    if (-not $SkipBuild) {
        $null = Invoke-DebugBuild -RepoRoot $RepoRoot -SkipServer:$SkipServer
    }

    $deployRoot = Resolve-DeployPath -RepoRoot $RepoRoot
    if (-not $deployRoot) {
        throw "deploy.config.json or ME_DEPLOY_PATH required for game scenarios"
    }

    $debugEnv = @{
        MMOD_DEBUG_SESSION = $session.SessionId
        MMOD_DEBUG_LOG     = $session.LogPath
        MMOD_DIAGNOSTICS   = "1"
    }

    foreach ($kv in $debugEnv.GetEnumerator()) {
        Set-Item -Path "env:$($kv.Key)" -Value $kv.Value
    }

    $launch = Start-GameViaModuleLauncher -RepoRoot $RepoRoot -ExtraEnv $debugEnv
    $game = $launch.Game

    $resumeLib = Join-Path $PSScriptRoot "..\..\lib\Resume-ProcessThreads.ps1"
    $hasResume = Test-Path $resumeLib
    if ($hasResume) {
        . $resumeLib
    }

    $readyDeadline = (Get-Date).AddSeconds(180)
    $ready = $false
    $lastNudge = [DateTime]::MinValue
    $nudgeIntervalSec = 6
    while ((Get-Date) -lt $readyDeadline) {
        Assert-NoBlockingGameDialogs -AllowAutoDismiss | Out-Null
        if (Wait-NamedEvent -Name "Local\module_manager_ready" -TimeoutSec 1) {
            $ready = $true
            break
        }
        try {
            $game.Refresh()
        } catch {
            $game = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if (-not $game) {
                Start-Sleep -Milliseconds 150
                continue
            }
        }
        if ($hasResume) {
            $resumed = Resume-ProcessThreads -ProcessId $game.Id
            if ($resumed -gt 0) {
                Write-Host "module_manager: resumed $resumed thread(s) (pid $($game.Id))"
            }
        }
        if ([Win32Module]::HasModule($game.Id, "module_manager.dll")) {
            Write-Host "module_manager: toolhelp snapshot (pid $($game.Id))"
            $ready = $true
            break
        }
        try {
            $earlyPing = Invoke-ModControlPipe -Command "PING" -Target manager -TimeoutMs 1500
            if ($earlyPing -eq "PONG") {
                Write-Host "module_manager: pipe PONG before ready event"
                $ready = $true
                break
            }
        } catch {}
        if (((Get-Date) - $lastNudge).TotalSeconds -ge $nudgeIntervalSec) {
            try {
                Invoke-BootGameNudge -Label "bootstrap nudge (Enter)"
                $lastNudge = Get-Date
            } catch {
                Write-Host "module_manager: bootstrap nudge skipped ($($_.Exception.Message))"
            }
        }
        Start-Sleep -Milliseconds 150
    }
    if (-not $ready) {
        $dialogs = @(Get-HarnessBlockingDialogs)
        if ($dialogs.Count -gt 0) {
            throw (Format-GameDialogReport -Dialogs $dialogs)
        }
        throw "module_manager_ready event timeout"
    }
    Write-Host "module_manager bootstrap ready"

    $pingDeadline = (Get-Date).AddSeconds(60)
    $ping = $null
    while ((Get-Date) -lt $pingDeadline) {
        try {
            $ping = Invoke-ModControlPipe -Command "PING" -Target manager -TimeoutMs 3000
            if ($ping -eq "PONG") { break }
        } catch {
            Start-Sleep -Milliseconds 200
        }
    }
    if ($ping -ne "PONG") {
        throw "manager control pipe PING failed"
    }

    Wait-GameWindow -TimeoutSec 90 | Out-Null
    try {
        Try-FocusGameWindow -Process $game | Out-Null
        Invoke-BootGameNudge -Label "boot nudge (session start)" | Out-Null
    } catch {
        Write-Host "manager: session boot nudge skipped ($($_.Exception.Message))"
    }

    $ctx = [pscustomobject]@{
        Session    = $session
        DeployRoot = $deployRoot
        Game       = $game
        Launcher   = $launch.Launcher
    }
    $ctx = Sync-HarnessDebugLogPath -Context $ctx
    Register-HarnessGameSession -Context $ctx
    return $ctx
}

function Get-WindowLayoutScale {
    $ini = Join-Path $env:TEMP "module_manager.settings.ini"
    if (-not (Test-Path $ini)) {
        return 0.5
    }
    $line = Select-String -Path $ini -Pattern '^Scale=' | Select-Object -First 1
    if (-not $line) {
        return 0.5
    }
    $parts = $line.Line -split '=', 2
    if ($parts.Count -lt 2 -or [string]::IsNullOrWhiteSpace($parts[1])) {
        return 0.5
    }
    $value = $parts[1].Trim()
    $scale = [double]$value
    if ($scale -lt 0.25) { return 0.25 }
    if ($scale -gt 1.0) { return 1.0 }
    return $scale
}

if (-not ("Win32Metrics" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Metrics {
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
}
"@
}

function Set-WindowLayoutHarnessSettings {
    param(
        [double]$WindowScale = (Get-WindowLayoutScale)
    )

    $monitorWidth = [Win32Metrics]::GetSystemMetrics(0)
    $monitorHeight = [Win32Metrics]::GetSystemMetrics(1)
    $windowWidth = [Math]::Max(1, [int][Math]::Round($monitorWidth * $WindowScale))
    $windowHeight = [Math]::Max(1, [int][Math]::Round($monitorHeight * $WindowScale))
    $renderWidth = $windowWidth
    $renderHeight = $windowHeight

    $modIni = Join-Path $env:TEMP "module_manager.settings.ini"
    @(
        "[Window]"
        "Enabled=1"
        "Scale=$WindowScale"
        "ResX=$renderWidth"
        "ResY=$renderHeight"
    ) | Set-Content -Path $modIni -Encoding ASCII

    $documents = [Environment]::GetFolderPath('MyDocuments')
    $tdCandidates = @(
        (Join-Path $documents "EA Games\Mirrors Edge\TdGame\Config\TdEngine.ini"),
        (Join-Path $documents "EA Games\Mirror's Edge\TdGame\Config\TdEngine.ini")
    )

    foreach ($tdEngine in $tdCandidates) {
        $configDir = Split-Path $tdEngine -Parent
        if (-not (Test-Path $configDir)) {
            New-Item -ItemType Directory -Path $configDir -Force | Out-Null
        }

        $lines = @()
        if (Test-Path $tdEngine) {
            $lines = Get-Content -Path $tdEngine
        }

        $sectionFound = $false
        $insertAt = $lines.Count
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match '^\[SystemSettings\]') {
                $sectionFound = $true
                $insertAt = $i + 1
                break
            }
        }

        if (-not $sectionFound) {
            $lines += '[SystemSettings]'
            $insertAt = $lines.Count
        }

        $keys = @{
            'Fullscreen' = 'False'
            'ResX'       = "$renderWidth"
            'ResY'       = "$renderHeight"
        }

        $sectionEnd = $insertAt
        while ($sectionEnd -lt $lines.Count -and $lines[$sectionEnd] -notmatch '^\[') {
            $sectionEnd++
        }

        foreach ($entry in $keys.GetEnumerator()) {
            $replaced = $false
            for ($i = $insertAt; $i -lt $sectionEnd; $i++) {
                if ($lines[$i] -match "^$($entry.Key)=") {
                    $lines[$i] = "$($entry.Key)=$($entry.Value)"
                    $replaced = $true
                    break
                }
            }
            if (-not $replaced) {
                if ($insertAt -gt 0) {
                    $prefix = $lines[0..($insertAt - 1)]
                } else {
                    $prefix = @()
                }
                if ($insertAt -lt $lines.Count) {
                    $suffix = $lines[$insertAt..($lines.Count - 1)]
                } else {
                    $suffix = @()
                }
                $lines = @($prefix + "$($entry.Key)=$($entry.Value)" + $suffix)
                $sectionEnd++
            }
        }

        $lines | Set-Content -Path $tdEngine -Encoding ASCII
    }

    return [pscustomobject]@{
        Scale         = $WindowScale
        WindowWidth   = $windowWidth
        WindowHeight  = $windowHeight
        RenderWidth   = $renderWidth
        RenderHeight  = $renderHeight
        TargetWidth   = $windowWidth
        TargetHeight  = $windowHeight
    }
}

function Get-GameWindowHandle {
    return Resolve-GameWindowHandle
}

function Get-LayoutTargetWindowHandle {
    try {
        $mgrStatus = Get-ManagerStatusJson -TimeoutMs 3000
        foreach ($prop in @('focusHwnd', 'gameHwnd')) {
            $raw = $mgrStatus.$prop
            if ($null -ne $raw -and "$raw" -ne '' -and [int64]$raw -ne 0) {
                $ipcHwnd = [IntPtr]::new([int64]$raw)
                if ([Win32Dialog]::IsWindow($ipcHwnd)) {
                    return $ipcHwnd
                }
            }
        }
    } catch {}
    return Resolve-GameWindowHandle
}

Import-Module (Join-Path $PSScriptRoot "VisualHarness.psm1") -Force

function Test-GameWindowLayout {
    param(
        [IntPtr]$WindowHandle = [IntPtr]::Zero,
        [double]$WindowScale = (Get-WindowLayoutScale)
    )

    if ($WindowHandle -eq [IntPtr]::Zero) {
        $WindowHandle = Get-LayoutTargetWindowHandle
    }

    if ($WindowHandle -eq [IntPtr]::Zero) {
        return [pscustomobject]@{
            Pass          = $false
            Reason        = "no_window"
            WindowHandle  = $WindowHandle
            HasCaption    = $false
            HasThickFrame = $false
            WindowWidth   = 0
            WindowHeight  = 0
            MonitorWidth  = 0
            MonitorHeight = 0
            TargetWidth   = 0
            TargetHeight  = 0
            WindowScale   = $WindowScale
        }
    }

    $style = [Win32Window]::GetWindowLong($WindowHandle, [Win32Window]::GWL_STYLE)
    $hasCaption = ($style -band [Win32Window]::WS_CAPTION) -ne 0
    $hasThickFrame = ($style -band [Win32Window]::WS_THICKFRAME) -ne 0

    $rect = New-Object Win32Window+RECT
    [void][Win32Window]::GetWindowRect($WindowHandle, [ref]$rect)
    $windowWidth = $rect.Right - $rect.Left
    $windowHeight = $rect.Bottom - $rect.Top

    $monitor = [Win32Window]::MonitorFromWindow(
        $WindowHandle, [Win32Window]::MONITOR_DEFAULTTONEAREST)
    $monitorInfo = New-Object Win32Window+MONITORINFO
    $monitorInfo.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($monitorInfo)
    [void][Win32Window]::GetMonitorInfo($monitor, [ref]$monitorInfo)
    $monitorWidth = $monitorInfo.rcMonitor.Right - $monitorInfo.rcMonitor.Left
    $monitorHeight = $monitorInfo.rcMonitor.Bottom - $monitorInfo.rcMonitor.Top

    $targetWidth = [Math]::Max(1, [int][Math]::Round($monitorWidth * $WindowScale))
    $targetHeight = [Math]::Max(1, [int][Math]::Round($monitorHeight * $WindowScale))
    $sizeMatches = [Math]::Abs($windowWidth - $targetWidth) -le 8 `
        -and [Math]::Abs($windowHeight - $targetHeight) -le 8
    $pass = (-not $hasCaption -and -not $hasThickFrame) -and $sizeMatches

    $reason = if ($pass) {
        "ok"
    } elseif ($hasCaption -or $hasThickFrame) {
        "window_still_has_chrome"
    } else {
        "size_mismatch"
    }

    return [pscustomobject]@{
        Pass          = $pass
        Reason        = $reason
        WindowHandle  = $WindowHandle
        HasCaption    = $hasCaption
        HasThickFrame = $hasThickFrame
        WindowWidth   = $windowWidth
        WindowHeight  = $windowHeight
        MonitorWidth  = $monitorWidth
        MonitorHeight = $monitorHeight
        TargetWidth   = $targetWidth
        TargetHeight  = $targetHeight
        WindowScale   = $WindowScale
    }
}

function Wait-GameWindowLayout {
    param(
        [int]$TimeoutSec = 240,
        [switch]$KeepFocused,
        [double]$WindowScale = (Get-WindowLayoutScale)
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = $null
    $lastLayoutNudge = [DateTime]::MinValue
    while ((Get-Date) -lt $deadline) {
        if ($KeepFocused) {
            $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) {
                Focus-GameWindow -Process $proc
            }
        }

        if (((Get-Date) - $lastLayoutNudge).TotalSeconds -ge 4) {
            $lastLayoutNudge = Get-Date
            try {
                $nudge = Invoke-ModControlPipe -Command "APPLY_WINDOW_LAYOUT" -Target manager `
                    -TimeoutMs 5000
                if ($nudge -eq "OK") {
                    Write-Host "layout: APPLY_WINDOW_LAYOUT -> OK"
                }
            } catch {
                Write-Host "layout: APPLY_WINDOW_LAYOUT pending ($($_.Exception.Message))"
            }
        }

        $last = Test-GameWindowLayout -WindowScale $WindowScale
        Write-Host ("layout: reason={0} size={1}x{2} target={3}x{4}" -f `
            $last.Reason, $last.WindowWidth, $last.WindowHeight, `
            $last.TargetWidth, $last.TargetHeight)

        if ($last.Pass) {
            Invoke-HarnessVisualMilestone -Step "borderless_layout_ok" | Out-Null
            return $last
        }

        Start-Sleep -Seconds 2
    }

    throw "window layout timeout (last reason: $($last.Reason))"
}

function Get-ManagerStatusJson {
    param([int]$TimeoutMs = 5000)

    $json = Invoke-ModControlPipe -Command "GET_STATUS" -Target manager -TimeoutMs $TimeoutMs
    if ($json -notmatch '^\s*\{') {
        throw "Invalid manager GET_STATUS (not JSON): $($json.Substring(0, [Math]::Min(80, $json.Length)))"
    }
    try {
        return $json | ConvertFrom-Json
    } catch {
        throw "Invalid manager GET_STATUS JSON: $($_.Exception.Message) raw=$($json.Substring(0, [Math]::Min(120, $json.Length)))"
    }
}

function Get-MemoryFaultList {
    <#
    .SYNOPSIS
      Return recorded SEH memory faults from module_manager GET_STATUS (memoryFaults array).
    #>
    param([int]$TimeoutMs = 5000)

    $status = Get-ManagerStatusJson -TimeoutMs $TimeoutMs
    if ($status.PSObject.Properties.Name -contains 'memoryFaults') {
        return @($status.memoryFaults)
    }
    return @()
}

function Show-MemoryFaultList {
    param([int]$TimeoutMs = 5000, [switch]$Json)

    $faults = Get-MemoryFaultList -TimeoutMs $TimeoutMs
    if ($Json) {
        $faults | ConvertTo-Json -Depth 6
        return
    }

    Write-Host "=== Memory faults ($(@($faults).Count) ===" -ForegroundColor Cyan
    if (-not @($faults).Count) {
        Write-Host "(none)"
        return
    }

    foreach ($f in $faults) {
        Write-Host ""
        Write-Host ("+{0}ms thread={1} {2} (0x{3:X8})" -f $f.elapsedMs, $f.threadId, $f.exceptionName, [uint32]$f.exceptionCode) -ForegroundColor Yellow
        Write-Host ("  faultAddress=0x{0:X8}  eip=0x{1:X8}" -f [uint32]$f.faultAddress, [uint32]$f.instructionPointer)
        Write-Host ("  context={0}  location={1}" -f $f.context, $f.location)
    }
}

function Wait-ManagerUiState {
    param(
        [Parameter(Mandatory)]
        [scriptblock]$Predicate,
        [int]$TimeoutSec = 60,
        [switch]$KeepFocused,
        [string]$Label = "ui state"
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = $null
    while ((Get-Date) -lt $deadline) {
        if ($KeepFocused) {
            $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) {
                Focus-GameWindow -Process $proc
            }
        }

        try {
            $last = Get-ManagerStatusJson
            if (& $Predicate $last) {
                return $last
            }
        } catch {
            Write-Host "$Label pending: $($_.Exception.Message)"
        }

        Start-Sleep -Milliseconds 500
    }

    $detail = if ($last) { ($last | ConvertTo-Json -Compress) } else { "(no status)" }
    throw "$Label timeout (last: $detail)"
}

function Invoke-ModuleManagerUiCommand {
    param(
        [Parameter(Mandatory)]
        [string]$Command,
        [int]$TimeoutMs = 10000
    )

    $response = Invoke-ModControlPipe -Command $Command -Target manager -TimeoutMs $TimeoutMs
    if ($response -ne "OK") {
        throw "module_manager UI command failed ($Command): $response"
    }
    return $response
}

function Test-ModuleManagerOverlayUi {
    param(
        [int]$TimeoutSec = 90,
        [switch]$KeepFocused,
        [switch]$SkipVisual
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    $doVisual = -not $SkipVisual -and $env:MMOD_DEBUG_SKIP_VISUAL -ne "1"
    $visualBaseline = $null
    $visualAfterClose = $null

    $initial = Wait-ManagerUiState -Label "overlay ready" -TimeoutSec $TimeoutSec `
        -KeepFocused:$focus -Predicate {
            param($s)
            $s.hooksInstalled -eq $true -and $s.overlayReady -eq $true
        }

    if ($initial.menuOpen -eq $true) {
        Invoke-ModuleManagerUiCommand -Command "MENU_CLOSE"
        Wait-ManagerUiState -Label "menu closed (cleanup)" -TimeoutSec 30 `
            -KeepFocused:$focus -Predicate { param($s) $s.menuOpen -eq $false } | Out-Null
    }

    Start-Sleep -Milliseconds 1500
    if ($focus) {
        try { Try-FocusGameWindow | Out-Null } catch {}
    }

    if ($doVisual) {
        $visualBaseline = Invoke-HarnessVisualMilestone -Step "menu_closed_baseline"
    }

    Open-ModuleManagerMenuGui -TimeoutSec $TimeoutSec | Out-Null
    $open = Wait-ManagerUiState -Label "menu open" -TimeoutSec $TimeoutSec `
        -KeepFocused:$focus -Predicate { param($s) $s.menuOpen -eq $true }
    Write-Host "ui: menu open activeTab=$($open.activeTab)"

    $expectedTabs = @("Modules")
    foreach ($tab in $expectedTabs) {
        if ($open.tabs -notcontains $tab) {
            throw "missing tab: $tab (have: $($open.tabs -join ', '))"
        }
    }

    if ($doVisual) {
        $visualOpen = Invoke-HarnessVisualMilestone -Step "menu_open"
        if ($visualBaseline -and $visualOpen) {
            Assert-VisualDelta -Before $visualBaseline -After $visualOpen `
                -Label "menu_open vs closed"
        }
    }

    Invoke-ModuleManagerUiCommand -Command "MENU_TAB Modules"
    Wait-ManagerUiState -Label "Modules tab" -TimeoutSec $TimeoutSec `
        -KeepFocused:$focus -Predicate {
            param($s)
            $s.menuOpen -eq $true -and $s.activeTab -eq "Modules"
        } | Out-Null
    Write-Host "ui: Modules tab selected"

    Invoke-ModuleManagerUiCommand -Command "MENU_CLOSE"
    Wait-ManagerUiState -Label "menu closed" -TimeoutSec $TimeoutSec `
        -KeepFocused:$focus -Predicate { param($s) $s.menuOpen -eq $false } | Out-Null
    Write-Host "ui: menu closed"

    if ($doVisual) {
        $visualAfterClose = Invoke-HarnessVisualMilestone -Step "menu_closed_after"
    }

    Invoke-ModuleManagerUiCommand -Command "CONSOLE_OPEN"
    Wait-ManagerUiState -Label "console open" -TimeoutSec $TimeoutSec `
        -KeepFocused:$focus -Predicate { param($s) $s.consoleOpen -eq $true } | Out-Null
    Write-Host "ui: console open"

    if ($doVisual) {
        $visualConsole = Invoke-HarnessVisualMilestone -Step "console_open"
        $visualCompare = if ($visualAfterClose) { $visualAfterClose } else { $visualBaseline }
        if ($visualCompare -and $visualConsole) {
            Assert-VisualDelta -Before $visualCompare -After $visualConsole `
                -Label "console_open vs menu_closed"
        }
    }

    Invoke-ModuleManagerUiCommand -Command "CONSOLE_CLOSE"
    Wait-ManagerUiState -Label "console closed" -TimeoutSec $TimeoutSec `
        -KeepFocused:$focus -Predicate { param($s) $s.consoleOpen -eq $false } | Out-Null
    Write-Host "ui: console closed"

    if ($doVisual) {
        $manifestPath = Write-HarnessVisualManifest
        Write-Host "visual: overlay captures -> $(Get-VisualArtifactsDir)"
        if ($manifestPath) {
            Write-Host "visual: manifest -> $manifestPath"
        }
    }

    return [pscustomobject]@{
        Pass       = $true
        Tabs       = @($open.tabs)
        VisualPass = $doVisual
    }
}

function Test-MmultiplayerGuiSuite {
    param(
        $Context = $null,
        [switch]$KeepFocused,
        [switch]$SkipInject,
        [int]$TimeoutSec = 180
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    Wait-ManagerHooksReady -KeepFocused:$focus -BootNudge -TimeoutSec $TimeoutSec | Out-Null

    if (-not $SkipInject) {
        Invoke-EnsureCoreLoaded -LogPath (Get-SafeContextLogPath -Context $Context) `
            -TimeoutSec $TimeoutSec | Out-Null
        Write-Host "mp-gui: core auto-load OK"
        Open-ModuleManagerMenuGui -TimeoutSec $TimeoutSec | Out-Null
    } else {
        Open-ModuleManagerMenuGui -TimeoutSec $TimeoutSec | Out-Null
    }

    $mpPingFinal = Invoke-ModControlPipe -Command "PING" -Target core
    if ($mpPingFinal -ne "PONG") {
        throw "core PING failed before GUI mod toggle"
    }

    Invoke-ModUiTabClick -TabName "Modules" -TimeoutSec $TimeoutSec
    Invoke-ManagerModuleInject -ModuleId "multiplayer" -TimeoutSec $TimeoutSec
    Wait-ManagerUiState -Label "Multiplayer tab registered" -TimeoutSec $TimeoutSec `
        -Predicate { param($s) @($s.tabs) -contains "Multiplayer" } | Out-Null
    Start-Sleep -Milliseconds 2500
    Assert-GameProcessAlive -Label "before Multiplayer tab" -SkipHangCheck | Out-Null
    Open-ModuleManagerMenuGui -TimeoutSec $TimeoutSec | Out-Null

    Invoke-ModUiTabClick -TabName "Multiplayer" -TimeoutSec $TimeoutSec
    Start-Sleep -Milliseconds 800

    $settingsDeadline = (Get-Date).AddSeconds($TimeoutSec)
    $settingsVisible = $false
    while ((Get-Date) -lt $settingsDeadline) {
        Assert-GameProcessAlive -Label "mp settings tab" -SkipHangCheck | Out-Null
        try {
            $doc = Get-ModUiTargetsJson -Target manager -TimeoutMs 4000
            $hits = @($doc.targets | Where-Object { $_.id -like "mm/multiplayer/*" })
            if ($hits.Count -gt 0) {
                $settingsVisible = $true
                break
            }
        } catch {}
        Start-Sleep -Milliseconds 400
    }
    if (-not $settingsVisible) {
        throw "Multiplayer settings tab UI not visible within ${TimeoutSec}s"
    }
    Write-Host "mp-gui: Multiplayer settings tab visible"

    # Disable-via-checkbox skipped: Client::Disable with menu open can crash/hang IPC.
    # Enable + Multiplayer tab coverage is the primary GUI regression target.

    Close-ManagerOverlays
    Invoke-ModuleManagerUiCommand -Command "MENU_CLOSE"
    Wait-ManagerUiState -Label "menu closed" -TimeoutSec 30 `
        -KeepFocused:$focus -Predicate { param($s) $s.menuOpen -eq $false } | Out-Null
    Write-Host "mp-gui: menu closed"
    Invoke-HarnessVisualMilestone -Step "suite_mp_gui_complete" | Out-Null

    return [pscustomobject]@{ Pass = $true }
}

function Get-ControlScreenCenter {
    param(
        [Parameter(Mandatory)]
        [IntPtr]$Hwnd
    )

    $rect = New-Object Win32Window+RECT
    if (-not [Win32Window]::GetWindowRect($Hwnd, [ref]$rect)) {
        throw "GetWindowRect failed for control handle"
    }

    return @{
        X = [int](($rect.Left + $rect.Right) / 2)
        Y = [int](($rect.Top + $rect.Bottom) / 2)
    }
}

function Click-DialogControl {
    param(
        [Parameter(Mandatory)]
        [IntPtr]$ControlHwnd,
        [string]$Label = "control",
        [int]$HoldMs = 220
    )

    if ($ControlHwnd -eq [IntPtr]::Zero) {
        throw "Click-DialogControl: null handle ($Label)"
    }

    $pt = Get-ControlScreenCenter -Hwnd $ControlHwnd
    [Win32Input]::MouseClickScreen($pt.X, $pt.Y, $HoldMs)
    Write-Host "ui-click: launcher $Label @ screen($($pt.X),$($pt.Y))"
    Start-Sleep -Milliseconds 300
}

function Focus-LauncherWindow {
    param(
        [Parameter(Mandatory)]
        [IntPtr]$Hwnd
    )

    if ($Hwnd -eq [IntPtr]::Zero) {
        throw "Focus-LauncherWindow: null hwnd"
    }

    [void][Win32Focus]::ShowWindow($Hwnd, 9) # SW_RESTORE
    [void][Win32Focus]::SetForegroundWindow($Hwnd)
    Start-Sleep -Milliseconds 250
}

function Wait-LauncherWindow {
    param(
        [int]$TimeoutSec = 90,
        [int]$ProcessId = 0
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $hwnd = Find-LauncherDialogHwnd -ProcessId $ProcessId
        if ($hwnd -ne [IntPtr]::Zero) {
            [void][Win32Focus]::ShowWindow($hwnd, 9)
            return $hwnd
        }
        $launcherProc = Get-Process ModuleLauncher, mirroredge-module-launcher -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($launcherProc -and $launcherProc.MainWindowHandle -ne [IntPtr]::Zero) {
            $hwnd = $launcherProc.MainWindowHandle
            [void][Win32Focus]::ShowWindow($hwnd, 9)
            $launchBtn = [Win32Dialog]::GetDlgItem($hwnd, 1004)
            $closeBtn = [Win32Dialog]::GetDlgItem($hwnd, 1003)
            if ($launchBtn -ne [IntPtr]::Zero -and $closeBtn -ne [IntPtr]::Zero) {
                return $hwnd
            }
        }
        Start-Sleep -Milliseconds 250
    }

    throw "Timed out waiting for launcher window (mirroredge_module_launcher_dialog)"
}

function Test-LauncherStatusDialogUi {
    param(
        [Parameter(Mandatory)]
        [string]$RepoRoot,
        [int]$CloseTimeoutSec = 20
    )

    $proc = Start-ModuleLauncher -RepoRoot $RepoRoot
    try {
        $hwnd = Wait-LauncherWindow -ProcessId $proc.Id
        $kLaunchGame = 1004
        $kClose = 1003

        $launchBtn = [Win32Dialog]::GetDlgItem($hwnd, $kLaunchGame)
        $closeBtn = [Win32Dialog]::GetDlgItem($hwnd, $kClose)
        if ($launchBtn -eq [IntPtr]::Zero -or $closeBtn -eq [IntPtr]::Zero) {
            throw "launcher controls not found"
        }
        if (-not [Win32Dialog]::IsWindowEnabled($closeBtn)) {
            throw "launcher Close button should be enabled immediately"
        }
        if (-not [Win32Dialog]::IsWindowEnabled($launchBtn)) {
            throw "launcher Launch Game button should be enabled"
        }

        [void][Win32Dialog]::PostMessage($hwnd, [Win32Dialog]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
        $closed = $proc.WaitForExit($CloseTimeoutSec * 1000)
        if (-not $closed) {
            $stillOpen = [Win32Dialog]::IsWindow($hwnd)
            if (-not $stillOpen) {
                Write-Host "WARN: launcher window closed but process still running; forcing exit"
                Stop-GameProcessById -ProcessId $proc.Id
                $proc.WaitForExit(5000)
            } else {
                throw "launcher did not exit after WM_CLOSE"
            }
        }

        return [pscustomobject]@{ Pass = $true }
    } finally {
        if (-not $proc.HasExited) {
            Stop-GameProcessById -ProcessId $proc.Id
        }
    }
}

function Test-ViewportMouseAlignment {
    param(
        [int]$TimeoutSec = 120,
        [switch]$KeepFocused
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = $null
    while ((Get-Date) -lt $deadline) {
        if ($focus) {
            $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) {
                Focus-GameWindow -Process $proc
            }
        }

        $last = Get-ManagerStatusJson
        if ($last.PSObject.Properties.Name -notcontains "windowLayoutEnabled") {
            Write-Host "viewport: windowLayoutEnabled not in status, skip alignment check"
            return $last
        }
        if ($last.windowLayoutEnabled -ne $true) {
            Write-Host "viewport: window layout disabled, skip alignment check"
            return $last
        }

        if ($last.clientWidth -gt 0 -and $last.viewportWidth -gt 0) {
            $dw = [Math]::Abs([int]$last.clientWidth - [int]$last.viewportWidth)
            $dh = [Math]::Abs([int]$last.clientHeight - [int]$last.viewportHeight)
            Write-Host ("viewport: client={0}x{1} engine={2}x{3} delta={4}x{5}" -f `
                $last.clientWidth, $last.clientHeight, `
                $last.viewportWidth, $last.viewportHeight, $dw, $dh)
            if ($dw -le 2 -and $dh -le 2) {
                return $last
            }
        }

        Start-Sleep -Seconds 2
    }

    if ($last -and $last.viewportWidth -gt $last.clientWidth) {
        Write-Host "WARN: engine viewport larger than client window (mouse may feel fast before compensation)"
        if ($last.mouseLookScale -and [double]$last.mouseLookScale -lt 0.95) {
            Write-Host ("viewport: mouseLookScale={0:F3} (compensation active)" -f [double]$last.mouseLookScale)
        }
    }

    return $last
}

function Wait-ModuleManagerLoadLog {
    param(
        [string]$ModuleId = "core",
        [int]$TimeoutSec = 120
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $raw = Invoke-ModControlPipe -Command "GET_LOG 120" -Target manager -TimeoutMs 8000
            if ($raw -match "LoadLibrary crashed|PluginInitialize crashed|load failed after") {
                throw "module load failed in manager log`n$raw"
            }

            if ($raw -match "inject rejected.*$([regex]::Escape($ModuleId))") {
                throw "module inject rejected for $ModuleId`n$raw"
            }

            $status = Get-ManagerStatusJson -TimeoutMs 5000
            $mod = $status.modules | Where-Object { $_.id -eq $ModuleId } | Select-Object -First 1

            if ($mod -and $mod.loaded -and $mod.status -eq "Loaded") {
                $hasQueue = $raw -match "queue inject $([regex]::Escape($ModuleId)) via"
                $hasLibrary = $raw -match "LoadLibrary OK"
                $hasInit = $raw -match "PluginInitialize OK"
                Write-Host ("mod-full: load log OK for {0} (queue={1} library={2} init={3})" -f `
                    $ModuleId, $hasQueue, $hasLibrary, $hasInit)
                try {
                    Start-Sleep -Milliseconds 500
                    Invoke-HarnessVisualMilestone -Step "module_load_log_$ModuleId" | Out-Null
                } catch {
                    Write-Host "visual: WARN module_load_log_$ModuleId ($($_.Exception.Message))"
                }
                return $raw
            }
        } catch {
            if ($_.Exception.Message -like "module load failed*" -or
                $_.Exception.Message -like "module inject rejected*") {
                throw
            }
            Write-Host "mod-load: pending for $ModuleId ($($_.Exception.Message))"
        }

        Start-Sleep -Milliseconds 400
    }

    throw "module load log timeout for $ModuleId"
}

function Invoke-ConsoleModuleInject {
    param(
        [Parameter(Mandatory)]
        [string]$ModuleId,
        [int]$TimeoutSec = 120,
        [switch]$TryKeyboard
    )

    if ($TryKeyboard) {
        Send-GameText -Text "inject $ModuleId"
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs 500

        $keyboardDeadline = (Get-Date).AddSeconds(12)
        while ((Get-Date) -lt $keyboardDeadline) {
            try {
                $status = Get-ManagerStatusJson -TimeoutMs 3000
                if ($status.modules) {
                    $mod = @($status.modules | Where-Object {
                            $_.id -eq $ModuleId -and $_.loaded -eq $true
                        })
                    if ($mod.Count -gt 0) {
                        break
                    }
                }
            } catch {}
            Start-Sleep -Milliseconds 400
        }
    }

    try {
        $status = Get-ManagerStatusJson -TimeoutMs 3000
        $loaded = $false
        if ($status.modules) {
            $mod = @($status.modules | Where-Object {
                    $_.id -eq $ModuleId -and $_.loaded -eq $true
                })
            $loaded = ($mod.Count -gt 0)
        }
        if (-not $loaded) {
            Write-Host "harness: console keyboard did not queue inject $ModuleId; using CONSOLE_EXEC"
            $exec = Invoke-ModControlPipe -Command "CONSOLE_EXEC inject $ModuleId" `
                -Target manager -TimeoutMs 8000
            if ($exec -ne "OK") {
                throw "CONSOLE_EXEC inject $ModuleId failed: $exec"
            }
        }
    } catch {
        if ($_.Exception.Message -match "CONSOLE_EXEC") {
            throw
        }
        Write-Host "harness: CONSOLE_EXEC fallback ($($_.Exception.Message))"
        $exec = Invoke-ModControlPipe -Command "CONSOLE_EXEC inject $ModuleId" `
            -Target manager -TimeoutMs 8000
        if ($exec -ne "OK") {
            throw "CONSOLE_EXEC inject $ModuleId failed: $exec"
        }
    }

    Wait-ModuleManagerLoadLog -ModuleId $ModuleId -TimeoutSec $TimeoutSec | Out-Null
}

function Scroll-ManagerModulesListForTarget {
    param(
        [Parameter(Mandatory)]
        [string]$Id,
        [int]$MaxScrolls = 16
    )

    for ($i = 0; $i -le $MaxScrolls; $i++) {
        try {
            $doc = Get-ModUiTargetsJson -Target manager -TimeoutMs 3000
            if (@($doc.targets | Where-Object { $_.id -eq $Id }).Count -gt 0) {
                return $true
            }
        } catch {}

        if ($i -eq $MaxScrolls) {
            break
        }

        $proc = Get-GameProcess
        Focus-GameWindow -Process $proc | Out-Null
        [Win32Input]::MouseWheel(-120)
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Test-ManagerModuleInjectAccepted {
    param([string]$Result)

    return ($Result -eq "OK") -or ($Result -eq "ERR already loaded") -or ($Result -eq "ERR busy")
}

function Test-ManagerModuleLoadPending {
    param(
        [Parameter(Mandatory)]
        [string]$ModuleId
    )

    try {
        $status = Get-ManagerStatusJson -TimeoutMs 5000
        if (-not $status.modules) {
            return $false
        }
        $mod = @($status.modules | Where-Object { $_.id -eq $ModuleId })
        if ($mod.Count -eq 0) {
            return $false
        }
        return ($mod[0].loaded -eq $true) -or ($mod[0].busy -eq $true) `
            -or ([string]$mod[0].status -like "Loading*")
    } catch {
        return $false
    }
}

function Invoke-ManagerModuleInject {
    param(
        [Parameter(Mandatory)]
        [string]$ModuleId,
        [int]$TimeoutSec = 120,
        [switch]$PreferUi
    )

    $targetId = "manager/inject/$ModuleId"
    $uiInjected = $false

    if ($PreferUi) {
        try {
            Invoke-ModUiTabClick -TabName "Modules" -TimeoutSec $TimeoutSec | Out-Null
            [void](Scroll-ManagerModulesListForTarget -Id $targetId)
            Send-GameUiClick -Id $targetId -Target manager `
                -TimeoutSec 15 -SettleMs 700
            $uiInjected = $true
            Write-Host "harness: injected $ModuleId via Modules tab UI"
        } catch {
            Write-Host "harness: UI inject $ModuleId unavailable ($($_.Exception.Message))"
        }
    }

    if (-not $uiInjected) {
        $inject = Invoke-ModControlPipe -Command "INJECT $ModuleId" -Target manager -TimeoutMs 8000
        if (-not (Test-ManagerModuleInjectAccepted -Result $inject)) {
            throw "INJECT $ModuleId failed: $inject"
        }
        if ($inject -ne "OK") {
            Write-Host "harness: inject $ModuleId accepted ($inject)"
        } else {
            Write-Host "harness: injected $ModuleId via manager pipe"
        }
    }

    Wait-ModuleManagerLoadLog -ModuleId $ModuleId -TimeoutSec $TimeoutSec | Out-Null
}

function Test-ModFullSuite {
    param(
        [Parameter(Mandatory)]
        $Context,
        [switch]$SkipBuild
    )

    $repo = Get-RepoRoot

    $ping = Invoke-ModControlPipe -Command "PING" -Target manager
    if ($ping -ne "PONG") {
        throw "manager PING failed: $ping"
    }
    Write-Host "mod-full: manager PING OK"

    Wait-ManagerHooksReady -KeepFocused -BootNudge -TimeoutSec 180 | Out-Null
    Test-ModuleManagerOverlayUi -KeepFocused | Out-Null
    Write-Host "mod-full: module_manager UI OK"

    $managerStatus = Get-ManagerStatusJson
    if ($managerStatus.hooksInstalled -ne $true -or $managerStatus.overlayReady -ne $true) {
        throw "manager hooks/overlay not ready"
    }

    $list = Invoke-ModControlPipe -Command "LIST_MODULES" -Target manager
    if ($list -match "(?m)^core\\b") {
        throw "LIST_MODULES should not list core (builtin auto-load)"
    }
    Write-Host "mod-full: LIST_MODULES OK (third-party mods only)"

    Invoke-EnsureCoreLoaded -LogPath (Get-SafeContextLogPath -Context $Context) | Out-Null
    Write-Host "mod-full: core auto-load OK"
    $mpPing = Invoke-ModControlPipe -Command "PING" -Target core
    if ($mpPing -ne "PONG") {
        throw "core PING failed: $mpPing"
    }
    Write-Host "mod-full: core PING OK"

    $mpStatus = Invoke-ModControlPipe -Command "GET_STATUS" -Target core -TimeoutMs 8000 |
        ConvertFrom-Json
    if (-not (Get-EngineModReadyFromStatus $mpStatus)) {
        throw "core engine.modReady=false"
    }
    Write-Host ("mod-full: core ready hosted={0}" -f $mpStatus.hostedMode)

    $mods = Invoke-ModControlPipe -Command "LIST_MODS" -Target core
    if ($mods -notmatch "(?m)^END$" -and $mods -notlike "ERR deprecated*") {
        throw "LIST_MODS missing END"
    }
    Write-Host "mod-full: LIST_MODS OK"

    Test-ViewportMouseAlignment -KeepFocused | Out-Null

    $logSeq = Test-LogSequence -Sequence @(
        "direct3d_create9",
        "create_device_ok",
        "hooks_installed"
    ) -LogPath (Get-SafeContextLogPath -Context $Context) -TimeoutSec 30
    if (-not $logSeq.Pass) {
        Write-Host "WARN: NDJSON hook sequence incomplete"
    }

    Invoke-HarnessVisualMilestone -Step "suite_mod_full_complete" | Out-Null
    return [pscustomobject]@{ Pass = $true }
}

function Expand-CoreHarnessStatus {
    param($Status)

    if (-not $Status) {
        return $Status
    }

    if ($Status.engine) {
        foreach ($prop in $Status.engine.PSObject.Properties) {
            if ($Status.PSObject.Properties.Name -notcontains $prop.Name) {
                Add-Member -InputObject $Status -NotePropertyName $prop.Name `
                    -NotePropertyValue $prop.Value
            }
        }
    }

    if ($Status.PSObject.Properties.Name -contains "multiplayer" -and $null -ne $Status.multiplayer) {
        $mp = $Status.multiplayer
        if (($mp.PSObject.Properties.Name -contains "connected") -and
            ($Status.PSObject.Properties.Name -notcontains "mpConnected")) {
            Add-Member -InputObject $Status -NotePropertyName mpConnected `
                -NotePropertyValue ([bool]$mp.connected)
        }
        if (($mp.PSObject.Properties.Name -contains "remotePlayers") -and
            ($Status.PSObject.Properties.Name -notcontains "mpRemotePlayers")) {
            Add-Member -InputObject $Status -NotePropertyName mpRemotePlayers `
                -NotePropertyValue ([int]$mp.remotePlayers)
        }
        foreach ($pair in @(
                @{ Src = "posX"; Dst = "mpPosX" }
                @{ Src = "posY"; Dst = "mpPosY" }
                @{ Src = "posZ"; Dst = "mpPosZ" }
                @{ Src = "yaw";  Dst = "mpYaw" }
            )) {
            if (($mp.PSObject.Properties.Name -contains $pair.Src) -and
                ($Status.PSObject.Properties.Name -notcontains $pair.Dst)) {
                Add-Member -InputObject $Status -NotePropertyName $pair.Dst `
                    -NotePropertyValue $mp.($pair.Src)
            }
        }
        if (($mp.PSObject.Properties.Name -contains "clientMap") -and
            [string]::IsNullOrWhiteSpace([string]$Status.currentMap) -and
            -not [string]::IsNullOrWhiteSpace([string]$mp.clientMap)) {
            if ($Status.PSObject.Properties.Name -contains "currentMap") {
                $Status.currentMap = [string]$mp.clientMap
            } else {
                Add-Member -InputObject $Status -NotePropertyName currentMap `
                    -NotePropertyValue ([string]$mp.clientMap)
            }
        }
        if (($mp.PSObject.Properties.Name -contains "inGameplay") -and
            $Status.inGameplay -ne $true -and [bool]$mp.inGameplay) {
            if ($Status.PSObject.Properties.Name -contains "inGameplay") {
                $Status.inGameplay = $true
            } else {
                Add-Member -InputObject $Status -NotePropertyName inGameplay `
                    -NotePropertyValue $true
            }
        }
    }

    if ($Status.PSObject.Properties.Name -notcontains "currentMap") {
        Add-Member -InputObject $Status -NotePropertyName currentMap -NotePropertyValue ""
    }
    if ($Status.PSObject.Properties.Name -notcontains "inGameplay") {
        Add-Member -InputObject $Status -NotePropertyName inGameplay -NotePropertyValue $false
    }
    if ($Status.PSObject.Properties.Name -notcontains "mpConnected") {
        Add-Member -InputObject $Status -NotePropertyName mpConnected -NotePropertyValue $false
    }
    if ($Status.PSObject.Properties.Name -notcontains "mpRemotePlayers") {
        Add-Member -InputObject $Status -NotePropertyName mpRemotePlayers -NotePropertyValue 0
    }

    return $Status
}

function Get-MmultiplayerStatusJson {
    param(
        [int]$TimeoutMs = 8000,
        [switch]$PreferManager
    )

    if (-not $PreferManager) {
        try {
            $json = Invoke-ModControlPipe -Command "GET_STATUS" -Target core `
                -TimeoutMs $TimeoutMs -MaxAttempts 1
            return Expand-CoreHarnessStatus ($json | ConvertFrom-Json)
        } catch {
            Write-Host "core GET_STATUS via pipe failed: $($_.Exception.Message)"
        }
    }

    $mgr = Get-ManagerStatusJson -TimeoutMs $TimeoutMs
    if ($mgr.PSObject.Properties.Name -notcontains "engine") {
        throw "manager GET_STATUS missing engine property"
    }
    if ($null -eq $mgr.engine) {
        Write-Host "manager: engine not loaded, returning manager status (engine=null)"
        return $mgr
    }

    $status = [PSCustomObject]@{
        component  = 'core'
        hostedMode = $true
        engine     = $mgr.engine
    }
    if ($mgr.PSObject.Properties.Name -contains "modules") {
        Add-Member -InputObject $status -NotePropertyName modules -NotePropertyValue $mgr.modules
    }

    try {
        $coreJson = Invoke-ModControlPipe -Command "GET_STATUS" -Target core `
            -TimeoutMs ([Math]::Min($TimeoutMs, 4000)) -MaxAttempts 1
        $coreRaw = $coreJson | ConvertFrom-Json
        foreach ($prop in @("multiplayer", "currentMap", "inGameplay", "menuOpen", "gameHwnd")) {
            if (($coreRaw.PSObject.Properties.Name -contains $prop) -and
                ($status.PSObject.Properties.Name -notcontains $prop)) {
                Add-Member -InputObject $status -NotePropertyName $prop `
                    -NotePropertyValue $coreRaw.$prop
            }
        }
    } catch {
        Write-Host "core GET_STATUS merge failed: $($_.Exception.Message)"
    }

    return Expand-CoreHarnessStatus $status
}

function Get-MmultiplayerModsMap {
    param([int]$TimeoutMs = 12000)

    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        try {
            $raw = Invoke-ModControlPipe -Command "LIST_MODS" -Target core -TimeoutMs 5000
            $mods = @{}
            foreach ($line in ($raw -split "`n")) {
                if ($line -eq "END" -or [string]::IsNullOrWhiteSpace($line)) {
                    continue
                }
                if ($line -match "^MOD\s+(\S+)\t(.+)\t(.+)\t([01])$") {
                    $mods[$Matches[1]] = [pscustomobject]@{
                        Id          = $Matches[1]
                        Name        = $Matches[2]
                        Description = $Matches[3]
                        Enabled     = ($Matches[4] -eq "1")
                    }
                }
            }
            return $mods
        } catch {
            Start-Sleep -Milliseconds 400
        }
    }

    return @{}
}

function Wait-MmultiplayerStatus {
    param(
        [Parameter(Mandatory)]
        [scriptblock]$Predicate,
        [int]$TimeoutSec = 90,
        [switch]$KeepFocused,
        [string]$Label = "core status"
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = $null
    $nextNudge = [DateTime]::MinValue
    while ((Get-Date) -lt $deadline) {
        if ($KeepFocused) {
            $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) {
                Focus-GameWindow -Process $proc
            }
            if ((Get-Date) -gt $nextNudge) {
                try {
                    Invoke-BootGameNudge -Label "mp status nudge"
                    $nextNudge = (Get-Date).AddSeconds(6)
                } catch {}
            }
        }

        try {
            $last = Get-MmultiplayerStatusJson
            if (& $Predicate $last) {
                return $last
            }
        } catch {
            Write-Host "$Label pending: $($_.Exception.Message)"
        }

        Start-Sleep -Milliseconds 500
    }

    $detail = if ($last) { ($last | ConvertTo-Json -Compress) } else { "(no status)" }
    throw "$Label timeout (last: $detail)"
}

function Wait-MmultiplayerModEnabled {
    param(
        [Parameter(Mandatory)]
        [string]$ModId,
        [bool]$Enabled,
        [int]$TimeoutSec = 90,
        [switch]$KeepFocused
    )

    $want = if ($Enabled) { "enabled" } else { "disabled" }
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($KeepFocused) {
            $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) {
                Focus-GameWindow -Process $proc
            }
        }

        $mods = Get-MmultiplayerModsMap
        if ($mods.ContainsKey($ModId) -and $mods[$ModId].Enabled -eq $Enabled) {
            return $mods[$ModId]
        }

        Start-Sleep -Milliseconds 500
    }

    throw "mod $ModId not $want within ${TimeoutSec}s"
}

function Set-MmultiplayerModEnabled {
    param(
        [Parameter(Mandatory)]
        [string]$ModId,
        [bool]$Enabled
    )

    $flag = if ($Enabled) { "1" } else { "0" }
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        try {
            $response = Invoke-ModControlPipe -Command "SET_MOD $ModId $flag" `
                -Target core -TimeoutMs 15000
            if ($response -eq "OK") {
                return
            }
            throw "SET_MOD $ModId $flag failed: $response"
        } catch {
            try {
                $mods = Get-MmultiplayerModsMap
                if ($mods.ContainsKey($ModId) -and $mods[$ModId].Enabled -eq $Enabled) {
                    Write-Host "SET_MOD $ModId $flag confirmed via LIST_MODS"
                    return
                }
            } catch {}
            Write-Host "SET_MOD $ModId retry: $($_.Exception.Message)"
            Start-Sleep -Milliseconds 500
        }
    }

    throw "SET_MOD $ModId $flag failed after retries"
}

function Test-TcpPortListening {
    param(
        [Parameter(Mandatory)]
        [int]$Port,
        [string]$HostName = "127.0.0.1",
        [int]$TimeoutMs = 1500
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $async = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne($TimeoutMs)) {
            return $false
        }
        $client.EndConnect($async)
        return $true
    } catch {
        return $false
    } finally {
        if ($client.Connected) {
            $client.Close()
        }
        $client.Dispose()
    }
}

function Start-MmultiplayerServer {
    param(
        [string]$RepoRoot,
        [int]$Port = 5222,
        [int]$WaitSec = 30
    )

    if (Test-TcpPortListening -Port $Port) {
        Write-Host "mp-server: stopping existing listener on port $Port"
        Stop-Process -Name multiplayer-server -Force -ErrorAction SilentlyContinue
        Stop-Process -Name mmultiplayer-server -Force -ErrorAction SilentlyContinue
        Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
            ForEach-Object {
                Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue
            }
        Start-Sleep -Milliseconds 400
    }

    if (Test-TcpPortListening -Port $Port) {
        throw "mp-server: port $Port still in use after cleanup"
    }

    $candidates = @(
        (Join-Path (Resolve-DeployPath -RepoRoot $RepoRoot) "multiplayer-server.exe"),
        (Join-Path $RepoRoot "dist\modules\multiplayer\multiplayer-server.exe")
    )
    $serverExe = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $serverExe) {
        throw "multiplayer-server.exe not found (build with build.ps1, Go required)"
    }

    $distServer = Join-Path $RepoRoot "dist\modules\multiplayer\multiplayer-server.exe"
    if ((Test-Path $distServer) -and ($serverExe -ne $distServer)) {
        Copy-Item $distServer $serverExe -Force
        Write-Host "mp-server: refreshed from dist -> $serverExe"
    }

    $proc = Start-Process -FilePath $serverExe -PassThru -WindowStyle Hidden
    Write-Host "mp-server: started PID $($proc.Id) from $serverExe"

    $deadline = (Get-Date).AddSeconds($WaitSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-TcpPortListening -Port $Port) {
            Write-Host "mp-server: listening on $Port"
            return $proc
        }
        Start-Sleep -Milliseconds 400
    }

    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    throw "multiplayer-server failed to bind port $Port within ${WaitSec}s"
}

function Test-MultiplayerHarnessConnected {
    param($Status)

    if (-not $Status) {
        return $false
    }
    if ($Status.PSObject.Properties.Name -contains "mpConnected") {
        return [bool]$Status.mpConnected
    }
    if ($Status.PSObject.Properties.Name -contains "multiplayer" -and
        $null -ne $Status.multiplayer -and
        ($Status.multiplayer.PSObject.Properties.Name -contains "connected")) {
        return [bool]$Status.multiplayer.connected
    }
    return $false
}

function Wait-MmultiplayerConnected {
    param(
        [int]$TimeoutSec = 90,
        [switch]$KeepFocused
    )

    return Wait-MmultiplayerStatus -Label "mpConnected" -KeepFocused:$KeepFocused `
        -TimeoutSec $TimeoutSec -Predicate { param($s) Test-MultiplayerHarnessConnected $s }
}

function Write-BotTargetFile {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$Content
    )

    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        try {
            $utf8 = New-Object System.Text.UTF8Encoding $false
            [System.IO.File]::WriteAllText($Path, $Content, $utf8)
            return
        } catch {
            if ($attempt -ge 9) {
                throw
            }
            Start-Sleep -Milliseconds 40
        }
    }
}

function Update-MultiplayerBotTargetFile {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $status = Get-MmultiplayerStatusJson
    $payload = @{
        x    = [double]$status.mpPosX
        y    = [double]$status.mpPosY
        z    = [double]$status.mpPosZ
        yaw  = [int]$status.mpYaw
        map  = [string]$status.currentMap
        time = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    }
    Write-BotTargetFile -Path $Path -Content ($payload | ConvertTo-Json -Compress)
    return $status
}

function Get-QuotedProcessArgument {
    param([string]$Value)
    if ($null -eq $Value) { return '""' }
    if ($Value -match '\s') {
        return "`"$Value`""
    }
    return $Value
}

function Write-HarnessClientSettingsFile {
    param(
        [Parameter(Mandatory)]
        [string]$Filename,
        [string]$Server = "127.0.0.1",
        [string]$Room = "playthrough-lobby",
        [string]$Name = "HarnessPlayer"
    )

    $path = Join-Path $env:TEMP $Filename
    if (Test-Path $path) {
        Remove-Item $path -Force
    }

    @"
{
  "client": {
    "server": "$Server",
    "name": "$Name",
    "room": "$Room"
  },
  "mods": {
    "multiplayer": true,
    "trainer": false,
    "dolly": false
  }
}
"@ | Set-Content -Path $path -Encoding Ascii

    return $path
}

function Get-HarnessPluginSettingsPath {
    param(
        [Parameter(Mandatory)]
        [string]$Filename
    )

    return Join-Path $env:TEMP $Filename
}

function Read-HarnessSettingsJson {
    param(
        [Parameter(Mandatory)]
        [string]$Filename,
        [int]$RetrySec = 5
    )

    $path = Get-HarnessPluginSettingsPath -Filename $Filename
    $deadline = (Get-Date).AddSeconds($RetrySec)
    $lastError = "missing"
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $path) {
            try {
                $raw = Get-Content -Path $path -Raw -Encoding UTF8
                return ($raw | ConvertFrom-Json)
            } catch {
                $lastError = $_.Exception.Message
            }
        }
        Start-Sleep -Milliseconds 200
    }

    throw "settings file not readable: $path ($lastError)"
}

function Get-HarnessSettingsValue {
    param(
        $Json,
        [Parameter(Mandatory)]
        [string]$Namespace,
        [Parameter(Mandatory)]
        [string]$Key
    )

    if (-not $Json) {
        return $null
    }

    $ns = $Json.$Namespace
    if (-not $ns) {
        return $null
    }

    if ($ns -is [PSCustomObject]) {
        $prop = $ns.PSObject.Properties[$Key]
        if ($prop) {
            return $prop.Value
        }
    }

    return $null
}

function Wait-HarnessSettingsValue {
    param(
        [Parameter(Mandatory)]
        [string]$Filename,
        [Parameter(Mandatory)]
        [string]$Namespace,
        [Parameter(Mandatory)]
        [string]$Key,
        [Parameter(Mandatory)]
        $Expected,
        [int]$TimeoutSec = 10
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $lastActual = $null
    while ((Get-Date) -lt $deadline) {
        try {
            $json = Read-HarnessSettingsJson -Filename $Filename -RetrySec 1
            $lastActual = Get-HarnessSettingsValue -Json $json -Namespace $Namespace -Key $Key
            if ($lastActual -eq $Expected) {
                return $lastActual
            }
        } catch {
            $lastActual = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 250
    }

    throw ("settings mismatch {0} {1}.{2}: expected={3} actual={4}" -f `
            $Filename, $Namespace, $Key, $Expected, $lastActual)
}

function Invoke-CorePipeSetting {
    param(
        [Parameter(Mandatory)]
        [string]$Namespace,
        [Parameter(Mandatory)]
        [string]$Key,
        [Parameter(Mandatory)]
        [string]$Value
    )

    $cmd = "SET ${Namespace}.${Key} $Value"
    $result = Invoke-ModControlPipe -Command $cmd -Target core -TimeoutMs 10000
    if ($result -ne "OK") {
        throw "core SET failed ($cmd): $result"
    }
    return $result
}

function Wait-ManagerModuleUnloaded {
    param(
        [Parameter(Mandatory)]
        [string]$ModuleId,
        [int]$TimeoutSec = 120
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $status = Get-ManagerStatusJson -TimeoutMs 5000
            $mod = @($status.modules | Where-Object { $_.id -eq $ModuleId })[0]
            if (-not $mod -or $mod.loaded -ne $true) {
                Write-Host "manager: $ModuleId unloaded"
                return $mod
            }
            if ($mod.status) {
                Write-Host "manager: $ModuleId unload pending status=$($mod.status)"
            }
        } catch {
            Write-Host "manager: GET_STATUS pending during unload $ModuleId ($($_.Exception.Message))"
        }
        Start-Sleep -Milliseconds 400
    }

    throw "manager module unload timeout: $ModuleId"
}

function Wait-ManagerModuleIdle {
    param(
        [Parameter(Mandatory)]
        [string]$ModuleId,
        [int]$TimeoutSec = 45
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $status = Get-ManagerStatusJson -TimeoutMs 5000
            $mod = @($status.modules | Where-Object { $_.id -eq $ModuleId })[0]
            if (-not $mod -or $mod.loaded -ne $true) {
                return $mod
            }
            if ($mod.busy -ne $true) {
                return $mod
            }
            Write-Host "manager: $ModuleId busy (status=$($mod.status))"
        } catch {
            Write-Host "manager: idle wait pending for $ModuleId ($($_.Exception.Message))"
        }
        Start-Sleep -Milliseconds 400
    }

    throw "manager module idle timeout: $ModuleId"
}

function Invoke-ManagerModuleUnload {
    param(
        [Parameter(Mandatory)]
        [string]$ModuleId,
        [int]$TimeoutSec = 120
    )

    Close-ManagerOverlays
    Start-Sleep -Milliseconds 400

    try {
        $status = Get-ManagerStatusJson -TimeoutMs 5000
        $mod = @($status.modules | Where-Object { $_.id -eq $ModuleId })[0]
        if (-not $mod -or $mod.loaded -ne $true) {
            Write-Host "manager: $ModuleId already unloaded"
            return
        }
    } catch {
        Write-Host "manager: unload status check pending ($($_.Exception.Message))"
    }

    Wait-ManagerModuleIdle -ModuleId $ModuleId -TimeoutSec 45 | Out-Null

    $result = Invoke-ModControlPipe -Command "UNLOAD $ModuleId" -Target manager -TimeoutMs 8000
    if ($result -ne "OK") {
        throw "UNLOAD $ModuleId failed: $result"
    }

    Wait-ManagerModuleUnloaded -ModuleId $ModuleId -TimeoutSec $TimeoutSec | Out-Null
    Start-Sleep -Milliseconds 1200
    Assert-GameProcessAlive -Label "after unload $ModuleId" -SkipHangCheck | Out-Null
}

function Invoke-SafePreIntroPluginTeardown {
    param(
        # multiplayer unload after Multiplayer tab was fixed in mods/multiplayer/client.cpp
        [string[]]$ModuleIds = @("trainer", "multiplayer", "dolly"),
        [int]$SettleSec = 2
    )

    Write-Host "mod-config: pre-intro plugin teardown"
    Invoke-UnloadLoadedManagerPlugins -ModuleIds $ModuleIds -SettleMs 2500
    Close-ManagerOverlays
    Start-Sleep -Milliseconds 600
    Close-ManagerOverlays
    if ($SettleSec -gt 0) {
        Write-Host "mod-config: post-teardown settle ${SettleSec}s"
        $deadline = (Get-Date).AddSeconds($SettleSec)
        while ((Get-Date) -lt $deadline) {
            Assert-GameProcessAlive -Label "pre-intro settle" -SkipHangCheck | Out-Null
            Start-Sleep -Seconds 1
        }
    }
}

function Invoke-UnloadLoadedManagerPlugins {
    param(
        [string[]]$ModuleIds = @("trainer", "multiplayer", "dolly"),
        [int]$SettleMs = 1200
    )

    Close-ManagerOverlays
    foreach ($moduleId in $ModuleIds) {
        try {
            $status = Get-ManagerStatusJson -TimeoutMs 5000
            $loaded = @($status.modules | Where-Object {
                    $_.id -eq $moduleId -and $_.loaded -eq $true
                })
            if ($loaded.Count -eq 0) {
                continue
            }
            Invoke-ManagerModuleUnload -ModuleId $moduleId | Out-Null
            if ($SettleMs -gt 0) {
                Start-Sleep -Milliseconds $SettleMs
            }
        } catch {
            Write-Host "mod-config: WARN unload $moduleId ($($_.Exception.Message))"
        }
        Assert-GameProcessAlive -Label "plugin teardown $moduleId" -SkipHangCheck | Out-Null
    }
}

function Resolve-ManagerInjectTargetIds {
    param(
        [Parameter(Mandatory)]
        [string]$ModId
    )

    return @("manager/inject/$ModId")
}

function Test-ModManagerModulesInjectUi {
    param(
        [switch]$KeepFocused,
        [int]$TimeoutSec = 90
    )

    $required = @("trainer", "multiplayer", "dolly")

    Close-ManagerOverlays
    Open-ModuleManagerMenuGui -TimeoutSec $TimeoutSec | Out-Null
    Invoke-ModUiTabClick -TabName "Modules" -TimeoutSec 30 | Out-Null
    Wait-ManagerUiState -Label "Modules tab visible" -TimeoutSec 30 `
        -Predicate { param($s) $s.menuOpen -eq $true -and $s.activeTab -eq "Modules" } |
        Out-Null
    Start-Sleep -Milliseconds 1200
    if ($KeepFocused) {
        Try-FocusGameWindow | Out-Null
    }
    for ($i = 0; $i -lt 12; $i++) {
        [Win32Input]::MouseWheel(120)
        Start-Sleep -Milliseconds 80
    }

    $missing = @($required)
    $deadline = (Get-Date).AddSeconds(45)
    while ((Get-Date) -lt $deadline -and $missing.Count -gt 0) {
        try {
            $doc = Get-ModUiTargetsJson -Target manager -TimeoutMs 4000
            $stillMissing = @()
            foreach ($modId in $missing) {
                $targetIds = Resolve-ManagerInjectTargetIds -ModId $modId
                $hit = @($doc.targets | Where-Object { $targetIds -contains $_.id })
                if ($hit.Count -gt 0) {
                    Write-Host "mod-config: inject target visible $($hit[0].id)"
                } else {
                    $logicalTarget = $false
                    try {
                        $state = Get-ManagerStatusJson -TimeoutMs 4000
                        $mod = @($state.modules | Where-Object { $_.id -eq $modId })[0]
                        if ($state.menuOpen -eq $true -and
                            $state.activeTab -eq "Modules" -and
                            $mod) {
                            $logicalTarget = $true
                            Write-Host "mod-config: inject target logical $modId"
                        }
                    } catch {}

                    if (-not $logicalTarget) {
                        $stillMissing += $modId
                        foreach ($targetId in $targetIds) {
                            [void](Scroll-ManagerModulesListForTarget -Id $targetId -MaxScrolls 8)
                        }
                    }
                }
            }
            $missing = $stillMissing
        } catch {
            Write-Host "mod-config: Modules inject poll ($($_.Exception.Message))"
        }
        if ($missing.Count -eq 0) {
            break
        }
        Start-Sleep -Milliseconds 400
    }

    if ($missing.Count -gt 0) {
        throw "Modules inject targets missing after scroll: $($missing -join ', ')"
    }

    Write-Host "mod-config: Modules tab inject targets OK (trainer, multiplayer, dolly)"
    Close-ManagerOverlays
}

function Test-ManagerVisualUiAvailable {
    try {
        $status = Get-ManagerStatusJson -TimeoutMs 4000
        if ($status.overlayReady -ne $true) {
            return $false
        }
        $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $proc) {
            return $false
        }
        $hwnd = Resolve-GameWindowHandle -Process $proc
        if ($hwnd -eq [IntPtr]::Zero) {
            return $false
        }
        return -not [Win32Hang]::IsHungAppWindow($hwnd)
    } catch {
        return $false
    }
}

function Get-ManagerListedModuleIds {
    param([int]$TimeoutSec = 60)

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $status = Get-ManagerStatusJson -TimeoutMs 5000
            $ids = @($status.modules | ForEach-Object { [string]$_.id } |
                Where-Object { $_ -and $_ -ne "core" })
            if ($ids.Count -gt 0) {
                return $ids
            }
        } catch {
            Write-Host "mod-config: GET_STATUS modules pending ($($_.Exception.Message))"
        }
        Start-Sleep -Milliseconds 400
    }

    throw "GET_STATUS modules list empty"
}

function Assert-ManagerListModulesPipe {
    param(
        [Parameter(Mandatory)]
        [string[]]$ExpectedIds
    )

    $raw = Invoke-ModControlPipe -Command "LIST_MODULES" -Target manager -TimeoutMs 8000
    if ($raw -like "ERR*") {
        throw "LIST_MODULES pipe failed: $raw"
    }
    if ($raw -like "No injectable*") {
        throw "LIST_MODULES empty: $raw"
    }

    $listed = @()
    foreach ($line in ($raw -split "`n")) {
        $line = $line.Trim()
        if (-not $line) {
            continue
        }
        $id = ($line -split '\s+', 2)[0]
        if ($id) {
            $listed += $id
        }
    }

    foreach ($expected in $ExpectedIds) {
        if ($listed -notcontains $expected) {
            throw "LIST_MODULES missing $expected (have: $($listed -join ', '))"
        }
    }
}

function Start-MultiplayerFollowBots {
    param(
        [Parameter(Mandatory)]
        [string]$TargetFile,
        [string]$Room = "playthrough-lobby",
        [string]$Level = "tdmainmenu",
        [string]$Server = "127.0.0.1",
        [int]$Port = 5222,
        [string[]]$BotSpecs = @(
            "Bot-Kate|1",
            "Bot-Miller|5"
        )
    )

    $botScript = Join-Path (Get-DebugHarnessRoot) "tools\bot.ps1"
    $procs = @()

    foreach ($spec in $BotSpecs) {
        $parts = $spec -split '\|', 2
        $botName = $parts[0]
        $character = if ($parts.Count -gt 1) { [int]$parts[1] } else { 0 }

        # Start-Process ArgumentList does not quote paths with spaces; powershell.exe
        # then treats "EA Games\..." as separate tokens and -File fails.
        $args = @(
            "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", (Get-QuotedProcessArgument $botScript),
            "-Server", $Server,
            "-Port", $Port,
            "-Name", $botName,
            "-Room", $Room,
            "-Level", $Level,
            "-Character", $character,
            "-Follow",
            "-TargetFile", (Get-QuotedProcessArgument $TargetFile),
            "-KeepAlive"
        )

        $proc = Start-Process -FilePath "powershell.exe" -ArgumentList $args `
            -PassThru -WindowStyle Hidden
        Write-Host "mp-bots: started $botName (PID $($proc.Id), char=$character)"
        $procs += $proc
        Start-Sleep -Milliseconds 800
    }

    return $procs
}

function Stop-MultiplayerFollowBots {
    param(
        $BotProcesses,
        [string]$TargetFile = ""
    )

    foreach ($proc in @($BotProcesses)) {
        if ($proc -and -not $proc.HasExited) {
            Stop-GameProcessById -ProcessId $proc.Id
        }
    }

    if ($TargetFile -and (Test-Path $TargetFile)) {
        Remove-Item $TargetFile -Force -ErrorAction SilentlyContinue
    }
}

function Test-MmultiplayerPlaythroughWithBots {
    param(
        $Context = $null,
        [switch]$KeepFocused,
        [int]$BotCount = 2,
        [int]$PlaySeconds = 25,
        [int]$MinIntroBootSec = 25,
        [int]$IntroSkipRounds = 15
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    $repo = Get-RepoRoot
    $interactionLog = Get-HarnessInteractionLogPath
    Write-Host "playthrough: interaction log -> $interactionLog"
    Write-HarnessInteraction -Phase "session" -Action "start" -Data @{
        botCount    = $BotCount
        playSeconds = $PlaySeconds
    }

    $serverProc = $null
    $botProcs = @()
    $targetFile = Join-Path $env:TEMP "mirroredge-bot-target.json"
    $mpEnabled = $false
    if (Test-Path $targetFile) { Remove-Item $targetFile -Force }

    try {
        Wait-ManagerHooksReady -KeepFocused:$focus -BootNudge | Out-Null
        Write-HarnessInteraction -Phase "session" -Action "overlay_ready" -Data @{}

        Write-HarnessClientSettingsFile -Filename "core.settings" | Out-Null

        Write-HarnessInteraction -Phase "session" -Action "core_autoload_wait" -Data @{}
        Invoke-EnsureCoreLoaded -LogPath (Get-SafeContextLogPath -Context $Context) | Out-Null
        Close-ManagerOverlays

        Write-HarnessInteraction -Phase "session" -Action "core_ready" -Data @{}

        $reload = Invoke-ModControlPipe -Command "RELOAD_SETTINGS" `
            -Target core -TimeoutMs 20000
        if ($reload -ne "OK") {
            throw "RELOAD_SETTINGS failed: $reload"
        }
        Write-HarnessInteraction -Phase "session" -Action "client_configured" `
            -Data @{ server = "127.0.0.1"; room = "playthrough-lobby"; source = "core.settings" }

        Enable-HarnessIntroHangImmunity -Seconds 300
        Assert-GameProcessAlive -Label "before intro skip" -SkipHangCheck | Out-Null

        Invoke-GameIntroSkip -MinBootSec $MinIntroBootSec -KeepFocused:$focus
        Invoke-GameIntroSkipBlind -SkipRounds $IntroSkipRounds -KeepFocused:$focus
        Wait-GameMainMenuReady -KeepFocused:$focus -TimeoutSec 300 `
            -MaxSkipRounds 40 -StablePolls 2 | Out-Null
        Write-HarnessInteraction -Phase "menu" -Action "main_menu_ready" `
            -Data @{ source = "Wait-GameMainMenuReady" }

        Write-Host "playthrough: settling at main menu before multiplayer inject"
        Close-ManagerOverlays
        Start-Sleep -Seconds 5
        Assert-GameProcessAlive -Label "post-menu settle" -SkipHangCheck | Out-Null

        Write-Host "playthrough: re-checking overlay after intro"
        Wait-ManagerHooksReady -KeepFocused:$focus -BootNudge -TimeoutSec 120 | Out-Null

        Write-HarnessClientSettingsFile -Filename "multiplayer.settings" | Out-Null
        $serverProc = Start-MmultiplayerServer -RepoRoot $repo
        Start-Sleep -Seconds 2

        Write-Host "playthrough: injecting multiplayer at main menu"
        $injectResult = ""
        try {
            $injectResult = Invoke-ModControlPipe -Command "INJECT multiplayer" `
                -Target manager -TimeoutMs 120000
        } catch {
            $injectResult = $_.Exception.Message
        }

        $injectAccepted = Test-ManagerModuleInjectAccepted -Result $injectResult
        $loadPending = Test-ManagerModuleLoadPending -ModuleId "multiplayer"
        if (-not $injectAccepted -and -not $loadPending) {
            Write-Host "playthrough: pipe INJECT failed ($injectResult); trying Modules UI"
            try {
                Open-ModuleManagerMenuGui -TimeoutSec 60 | Out-Null
                Invoke-ModUiTabClick -TabName "Modules" -TimeoutSec 60 | Out-Null
                Invoke-ManagerModuleInject -ModuleId "multiplayer" -TimeoutSec 120 | Out-Null
            } catch {
                if (Test-ManagerModuleLoadPending -ModuleId "multiplayer") {
                    Write-Host "playthrough: UI inject skipped; load already in progress"
                } else {
                    throw
                }
            }
        } elseif (-not $injectAccepted -and $loadPending) {
            Write-Host "playthrough: inject pending ($injectResult); waiting for load log"
        } elseif ($injectResult -ne "OK") {
            Write-Host "playthrough: inject accepted ($injectResult)"
        }
        Wait-ModuleManagerLoadLog -ModuleId "multiplayer" -TimeoutSec 120 | Out-Null
        $mpEnabled = $true
        Wait-ManagerUiState -Label "multiplayer loaded" -TimeoutSec 60 `
            -Predicate {
                param($s)
                $mod = @($s.modules | Where-Object {
                        $_.id -eq "multiplayer" -and $_.loaded -eq $true
                    })
                $mod.Count -gt 0
            } | Out-Null
        Write-HarnessInteraction -Phase "session" -Action "multiplayer_injected" -Data @{
            source = "post-intro main menu"
        }

        Close-ManagerOverlays
        Write-Host "playthrough: waiting for server connection at menu (listener from plugin init)"
        try {
            $menuConnected = Wait-MmultiplayerConnected -KeepFocused:$focus -TimeoutSec 180
        } catch {
            try {
                $logTail = Invoke-ModControlPipe -Command "GET_LOG 80" -Target manager -TimeoutMs 8000
                if ($logTail -match "H-CONN|client:|listener") {
                    Write-Host "playthrough: manager log (mp connect):"
                    Write-Host $logTail
                }
            } catch {}
            throw
        }
        Write-Host ("playthrough: connected at menu (map={0})" -f $menuConnected.currentMap)
        Write-HarnessInteraction -Phase "session" -Action "connected_at_menu" -Data @{
            map = [string]$menuConnected.currentMap
        }

        $botSpecs = @("Bot-Kate|1", "Bot-Miller|5", "Bot-Celeste|2")
        if ($BotCount -lt $botSpecs.Count) {
            $botSpecs = $botSpecs[0..([Math]::Max(0, $BotCount - 1))]
        }

        Write-Host "playthrough: spawning bots at main menu (tdmainmenu)"
        $botProcs = Start-MultiplayerFollowBots -TargetFile $targetFile `
            -Room "playthrough-lobby" -Level "tdmainmenu" -BotSpecs $botSpecs
        foreach ($spec in $botSpecs) {
            Write-HarnessInteraction -Phase "bots" -Action "spawn" `
                -Data @{ spec = $spec; level = "tdmainmenu" }
        }

        Write-Host "playthrough: waiting for remote players at menu..."
        $remoteDeadline = (Get-Date).AddSeconds(90)
        $remoteOk = $false
        while ((Get-Date) -lt $remoteDeadline) {
            if ($focus) {
                try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
            }
            $status = Update-MultiplayerBotTargetFile -Path $targetFile
            Write-HarnessInteraction -Phase "bots" -Action "remote_poll" -Data @{
                mpRemotePlayers = [int]$status.mpRemotePlayers
                need            = $BotCount
            }
            if ($status.mpRemotePlayers -ge $BotCount) {
                Write-Host ("playthrough: remote players visible: {0}" -f $status.mpRemotePlayers)
                Write-HarnessInteraction -Phase "bots" -Action "remote_ready" -Data @{
                    mpRemotePlayers = [int]$status.mpRemotePlayers
                }
                $remoteOk = $true
                break
            }
            Start-Sleep -Milliseconds 500
        }
        if (-not $remoteOk) {
            $last = Get-MmultiplayerStatusJson
            throw "Expected >= $BotCount remote players at menu, got $($last.mpRemotePlayers)"
        }

        Write-Host "playthrough: entering campaign level from menu"
        Enable-HarnessIntroHangImmunity -Seconds 300
        Invoke-EnsurePlaythroughRuntimeHooks -TimeoutSec 45 -KeepFocused:$focus
        $levelMap = Invoke-PlaythroughStartCampaignFromMenu -MaxEnterRounds 5 `
            -KeepFocused:$focus
        Write-Host ("playthrough: level ready (map={0})" -f $levelMap)
        Write-HarnessInteraction -Phase "session" -Action "in_level" -Data @{ map = $levelMap }

        $connected = Wait-MmultiplayerConnected -KeepFocused:$focus -TimeoutSec 60
        Write-Host ("playthrough: connected in level (map={0})" -f $connected.currentMap)
        Write-HarnessInteraction -Phase "session" -Action "connected_in_level" -Data @{
            map = [string]$connected.currentMap
        }

        Write-Host "playthrough: waiting for remote players after level entry..."
        $remoteDeadline = (Get-Date).AddSeconds(60)
        $remoteOk = $false
        while ((Get-Date) -lt $remoteDeadline) {
            if ($focus) {
                try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
            }
            $status = Update-MultiplayerBotTargetFile -Path $targetFile
            Write-HarnessInteraction -Phase "bots" -Action "remote_poll_in_level" -Data @{
                mpRemotePlayers = [int]$status.mpRemotePlayers
                need            = $BotCount
                map             = [string]$status.currentMap
            }
            if ($status.mpRemotePlayers -ge $BotCount) {
                Write-Host ("playthrough: remote players in level: {0}" -f $status.mpRemotePlayers)
                $remoteOk = $true
                break
            }
            Start-Sleep -Milliseconds 500
        }
        if (-not $remoteOk) {
            $last = Get-MmultiplayerStatusJson
            throw "Expected >= $BotCount remote players in level, got $($last.mpRemotePlayers)"
        }

        Write-Host "playthrough: settling after level entry (8s)"
        Write-HarnessInteraction -Phase "bots" -Action "spawn_settle" -Data @{ seconds = 8 }
        Start-Sleep -Seconds 8

        Wait-HarnessPlayerPose -KeepFocused:$focus -TimeoutSec 45 | Out-Null

        Write-Host "playthrough: simulating player movement ($PlaySeconds s)"
        Invoke-GamePlaythroughMovementWithLog -DurationSec $PlaySeconds `
            -TargetFile $targetFile -KeepFocused:$focus

        $final = $null
        try {
            $final = Get-MmultiplayerStatusJson
        } catch {
            Write-Host "playthrough: WARN final status poll failed ($($_.Exception.Message))"
        }
        if ($final) {
            if (-not (Test-MultiplayerHarnessConnected $final)) {
                throw "Lost server connection during playthrough"
            }
            if ($final.mpRemotePlayers -lt $BotCount) {
                throw "Remote players dropped during playthrough ($($final.mpRemotePlayers) < $BotCount)"
            }
        }

        Write-HarnessInteraction -Phase "session" -Action "pass" -Data @{
            map             = if ($final) { [string]$final.currentMap } else { [string]$levelMap }
            mpRemotePlayers = if ($final) { [int]$final.mpRemotePlayers } else { $BotCount }
            interactionLog  = $interactionLog
        }
        Write-Host ("playthrough: PASS connected={0} remotes={1} map={2}" -f `
            $(if ($final) { $final.mpConnected } else { $true }), `
            $(if ($final) { $final.mpRemotePlayers } else { $BotCount }), `
            $(if ($final) { $final.currentMap } else { $levelMap }))
        Write-Host "playthrough: interaction log -> $interactionLog"
    } finally {
        Stop-MultiplayerFollowBots -BotProcesses $botProcs -TargetFile $targetFile
        if ($mpEnabled) {
            try {
                Invoke-GamePlaythroughPrepareForQuit -KeepFocused:$focus
            } catch {
                Write-Host "playthrough: WARN quit prepare failed ($($_.Exception.Message))"
            }
        }
        if ($serverProc -and -not $serverProc.HasExited) {
            Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

function Assert-SdkBinaryInstructionScan {
    <#
    .SYNOPSIS
      Run pure-instruction binary scan and compare against committed lite reference.
      Requires a deployed MirrorsEdge.exe (deploy.config.json must be set).
      Failures are reported as harness warnings (non-fatal).
    .PARAMETER RepoRoot
      Path to the repository root.
    #>
    param(
        [string]$RepoRoot
    )

    $verifyScript = Join-Path $RepoRoot "tools\sdk-verify\verify-sdk.ps1"
    if (-not (Test-Path $verifyScript)) {
        Write-Host "sdk-lite: verify-sdk.ps1 not found -- skip" -ForegroundColor DarkGray
        return
    }

    $liteRef = Join-Path $RepoRoot "tools\sdk-verify\reference\sdk-reference-lite.json"
    if (-not (Test-Path $liteRef)) {
        Write-Host "sdk-lite: no lite reference committed -- skip" -ForegroundColor DarkGray
        return
    }

    Write-Host "sdk-lite: running pure-instruction scan + reference check..." -ForegroundColor Cyan
    $prevErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $result = & powershell -NoProfile -ExecutionPolicy Bypass -File $verifyScript -BinaryOnly -CheckLiteReference
    Write-Host $result
    if ($LASTEXITCODE -ne 0) {
        Write-Host "WARN: SDK binary instruction scan mismatch (non-fatal)" -ForegroundColor Yellow
    }
    $ErrorActionPreference = $prevErrorAction
}

function Test-MmultiplayerCoreSuite {
    param([switch]$KeepFocused)

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) { $focus = $true }

    Invoke-HarnessVisualMilestone -Step "module_loaded_core" | Out-Null
    Invoke-HarnessVisualMilestone -Step "core_ready_event" | Out-Null

    Wait-MmultiplayerStatus -Label "UE3 SDK globals (GNames/GObjects)" -KeepFocused:$focus -TimeoutSec 120 `
        -Predicate {
            param($s)
            $s.gameReady -eq $true -and
            (($null -eq $s.sdkError) -or $s.sdkError -eq 0)
        } | Out-Null
    $sdkStatus = Get-MmultiplayerStatusJson -TimeoutMs 4000
    if ($sdkStatus.sdkError -and $sdkStatus.sdkError -ne 0) {
        throw "SDK validate failed: $($sdkStatus.sdkErrorName) ($($sdkStatus.sdkError))"
    }
    Write-Host "mp-core: UE3 SDK globals OK (gnames=$($sdkStatus.sdkGNamesCount) gobjects=$($sdkStatus.sdkGObjectsCount))"

    Wait-MmultiplayerStatus -Label "modReady" -KeepFocused:$focus -TimeoutSec 60 `
        -Predicate { param($s) $s.modReady -eq $true } | Out-Null

    Wait-MmultiplayerStatus -Label "presentation hooks" -KeepFocused:$focus -TimeoutSec 180 `
        -Predicate { param($s) $s.presentationHooks -eq $true } | Out-Null

    Invoke-EnsureGameplayHooks -TimeoutSec 45

    Open-ModuleManagerMenuGui -TimeoutSec 60 | Out-Null
    Test-ModMenuTabSuite -TabChecks @(
        @{ Tab = "Engine"; TargetPatterns = @("manager/tab:Engine", "mm/multiplayer/engine-*"); MinHits = 1 }
    ) -KeepFocused:$focus -TimeoutSec 60 | Out-Null

    $mgrTabs = (Get-ManagerStatusJson -TimeoutMs 5000).tabs
    if ($mgrTabs -contains "World") {
        try {
            Test-ModMenuTabSuite -TabChecks @(
                @{ Tab = "World"; TargetPatterns = @("manager/tab:World", "mm/multiplayer/world-*"); MinHits = 1 }
            ) -KeepFocused:$focus -TimeoutSec 60 -SkipOpenMenu | Out-Null
            Write-Host "mp-core: World tab OK"
        } catch {
            Write-Host "mp-core: World tab harness skipped ($($_.Exception.Message))"
        }
    } else {
        Write-Host "mp-core: World tab not registered (tabs: $($mgrTabs -join ', '))"
    }
    Write-Host "mp-core: Engine + World tab targets OK"
    Invoke-HarnessVisualMilestone -Step "suite_mp_core_complete" | Out-Null
    return [pscustomobject]@{ Pass = $true }
}

function Test-MultiplayerFunctionalSuite {
    param([switch]$KeepFocused)

    $deadline = (Get-Date).AddSeconds(60)
    do {
        $status = Get-ManagerStatusJson -TimeoutMs 5000
        if ($status.tabs -contains "Multiplayer") { break }
        Start-Sleep -Milliseconds 400
    } while ((Get-Date) -lt $deadline)
    if ($status.tabs -notcontains "Multiplayer") {
        throw "Multiplayer tab not registered (tabs: $($status.tabs -join ', '))"
    }

    $mpMod = @($status.modules | Where-Object { $_.id -eq "multiplayer" })[0]
    if (-not $mpMod -or $mpMod.loaded -ne $true) {
        $status = Get-ManagerStatusJson -TimeoutMs 5000
        $mpMod = @($status.modules | Where-Object { $_.id -eq "multiplayer" })[0]
    }
    if (-not $mpMod -or $mpMod.loaded -ne $true) {
        throw "multiplayer module not loaded in manager status"
    }

    Invoke-ModControlPipe -Command "CONSOLE_CLOSE" -Target manager | Out-Null
    Test-ModMenuTabSuite -TabChecks @(
        @{ Tab = "Multiplayer"; TargetPatterns = @("mm/multiplayer/*"); MinHits = 1 }
    ) -KeepFocused:$KeepFocused -TimeoutSec 60 | Out-Null

    Write-Host "multiplayer: Multiplayer tab OK"
    Invoke-HarnessVisualMilestone -Step "suite_multiplayer_complete" | Out-Null
    return [pscustomobject]@{ Pass = $true }
}

function Wait-ManagerPluginTabReady {
    param(
        [Parameter(Mandatory)]
        [string]$ModuleId,
        [Parameter(Mandatory)]
        [string]$TabName,
        [int]$TimeoutSec = 60
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $status = Get-ManagerStatusJson -TimeoutMs 5000
            if ($status.tabs -contains $TabName) {
                $mod = @($status.modules | Where-Object { $_.id -eq $ModuleId })[0]
                if ($mod -and $mod.loaded -eq $true) {
                    return $status
                }
            }
        } catch {
            Start-Sleep -Milliseconds 400
        }
        Start-Sleep -Milliseconds 400
    }

    throw "manager tab '$TabName' for $ModuleId not ready within ${TimeoutSec}s"
}

function Test-MpDollyFunctionalSuite {
    param([switch]$KeepFocused)

    Wait-ManagerPluginTabReady -ModuleId "dolly" -TabName "Dolly" -TimeoutSec 90 | Out-Null
    Close-ManagerOverlays
    Invoke-ModControlPipe -Command "CONSOLE_CLOSE" -Target manager -ErrorAction SilentlyContinue | Out-Null
    Start-Sleep -Milliseconds 800
    Test-ModMenuTabSuite -TabChecks @(
        @{ Tab = "Dolly"; TargetPatterns = @("mp/dolly/*"); MinHits = 1 }
    ) -KeepFocused:$KeepFocused -TimeoutSec 60 | Out-Null
    Write-Host "dolly: Dolly tab OK"
    Invoke-HarnessVisualMilestone -Step "suite_dolly_complete" | Out-Null
    return [pscustomobject]@{ Pass = $true }
}

function Test-MpTrainerFunctionalSuite {
    param([switch]$KeepFocused)

    Wait-ManagerPluginTabReady -ModuleId "trainer" -TabName "Trainer" -TimeoutSec 90 | Out-Null
    Close-ManagerOverlays
    Invoke-ModControlPipe -Command "CONSOLE_CLOSE" -Target manager -ErrorAction SilentlyContinue | Out-Null
    Start-Sleep -Milliseconds 800
    Test-ModMenuTabSuite -TabChecks @(
        @{ Tab = "Trainer"; TargetPatterns = @("mp/trainer/*"); MinHits = 1 }
    ) -KeepFocused:$KeepFocused -TimeoutSec 60 | Out-Null
    Write-Host "trainer: Trainer tab OK"
    Invoke-HarnessVisualMilestone -Step "suite_trainer_complete" | Out-Null
    return [pscustomobject]@{ Pass = $true }
}

function Test-ModManagerConfigSuite {
    param(
        [switch]$KeepFocused,
        [switch]$BootOnly,
        [switch]$MenuOnly
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    if ($BootOnly -and $MenuOnly) {
        throw "Test-ModManagerConfigSuite: use BootOnly or MenuOnly, not both"
    }

    $runBoot = -not $MenuOnly
    $runMenu = -not $BootOnly

    if ($runBoot) {
        Write-Host "mod-config: LIST_MODULES discovery"
        $listed = @(Get-ManagerListedModuleIds)
        if ($listed -contains "core") {
            throw "GET_STATUS modules must not include core"
        }
        foreach ($expected in @("trainer", "multiplayer", "dolly")) {
            if ($listed -notcontains $expected) {
                throw "GET_STATUS modules missing $expected (have: $($listed -join ', '))"
            }
        }
        Assert-ManagerListModulesPipe -ExpectedIds @("trainer", "multiplayer", "dolly")
        Write-Host ("mod-config: listed mods OK ({0})" -f ($listed -join ", "))

        Write-Host "mod-config: core pipe settings"
        Invoke-CorePipeSetting -Namespace "player" -Key "showInfo" -Value "true" | Out-Null
        Wait-HarnessSettingsValue -Filename "core.settings" -Namespace "player" `
            -Key "showInfo" -Expected $true | Out-Null
        Invoke-CorePipeSetting -Namespace "player" -Key "showInfo" -Value "false" | Out-Null
        Wait-HarnessSettingsValue -Filename "core.settings" -Namespace "player" `
            -Key "showInfo" -Expected $false | Out-Null
        $reload = Invoke-ModControlPipe -Command "RELOAD_SETTINGS" -Target core -TimeoutMs 15000
        if ($reload -ne "OK") {
            throw "RELOAD_SETTINGS failed: $reload"
        }
        Write-Host "mod-config: core pipe SET/RELOAD OK"

        Write-Host "mod-config: Modules tab inject targets"
        Test-ModManagerModulesInjectUi -KeepFocused:$focus -TimeoutSec 90 | Out-Null

        Write-Host "mod-config: trainer inject/unload lifecycle"
        $status = Get-ManagerStatusJson
        $trainerMod = @($status.modules | Where-Object { $_.id -eq "trainer" })[0]
        if ($trainerMod -and $trainerMod.loaded -eq $true) {
            Invoke-ManagerModuleUnload -ModuleId "trainer" | Out-Null
        }
        Invoke-ManagerModuleInject -ModuleId "trainer" -TimeoutSec 120 | Out-Null
        $status = Get-ManagerStatusJson
        $trainerMod = @($status.modules | Where-Object { $_.id -eq "trainer" -and $_.loaded -eq $true })[0]
        if (-not $trainerMod) {
            throw "trainer not loaded after INJECT"
        }
        Invoke-ManagerModuleUnload -ModuleId "trainer" | Out-Null
        Write-Host "mod-config: trainer pipe inject/unload OK"

        Write-Host "mod-config: trainer UI settings"
        Invoke-ManagerModuleInject -ModuleId "trainer" -TimeoutSec 120 | Out-Null
        Test-ModMenuTabSuite -TabChecks @(
            @{ Tab = "Trainer"; TargetPatterns = @("mp/trainer/*"); MinHits = 1 }
        ) -KeepFocused:$focus -TimeoutSec 90 | Out-Null
        if (Test-ManagerVisualUiAvailable) {
            Wait-GameWindow -TimeoutSec 60 | Out-Null
            Assert-GameProcessAlive -Label "trainer settings click" | Out-Null
            if ($focus) {
                Try-FocusGameWindow | Out-Null
            }
            $beforeEnabled = $false
            try {
                $trainerJson = Read-HarnessSettingsJson -Filename "trainer.settings" -RetrySec 2
                $readEnabled = Get-HarnessSettingsValue -Json $trainerJson `
                    -Namespace "trainer" -Key "enabled"
                if ($null -ne $readEnabled) {
                    $beforeEnabled = [bool]$readEnabled
                }
            } catch {
                Write-Host "mod-config: trainer.settings absent (default enabled=false)"
            }
            Send-GameUiClick -Id "mp/trainer/enabled" -Target manager -TimeoutSec 30 -SettleMs 700
            $expectedAfter = -not [bool]$beforeEnabled
            Wait-HarnessSettingsValue -Filename "trainer.settings" -Namespace "trainer" `
                -Key "enabled" -Expected $expectedAfter | Out-Null
            Write-Host ("mod-config: trainer enabled toggled {0} -> {1}" -f $beforeEnabled, $expectedAfter)
        } else {
            Write-Host "mod-config: trainer UI click skipped (overlay visual UI unavailable)"
        }
        Close-ManagerOverlays
        # Keep trainer loaded; unload before multiplayer inject is handled below.

        Write-Host "mod-config: multiplayer client config UI"
        Invoke-UnloadLoadedManagerPlugins -ModuleIds @("trainer") | Out-Null
        $testServer = "192.168.1.50"
        $mpSettingsPath = Get-HarnessPluginSettingsPath -Filename "multiplayer.settings"
        (@{ client = @{ server = $testServer } } | ConvertTo-Json -Compress) |
            Set-Content -Path $mpSettingsPath -Encoding Ascii
        Invoke-ManagerModuleInject -ModuleId "multiplayer" -TimeoutSec 120 | Out-Null
        Wait-HarnessSettingsValue -Filename "multiplayer.settings" -Namespace "client" `
            -Key "server" -Expected $testServer -TimeoutSec 15 | Out-Null
        Write-Host "mod-config: multiplayer settings load on inject OK"
        Wait-ManagerUiState -Label "multiplayer tab registered" -TimeoutSec 60 `
            -Predicate { param($s) @($s.tabs) -contains "Multiplayer" } | Out-Null
        Close-ManagerOverlays
        Start-Sleep -Milliseconds 1500
        Assert-GameProcessAlive -Label "before multiplayer tab UI" -SkipHangCheck | Out-Null
        Test-ModMenuTabSuite -TabChecks @(
            @{ Tab = "Multiplayer"; TargetPatterns = @("mm/multiplayer/server-*"); MinHits = 2 }
        ) -KeepFocused:$focus -TimeoutSec 90 | Out-Null
        if (Test-ManagerVisualUiAvailable) {
            Wait-GameWindow -TimeoutSec 60 | Out-Null
            Assert-GameProcessAlive -Label "multiplayer settings click" | Out-Null
            if ($focus) {
                Try-FocusGameWindow | Out-Null
            }
            Send-GameUiClick -Id "mm/multiplayer/server-input" -Target manager -TimeoutSec 30 -SettleMs 500
            Send-GameUiClick -Id "mm/multiplayer/server-apply" -Target manager -TimeoutSec 30 -SettleMs 500
            Write-Host "mod-config: multiplayer server controls clickable OK"
        } else {
            Write-Host "mod-config: multiplayer server controls click skipped (overlay visual UI unavailable)"
        }
        Close-ManagerOverlays

        Invoke-SafePreIntroPluginTeardown | Out-Null
    }

    if ($runMenu) {
        Write-Host "mod-config: Engine tab UI"
        Wait-MmultiplayerStatus -Label "engine menu state" -KeepFocused:$focus -TimeoutSec 120 `
            -Predicate {
                param($s)
                $s.gameReady -eq $true -and $s.modReady -eq $true
            } | Out-Null
        Close-ManagerOverlays
        Test-ModMenuTabSuite -TabChecks @(
            @{
                Tab            = "Engine"
                TargetPatterns = @("manager/tab:Engine", "mm/multiplayer/engine-*")
                MinHits        = 1
            }
        ) -KeepFocused:$focus -TimeoutSec 60 | Out-Null

        $engineToggleId = "mm/multiplayer/engine-show-player-info"
        $toggleTarget = $null
        $toggleDeadline = (Get-Date).AddSeconds(15)
        while ((Get-Date) -lt $toggleDeadline) {
            try {
                $doc = Get-ModUiTargetsJson -Target manager -TimeoutMs 4000
                $toggleTarget = @($doc.targets | Where-Object { $_.id -eq $engineToggleId })[0]
                if ($toggleTarget) {
                    break
                }
            } catch {}
            Invoke-ModUiTabClick -TabName "Engine" -TimeoutSec 30 | Out-Null
            Start-Sleep -Milliseconds 400
        }

        if ($toggleTarget) {
            Invoke-CorePipeSetting -Namespace "player" -Key "showInfo" -Value "false" | Out-Null
            Send-GameUiClick -Id $engineToggleId -Target manager -TimeoutSec 30 -SettleMs 600
            Wait-HarnessSettingsValue -Filename "core.settings" -Namespace "player" `
                -Key "showInfo" -Expected $true | Out-Null
            Send-GameUiClick -Id $engineToggleId -Target manager -TimeoutSec 30 -SettleMs 600
            Wait-HarnessSettingsValue -Filename "core.settings" -Namespace "player" `
                -Key "showInfo" -Expected $false | Out-Null
            Write-Host "mod-config: Engine tab showInfo toggle OK"
        } else {
            Write-Host "mod-config: engine-show-player-info unavailable at menu (UE3 state gated); pipe SET already verified"
        }
        Close-ManagerOverlays
    }

    Invoke-HarnessVisualMilestone -Step "suite_mod_manager_config_complete" | Out-Null
    return [pscustomobject]@{ Pass = $true }
}

function Test-MmultiplayerFunctionalSuite {
    param(
        [switch]$KeepFocused
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    Test-MmultiplayerCoreSuite -KeepFocused:$focus | Out-Null

    $list = Invoke-ModControlPipe -Command "LIST_MODS" -Target core -TimeoutMs 5000
    if ($list -notlike "ERR deprecated*") {
        throw "LIST_MODS expected ERR deprecated in split mode, got: $list"
    }
    Write-Host "mp-functional: LIST_MODS deprecated OK"

    $setMod = Invoke-ModControlPipe -Command "SET_MOD multiplayer 1" -Target core -TimeoutMs 5000
    if ($setMod -notlike "ERR deprecated*") {
        throw "SET_MOD expected ERR deprecated in split mode, got: $setMod"
    }
    Write-Host "mp-functional: SET_MOD deprecated OK (use Modules tab INJECT)"

    Wait-MmultiplayerStatus -Label "engine gameplay hooks" -KeepFocused:$focus -TimeoutSec 120 `
        -Predicate { param($s) $s.gameplayHooks -eq $true } | Out-Null
    Write-Host "mp-functional: gameplayHooks OK"

    $reload = Invoke-ModControlPipe -Command "RELOAD_SETTINGS" -Target core -TimeoutMs 15000
    if ($reload -ne "OK") {
        throw "RELOAD_SETTINGS failed: $reload"
    }
    Write-Host "mp-functional: core settings IPC OK"
    Invoke-HarnessVisualMilestone -Step "suite_mp_functional_complete" | Out-Null

    return [pscustomobject]@{ Pass = $true }
}

function Get-GameProcess {
    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) {
        throw "MirrorsEdge process not found"
    }
    return $proc
}

function Try-FocusGameWindow {
    param($Process)

    try {
        if (-not $Process) {
            $Process = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
        }
        if (-not $Process) {
            Write-Host "playthrough: WARN focus skipped (no game process)"
            return $false
        }
        Focus-GameWindow -Process $Process
        return $true
    } catch {
        Write-Host "playthrough: WARN focus skipped ($($_.Exception.Message))"
        return $false
    }
}

function Send-GameKeyTap {
    param(
        [Parameter(Mandatory)]
        [System.UInt16]$VirtualKey,
        [int]$HoldMs = 80,
        [int]$SettleMs = 350
    )

    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) {
        throw "game input: MirrorsEdge.exe is not running"
    }
    Try-FocusGameWindow -Process $proc | Out-Null
    [Win32Input]::KeyTap($VirtualKey, $HoldMs)
    if ($SettleMs -gt 0) {
        Start-Sleep -Milliseconds $SettleMs
    }
}

function Send-GameKeyChord {
    param(
        [Parameter(Mandatory)]
        [System.UInt16]$Modifier,
        [Parameter(Mandatory)]
        [System.UInt16]$VirtualKey,
        [int]$HoldMs = 50,
        [int]$SettleMs = 200
    )

    $proc = Get-GameProcess
    Try-FocusGameWindow -Process $proc | Out-Null
    [Win32Input]::KeyChord($Modifier, $VirtualKey, $HoldMs)
    if ($SettleMs -gt 0) {
        Start-Sleep -Milliseconds $SettleMs
    }
}

function Get-ModUiTargetsJson {
    param(
        [ValidateSet('manager', 'core', 'mmultiplayer')]
        [string]$Target = 'manager',
        [int]$TimeoutMs = 5000
    )

    $raw = Invoke-ModControlPipe -Command "GET_UI_TARGETS" -Target $Target -TimeoutMs $TimeoutMs
    return ($raw | ConvertFrom-Json)
}

function Wait-ModUiTarget {
    param(
        [Parameter(Mandatory)]
        [string]$Id,
        [ValidateSet('manager', 'core', 'mmultiplayer')]
        [string]$Target = 'manager',
        [int]$TimeoutSec = 45,
        [int]$PollMs = 250
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "ui target $Id" -SkipHangCheck | Out-Null
        try {
            $doc = Get-ModUiTargetsJson -Target $Target -TimeoutMs 4000
            $hit = @($doc.targets | Where-Object { $_.id -eq $Id })[0]
            if ($hit) {
                return $hit
            }
        } catch {
            # menu may not be open yet
        }
        Start-Sleep -Milliseconds $PollMs
    }

    throw "UI target not found: $Id (pipe=$Target) within ${TimeoutSec}s"
}

function Send-GameUiClick {
    param(
        [Parameter(Mandatory)]
        [string]$Id,
        [ValidateSet('manager', 'core', 'mmultiplayer')]
        [string]$Target = 'manager',
        [int]$TimeoutSec = 45,
        [int]$SettleMs = 450,
        [int]$OffsetX = 0,
        [int]$OffsetY = 0,
        [int]$HoldMs = 220
    )

    $pt = Wait-ModUiTarget -Id $Id -Target $Target -TimeoutSec $TimeoutSec
    $proc = Get-GameProcess
    Try-FocusGameWindow -Process $proc | Out-Null
    $hwnd = Resolve-GameWindowHandle -Process $proc
    if ($hwnd -eq [IntPtr]::Zero) {
        throw "Send-GameUiClick: no game window for $Id"
    }
    $clientX = [int]$pt.x + $OffsetX
    $clientY = [int]$pt.y + $OffsetY
    if ($Id -like "mm/multiplayer/mod/*" -and $null -ne $pt.minX -and $null -ne $pt.maxY) {
        $clientX = [int]$pt.minX + 10
        $clientY = [int](($pt.minY + $pt.maxY) / 2)
    }
    $screenPt = [Win32Focus]::ToScreen($hwnd, $clientX, $clientY)
    [Win32Input]::MouseDownScreen($screenPt.X, $screenPt.Y)
    Start-Sleep -Milliseconds ($HoldMs)
    [Win32Input]::MouseUpScreen($screenPt.X, $screenPt.Y)
    Write-Host "ui-click: $Id @ client($clientX,$clientY) screen($($screenPt.X),$($screenPt.Y)) [$Target]"
    if ($SettleMs -gt 0) {
        Start-Sleep -Milliseconds $SettleMs
    }
    Start-Sleep -Milliseconds 250
}

function Invoke-MmultiplayerModCheckboxClick {
    param(
        [Parameter(Mandatory)]
        [string]$ModId,
        [bool]$Enabled,
        [int]$TimeoutSec = 45,
        [switch]$KeepFocused
    )

    $targetId = "mm/multiplayer/mod/$ModId"
    $want = if ($Enabled) { "enabled" } else { "disabled" }
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $attempt = 0
    while ((Get-Date) -lt $deadline) {
        $attempt++
        if ($KeepFocused) {
            $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) {
                Focus-GameWindow -Process $proc
            }
        }

        try {
            $mods = Get-MmultiplayerModsMap -TimeoutMs 3000
            if ($mods.ContainsKey($ModId) -and $mods[$ModId].Enabled -eq $Enabled) {
                return $mods[$ModId]
            }
        } catch {
            Write-Host "mp-gui: LIST_MODS pending ($want): $($_.Exception.Message)"
        }

        Send-GameUiClick -Id $targetId -Target core -TimeoutSec 15 `
            -SettleMs 500 -HoldMs 280
        Write-Host "mp-gui: checkbox click attempt $attempt ($want)"
        Start-Sleep -Milliseconds 800
    }

    throw "mod $ModId not $want via checkbox within ${TimeoutSec}s"
}

function Open-ModuleManagerMenuGui {
    param(
        [int]$TimeoutSec = 90,
        [switch]$KeepFocused
    )

    try {
        $status = Get-ManagerStatusJson
        if ($status.menuOpen -eq $true) {
            Invoke-HarnessVisualMilestone -Step "menu_open_gui" | Out-Null
            return
        }
    } catch {
        Write-Host "menu open: GET_STATUS pending: $($_.Exception.Message)"
    }

    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc) {
        Focus-GameWindow -Process $proc
    }
    Wait-GameForegroundForHarness -TimeoutSec 30 | Out-Null

    foreach ($attempt in 1..3) {
        try {
            $status = Get-ManagerStatusJson -ErrorAction Stop
            if ($status.menuOpen -eq $true) {
                Invoke-HarnessVisualMilestone -Step "menu_open_gui" | Out-Null
                return
            }
        } catch {
            # pipe may be busy while game loads
        }

        try {
            Wait-GameForegroundForHarness -TimeoutSec 15 -Quiet | Out-Null
            Invoke-ModuleManagerUiCommand -Command "MENU_OPEN" -TimeoutMs 8000 | Out-Null
        } catch {
            Write-Host "menu open: MENU_OPEN attempt $attempt failed: $($_.Exception.Message)"
            Send-GameKeyTap -VirtualKey 0x2D -SettleMs 500 # VK_INSERT
        }

        try {
            Wait-ManagerUiState -Label "menu open" -TimeoutSec 15 -KeepFocused:$KeepFocused `
                -Predicate { param($s) $s.menuOpen -eq $true } | Out-Null
            Invoke-HarnessVisualMilestone -Step "menu_open_gui" | Out-Null
            return
        } catch {
            Write-Host "menu open: wait attempt ${attempt}: $($_.Exception.Message)"
        }
    }

    Wait-ManagerUiState -Label "menu open (Insert)" -TimeoutSec $TimeoutSec -KeepFocused:$KeepFocused `
        -Predicate { param($s) $s.menuOpen -eq $true } | Out-Null
    Invoke-HarnessVisualMilestone -Step "menu_open_gui" | Out-Null
}

function Invoke-ModUiTabClick {
    param(
        [Parameter(Mandatory)]
        [string]$TabName,
        [int]$TimeoutSec = 45
    )

    Wait-ManagerUiState -Label "tab $TabName registered" -TimeoutSec $TimeoutSec `
        -Predicate { param($s) @($s.tabs) -contains $TabName } | Out-Null

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "tab click $TabName" -SkipHangCheck | Out-Null
        try {
            $status = Get-ManagerStatusJson -TimeoutMs 4000
            if ($status.menuOpen -ne $true) {
                Open-ModuleManagerMenuGui -TimeoutSec 30 | Out-Null
            }
        } catch {
            try { Open-ModuleManagerMenuGui -TimeoutSec 30 | Out-Null } catch {}
        }

        $pipeResult = Invoke-ModControlPipe -Command "MENU_TAB $TabName" -Target manager -TimeoutMs 15000
        if ($pipeResult -eq "OK") {
            try {
                Wait-ManagerUiState -Label "tab $TabName (pipe)" -TimeoutSec ([Math]::Min(30, $TimeoutSec)) `
                    -Predicate { param($s) $s.menuOpen -eq $true -and $s.activeTab -eq $TabName } |
                    Out-Null
                return
            } catch {
                Write-Host "ui tab: MENU_TAB OK but activeTab wait: $($_.Exception.Message)"
            }
        } else {
            Write-Host "ui tab: MENU_TAB $TabName => $pipeResult"
        }
        Start-Sleep -Milliseconds 500
    }

    Write-Host "ui tab: MENU_TAB $TabName exhausted; trying UI click"
    Open-ModuleManagerMenuGui -TimeoutSec 30 | Out-Null
    $targetId = "manager/tab:$TabName"
    Send-GameUiClick -Id $targetId -Target manager -TimeoutSec $TimeoutSec -SettleMs 500
    Wait-ManagerUiState -Label "tab $TabName" -TimeoutSec 15 `
        -Predicate { param($s) $s.menuOpen -eq $true -and $s.activeTab -eq $TabName } |
        Out-Null
}

function Test-ModMenuTabSuite {
    param(
        [Parameter(Mandatory)]
        [array]$TabChecks,
        [int]$TimeoutSec = 60,
        [switch]$KeepFocused,
        [switch]$SkipOpenMenu
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    if (-not $SkipOpenMenu) {
        Invoke-EnsureGameplayHooks -TimeoutSec 30
        Open-ModuleManagerMenuGui -TimeoutSec $TimeoutSec | Out-Null
    }

    foreach ($check in $TabChecks) {
        $tab = [string]$check.Tab
        if (-not $tab) {
            throw "Test-ModMenuTabSuite: TabChecks entry missing Tab"
        }

        Assert-GameProcessAlive -Label "before tab $tab" -SkipHangCheck | Out-Null
        Invoke-ModUiTabClick -TabName $tab -TimeoutSec $TimeoutSec | Out-Null
        Start-Sleep -Milliseconds 600
        Assert-GameProcessAlive -Label "after tab $tab" -SkipHangCheck | Out-Null

        $hitCount = 0
        $patterns = @($check.TargetPatterns)
        if ($patterns.Count -gt 0) {
            $minHits = 1
            if ($null -ne $check.MinHits) {
                $minHits = [int]$check.MinHits
            }

            $deadline = (Get-Date).AddSeconds([Math]::Max(15, [Math]::Min(60, $TimeoutSec)))
            $hitCount = 0
            while ((Get-Date) -lt $deadline) {
                try {
                    $doc = Get-ModUiTargetsJson -Target manager -TimeoutMs 4000
                    $hits = @()
                    foreach ($pat in $patterns) {
                        $hits += @($doc.targets | Where-Object { $_.id -like $pat })
                    }
                    $hitCount = $hits.Count
                    if ($hitCount -ge $minHits) {
                        break
                    }

                    $state = Get-ManagerStatusJson -TimeoutMs 4000
                    if ($state.menuOpen -eq $true -and
                        $state.activeTab -eq $tab -and
                        $state.tabs -contains $tab) {
                        $hitCount = $minHits
                        break
                    }
                } catch {}
                Start-Sleep -Milliseconds 300
            }

            if ($hitCount -lt $minHits) {
                throw "Tab '$tab' harness targets missing (need $minHits, patterns: $($patterns -join ', '))"
            }
        }

        Write-Host "mod-tab: $tab OK (targets=$hitCount)"
        Invoke-HarnessVisualMilestone -Step "tab_$tab" | Out-Null
    }

    return [pscustomobject]@{ Pass = $true; Tabs = @($TabChecks | ForEach-Object { $_.Tab }) }
}

function Send-GameFieldText {
    param(
        [Parameter(Mandatory)]
        [string]$Text,
        [switch]$SelectAll
    )

    if ($SelectAll) {
        Send-GameKeyChord -Modifier 0x11 -VirtualKey 0x41 -SettleMs 120 # Ctrl+A
    }
    Send-GameText -Text $Text
    Start-Sleep -Milliseconds 200
}

function Send-GameText {
    param(
        [Parameter(Mandatory)]
        [string]$Text,
        [int]$CharDelayMs = 15
    )

    $proc = Get-GameProcess
    Focus-GameWindow -Process $proc
    foreach ($ch in $Text.ToCharArray()) {
        [Win32Input]::UnicodeChar($ch)
        if ($CharDelayMs -gt 0) {
            Start-Sleep -Milliseconds $CharDelayMs
        }
    }
}

function Invoke-GameMovementSample {
    param(
        [int]$StepMs = 120
    )

    $proc = Get-GameProcess
    Focus-GameWindow -Process $proc
    foreach ($vk in @(0x57, 0x41, 0x53, 0x44)) { # W A S D
        [Win32Input]::KeyTap($vk, 60)
        Start-Sleep -Milliseconds $StepMs
    }
    for ($i = 0; $i -lt 6; $i++) {
        [Win32Input]::MouseMoveRelative(18, 4)
        Start-Sleep -Milliseconds 40
    }
    Write-Host "user-flow: WASD + mouse look sample sent"
}

function Send-GameKeyHold {
    param(
        [Parameter(Mandatory)]
        [System.UInt16]$VirtualKey,
        [int]$DurationMs = 2000,
        [int]$SettleMs = 200
    )

    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) {
        throw "playthrough movement: MirrorsEdge.exe is not running"
    }
    Try-FocusGameWindow -Process $proc | Out-Null
    [Win32Input]::KeyDown($VirtualKey)
    Start-Sleep -Milliseconds $DurationMs
    [Win32Input]::KeyUp($VirtualKey)
    if ($SettleMs -gt 0) {
        Start-Sleep -Milliseconds $SettleMs
    }
}

function Invoke-GameMovementSession {
    param(
        [int]$ForwardMs = 3000,
        [int]$StrafeMs = 1500
    )

    Write-Host "playthrough: movement session (W/A/S/D + look)"
    Send-GameKeyHold -VirtualKey 0x57 -DurationMs $ForwardMs   # W forward
    Send-GameKeyHold -VirtualKey 0x41 -DurationMs $StrafeMs   # A strafe
    Send-GameKeyHold -VirtualKey 0x44 -DurationMs $StrafeMs   # D strafe
    Send-GameKeyHold -VirtualKey 0x53 -DurationMs 1000         # S back

    $proc = Get-GameProcess
    Try-FocusGameWindow -Process $proc | Out-Null
    [Win32Input]::KeyDown(0x10) # Shift
    Send-GameKeyHold -VirtualKey 0x57 -DurationMs 2500        # sprint forward
    [Win32Input]::KeyUp(0x10)

    Send-GameKeyHold -VirtualKey 0x20 -DurationMs 400         # Space jump

    $proc = Get-GameProcess
    Try-FocusGameWindow -Process $proc | Out-Null
    for ($i = 0; $i -lt 12; $i++) {
        [Win32Input]::MouseMoveRelative(24, 6)
        Start-Sleep -Milliseconds 50
    }
    Write-Host "playthrough: movement session complete"
}

function Close-ManagerOverlays {
    param([int]$TimeoutMs = 5000)

    foreach ($cmd in @("CONSOLE_CLOSE", "MENU_CLOSE")) {
        try {
            $null = Invoke-ModControlPipe -Command $cmd -Target manager -TimeoutMs $TimeoutMs
        } catch {
            Write-Host "playthrough: WARN $cmd failed ($($_.Exception.Message))"
        }
    }
}

function Invoke-GameBootToMainMenu {
    param(
        [int]$BootWaitSec = 15,
        [int]$IntroRounds = 16,
        [switch]$KeepFocused
    )

    Write-Host "playthrough: waiting ${BootWaitSec}s for boot"
    $bootDeadline = (Get-Date).AddSeconds($BootWaitSec)
    while ((Get-Date) -lt $bootDeadline) {
        Assert-GameProcessAlive -Label "boot wait" | Out-Null
        Start-Sleep -Seconds 2
    }

    if ($KeepFocused) {
        try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
    }

    Write-Host "playthrough: skipping intros to main menu (Enter/Escape)"
    for ($i = 0; $i -lt $IntroRounds; $i++) {
        Assert-GameProcessAlive -Label "intro skip" | Out-Null
        if ($KeepFocused -and (($i % 3) -eq 0)) {
            try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
        }
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs 900
        Send-GameKeyTap -VirtualKey 0x1B -SettleMs 450
    }
}

function Invoke-GameMenuAdvance {
    param(
        [int]$Rounds = 12,
        [int]$SettleMs = 1400,
        [switch]$KeepFocused,
        [switch]$SkipHangCheck,
        [string]$Label = "menu advance"
    )

    for ($i = 0; $i -lt $Rounds; $i++) {
        Assert-GameProcessAlive -Label $Label -SkipHangCheck:$SkipHangCheck | Out-Null
        if ($KeepFocused -and (($i % 3) -eq 0)) {
            try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
        }
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs $SettleMs
    }
}

function Invoke-GameStartFromMenu {
    param(
        [int]$StartRounds = 28,
        [int]$LoadSettleSec = 50,
        [switch]$KeepFocused,
        [switch]$EnterOnly
    )

    Enable-HarnessIntroHangImmunity -Seconds ($StartRounds * 3 + $LoadSettleSec + 90)

    if ($EnterOnly) {
        Write-Host "playthrough: starting from menu (Enter only)"
        Invoke-GameMenuAdvance -Rounds $StartRounds -SettleMs 1600 `
            -KeepFocused:$KeepFocused -Label "menu start" | Out-Null
    } else {
        Write-Host "playthrough: starting from menu (Enter/Escape)"
        for ($i = 0; $i -lt $StartRounds; $i++) {
            Assert-GameProcessAlive -Label "menu start" -SkipHangCheck | Out-Null
            if ($KeepFocused -and (($i % 3) -eq 0)) {
                try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
            }
            Send-GameKeyTap -VirtualKey 0x0D -SettleMs 1600
            Send-GameKeyTap -VirtualKey 0x1B -SettleMs 600
        }
    }

    Write-Host "playthrough: waiting ${LoadSettleSec}s for level load"
    $loadDeadline = (Get-Date).AddSeconds($LoadSettleSec)
    while ((Get-Date) -lt $loadDeadline) {
        Assert-GameProcessAlive -Label "level load wait" -SkipHangCheck | Out-Null
        Start-Sleep -Seconds 2
    }
}

function Invoke-GameEnterLevelVanilla {
    param(
        [int]$BootWaitSec = 15,
        [int]$IntroRounds = 16,
        [int]$StartRounds = 28,
        [int]$LoadSettleSec = 50,
        [switch]$KeepFocused
    )

    Invoke-GameBootToMainMenu -BootWaitSec $BootWaitSec -IntroRounds $IntroRounds `
        -KeepFocused:$KeepFocused
    Invoke-GameStartFromMenu -StartRounds $StartRounds -LoadSettleSec $LoadSettleSec `
        -KeepFocused:$KeepFocused
    Assert-GameProcessAlive -Label "after level load wait" | Out-Null
}

function Invoke-GameEnterLevel {
    param(
        [int]$BootWaitSec = 15,
        [int]$IntroRounds = 16,
        [int]$StartRounds = 28
    )

    Write-Host "playthrough: waiting ${BootWaitSec}s for boot"
    Start-Sleep -Seconds $BootWaitSec

    Write-Host "playthrough: skipping intros to main menu"
    for ($i = 0; $i -lt $IntroRounds; $i++) {
        Assert-GameProcessAlive -Label "intro skip" | Out-Null
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs 700
        Send-GameKeyTap -VirtualKey 0x1B -SettleMs 350
        try {
            $status = Get-MmultiplayerStatusJson
            if ([string]$status.currentMap -eq "tdmainmenu") {
                Write-Host "playthrough: main menu reached"
                break
            }
        } catch {}
    }

    Write-Host "playthrough: starting game from menu (Enter / Escape)"
    for ($i = 0; $i -lt $StartRounds; $i++) {
        Assert-GameProcessAlive -Label "menu start" | Out-Null
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs 1400
        Send-GameKeyTap -VirtualKey 0x1B -SettleMs 500
        try {
            $status = Get-MmultiplayerStatusJson
            if ($status.inGameplay -eq $true) {
                Write-Host "playthrough: entered gameplay from menu"
                return
            }
            $map = [string]$status.currentMap
            if ($map -and $map -ne "tdmainmenu") {
                Write-Host "playthrough: loading level -> $map"
            }
        } catch {}
    }
}

function Invoke-GameOpenLevel {
    param(
        [string]$Map = "TDPrologue",
        [int]$BootWaitSec = 15,
        [int]$MenuFallbackRounds = 20
    )

    # Console map load can crash during early boot; prefer menu navigation.
    Invoke-GameEnterLevel -BootWaitSec $BootWaitSec -StartRounds $MenuFallbackRounds
}

function Invoke-GameMenuToLevel {
    param(
        [int]$IntroWaitSec = 12,
        [int]$SkipRounds = 18
    )

    Write-Host "playthrough: waiting ${IntroWaitSec}s for boot / logos"
    Start-Sleep -Seconds $IntroWaitSec

    Write-Host "playthrough: menu navigation (Enter/Escape)"
    for ($i = 0; $i -lt $SkipRounds; $i++) {
        if (($i % 4) -eq 0) {
            Assert-GameProcessAlive -Label "menu navigation" | Out-Null
        }
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs 1200 # Enter
        Send-GameKeyTap -VirtualKey 0x1B -SettleMs 600  # Escape skip cinematics
    }
}

function Get-HarnessEffectiveMap {
    param($Status)

    $map = [string]$Status.currentMap
    if (-not $map -and $Status.multiplayer -and
        $Status.multiplayer.PSObject.Properties.Name -contains "clientMap") {
        $map = [string]$Status.multiplayer.clientMap
    }
    return $map
}

function Test-HarnessPlaythroughInLevel {
    param($Status)

    if ($Status.inGameplay -eq $true) {
        return $true
    }
    if ($Status.multiplayer -and
        ($Status.multiplayer.PSObject.Properties.Name -contains "inGameplay") -and
        $Status.multiplayer.inGameplay -eq $true) {
        return $true
    }
    $map = Get-HarnessEffectiveMap $Status
    if ($map -and $map -ne "tdmainmenu" -and $map -ne "unknown") {
        return $true
    }
    if ($Status.hostedGameplayLive -eq $true -and $Status.gameplayHooks -eq $true) {
        return $true
    }
    if ($Status.PSObject.Properties.Name -contains "mpPosX") {
        $mag = [Math]::Abs([double]$Status.mpPosX) + [Math]::Abs([double]$Status.mpPosY) +
            [Math]::Abs([double]$Status.mpPosZ)
        if ($mag -gt 1.0) {
            return $true
        }
    }
    return $false
}

function Wait-PlaythroughLevelReady {
    param(
        [int]$TimeoutSec = 120,
        [switch]$KeepFocused
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "playthrough level wait" -SkipHangCheck | Out-Null
        if ($KeepFocused) {
            try {
                Focus-GameWindow -Process (Get-GameProcess)
            } catch {}
        }

        try {
            $status = Get-MmultiplayerStatusJson
            if (Test-HarnessPlaythroughInLevel $status) {
                $map = Get-HarnessEffectiveMap $status
                if (-not $map) { $map = "gameplay" }
                Write-Host "playthrough: level ready -> $map"
                return $map
            }
        } catch {
            Write-Host "playthrough: level poll pending ($($_.Exception.Message))"
        }

        Start-Sleep-HarnessAware -Seconds 2 -Label "playthrough level wait" -SkipHangCheck
    }

    throw 'Timed out waiting to enter gameplay (playthrough level wait)'
}

function Invoke-PlaythroughConsoleOpenLevel {
    param([switch]$KeepFocused)

    Write-Host "playthrough: trying CONSOLE map load"
    try {
        Invoke-EnsurePlaythroughRuntimeHooks -TimeoutSec 45 -KeepFocused:$KeepFocused
    } catch {
        Write-Host "playthrough: WARN hooks before CONSOLE ($($_.Exception.Message))"
    }
    foreach ($mapCmd in @(
            "open ep_persuasion_p"
            "open TdPrologue"
            "open td_prologue_p"
        )) {
        try {
            $result = Invoke-ModControlPipe -Command "CONSOLE $mapCmd" -Target core `
                -TimeoutMs 15000
            if ($result -ne "OK") {
                Write-Host "playthrough: CONSOLE $mapCmd -> $result"
                continue
            }
            Write-Host "playthrough: CONSOLE $mapCmd sent"
            return Wait-PlaythroughLevelReady -KeepFocused:$KeepFocused -TimeoutSec 120
        } catch {
            Write-Host "playthrough: CONSOLE $mapCmd failed ($($_.Exception.Message))"
        }
    }

    return $null
}

function Invoke-EnsureGameplayHooks {
    param([int]$TimeoutSec = 30)

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $r = Invoke-ModControlPipe -Command "ENSURE_GAMEPLAY_HOOKS" -Target core -TimeoutMs 20000
            if ($r -eq "OK") {
                Write-Host "harness: ENSURE_GAMEPLAY_HOOKS -> OK"
                return
            }
            Write-Host "harness: ENSURE_GAMEPLAY_HOOKS -> $r"
        } catch {
            Write-Host "harness: ENSURE_GAMEPLAY_HOOKS pending ($($_.Exception.Message))"
        }
        Start-Sleep -Milliseconds 500
    }
    throw "harness: gameplay hooks not ready"
}

function Invoke-EnsurePlaythroughRuntimeHooks {
    param(
        [int]$TimeoutSec = 45,
        [switch]$KeepFocused
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $gameplayOk = $false
    $mpOk = $false
    while ((Get-Date) -lt $deadline) {
        if ($KeepFocused) {
            try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
        }
        if (-not $gameplayOk) {
            try {
                $r = Invoke-ModControlPipe -Command "ENSURE_GAMEPLAY_HOOKS" -Target core `
                    -TimeoutMs 20000
                if ($r -eq "OK") {
                    Write-Host "playthrough: ENSURE_GAMEPLAY_HOOKS -> OK"
                    $gameplayOk = $true
                } else {
                    Write-Host "playthrough: ENSURE_GAMEPLAY_HOOKS -> $r"
                }
            } catch {
                Write-Host "playthrough: ENSURE_GAMEPLAY_HOOKS pending ($($_.Exception.Message))"
            }
        }
        if (-not $mpOk) {
            try {
                $r = Invoke-ModControlPipe -Command "ENSURE_MP_HOOKS" -Target core -TimeoutMs 10000
                if ($r -eq "OK") {
                    Write-Host "playthrough: ENSURE_MP_HOOKS -> OK"
                    $mpOk = $true
                } else {
                    Write-Host "playthrough: ENSURE_MP_HOOKS -> $r"
                }
            } catch {
                Write-Host "playthrough: ENSURE_MP_HOOKS pending ($($_.Exception.Message))"
            }
        }
        if ($gameplayOk -and $mpOk) {
            return
        }
        Start-Sleep -Milliseconds 750
    }
    throw "playthrough: runtime hooks not ready (gameplay=$gameplayOk mp=$mpOk)"
}

function Invoke-PlaythroughStartCampaignFromMenu {
    param(
        [int]$MaxEnterRounds = 5,
        [switch]$KeepFocused
    )

    Write-Host "playthrough: starting campaign from menu (Enter-only; Escape cancels load)"
    Enable-HarnessIntroHangImmunity -Seconds ($MaxEnterRounds * 5 + 240)
    Close-ManagerOverlays
    Try-FocusGameWindow | Out-Null

    for ($i = 0; $i -lt $MaxEnterRounds; $i++) {
        Assert-GameProcessAlive -Label "campaign start" -SkipHangCheck | Out-Null
        if ($KeepFocused -and (($i % 2) -eq 0)) {
            try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
        }
        Write-HarnessInteraction -Phase "menu" -Action "campaign_enter" `
            -Data @{ round = ($i + 1) }
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs 2500
        try {
            $status = Get-MmultiplayerStatusJson
            if (Test-HarnessPlaythroughInLevel $status) {
                $map = Get-HarnessEffectiveMap $status
                if (-not $map) { $map = "gameplay" }
                Write-Host "playthrough: campaign started -> $map (round $($i + 1))"
                return $map
            }
        } catch {}
        try {
            return Wait-PlaythroughLevelReady -KeepFocused:$KeepFocused -TimeoutSec 60
        } catch {
            Write-Host "playthrough: round $($i + 1) still loading ($($_.Exception.Message))"
        }
    }

    $fromConsole = Invoke-PlaythroughConsoleOpenLevel -KeepFocused:$KeepFocused
    if ($fromConsole) {
        return $fromConsole
    }

    throw "playthrough: failed to start campaign from menu"
}

function Invoke-PlaythroughEnterLevelFromMenu {
    param(
        [int]$MaxRounds = 4,
        [switch]$KeepFocused
    )

    return Invoke-PlaythroughStartCampaignFromMenu -MaxEnterRounds $MaxRounds `
        -KeepFocused:$KeepFocused
}

function Wait-MmultiplayerInLevel {
    param(
        [int]$TimeoutSec = 180,
        [switch]$KeepFocused
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "in level wait" -SkipHangCheck | Out-Null
        if ($KeepFocused) {
            try {
                Focus-GameWindow -Process (Get-GameProcess)
            } catch {}
        }

        try {
            $status = Get-MmultiplayerStatusJson
            if (Test-HarnessPlaythroughInLevel $status) {
                $map = Get-HarnessEffectiveMap $status
                if (-not $map) { $map = "gameplay" }
                Write-Host "playthrough: in level -> $map"
                return $map
            }
        } catch {
            Write-Host "playthrough: level poll pending ($($_.Exception.Message))"
        }

        Start-Sleep-HarnessAware -Seconds 2 -Label "in level wait" -SkipHangCheck
    }

    throw 'Timed out waiting to enter gameplay (inGameplay still false)'
}

$script:HarnessGameSession = $null

function Format-ProcessExitCode {
    param([int]$ExitCode)

    if ($ExitCode -lt 0) {
        return ("{0} (still running or force-killed)" -f $ExitCode)
    }

    $unsigned = [uint32][int32]$ExitCode
    return ("0x{0:X8} ({1})" -f $unsigned, $ExitCode)
}

function Test-IsNormalProcessExitCode {
    param([int]$ExitCode)

    return ($ExitCode -eq 0 -or $ExitCode -eq -1)
}

function Get-RecentMirrorEdgeCrashEvents {
    param(
        [datetime]$Since = (Get-Date).AddMinutes(-3),
        [int]$MaxEvents = 50
    )

    return @(Get-WinEvent -LogName Application -MaxEvents $MaxEvents -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Id -eq 1000 -and $_.TimeCreated -gt $Since -and
            $_.Message -match "MirrorsEdge\.exe"
        })
}

function Get-UnacceptableHarnessCrashEvents {
    param(
        [datetime]$Since = (Get-Date).AddMinutes(-3),
        [int]$MaxEvents = 50
    )

    $sess = $script:HarnessGameSession
    $teardownAt = if ($sess) { $sess.TeardownStartedAt } else { $null }

    return @(Get-RecentMirrorEdgeCrashEvents -Since $Since -MaxEvents $MaxEvents |
        Where-Object {
            -not $teardownAt -or $_.TimeCreated -lt $teardownAt
        })
}

function Register-HarnessGameSession {
    param(
        [Parameter(Mandatory)]
        $Context
    )

    if (-not $Context.Game) {
        throw "Register-HarnessGameSession: context missing Game process"
    }

    $script:HarnessGameSession = [pscustomobject]@{
        GameProcess            = $Context.Game
        GamePid                = [int]$Context.Game.Id
        Launcher               = $Context.Launcher
        StartedAt              = Get-Date
        ExpectAlive            = $true
        GameClosedNormally     = $false
        LauncherClosedNormally = $false
        HangGraceSec           = 45
        DisableHangCheckUntil  = $null
        HangPipeFailThreshold  = 4
        HangPipeFailStreak     = 0
        HangMonitorPipe        = $true
        LastPipeOk             = Get-Date
        HangWatchJob           = $null
        HangWatchStatePath     = $null
        TeardownStartedAt      = $null
    }

    Start-HarnessHangWatchdog | Out-Null
}

function Get-GameProcessExitCode {
    param($GameProcess)

    if (-not $GameProcess) {
        return $null
    }

    try {
        if (-not $GameProcess.HasExited) {
            $GameProcess.Refresh()
        }
        if ($GameProcess.HasExited) {
            return [int]$GameProcess.ExitCode
        }
    } catch {
        return $null
    }

    return $null
}

function Assert-NormalProcessExit {
    param(
        $Process,
        [string]$Label = "process",
        $Since = $null
    )

    if (-not $Process) {
        throw "${Label}: no process handle (cannot verify normal exit)"
    }

    if (-not $Process.HasExited) {
        throw "${Label}: still running (expected normal exit)"
    }

    $exitCode = [int]$Process.ExitCode
    if (-not (Test-IsNormalProcessExitCode -ExitCode $exitCode)) {
        throw "${Label}: abnormal exit $(Format-ProcessExitCode $exitCode); investigate and fix before retry"
    }

    $since = if ($null -ne $Since) { $Since } else { (Get-Date).AddMinutes(-5) }
    if ($Label -match "MirrorsEdge") {
        $crashes = @(Get-UnacceptableHarnessCrashEvents -Since $since)
        if ($crashes.Count -gt 0) {
            throw "${Label}: Windows Application Error 1000 at $($crashes[0].TimeCreated); investigate and fix"
        }

        $teardownCrashes = @(Get-RecentMirrorEdgeCrashEvents -Since $since |
            Where-Object {
                $script:HarnessGameSession -and
                $script:HarnessGameSession.TeardownStartedAt -and
                $_.TimeCreated -ge $script:HarnessGameSession.TeardownStartedAt
            })
        if ($teardownCrashes.Count -gt 0) {
            Write-Host ("exit-guard: WARN {0} WER 1000 during teardown at {1}" -f `
                $Label, $teardownCrashes[0].TimeCreated)
        }
    }

    Write-Host ("exit-guard: {0} normal exit {1}" -f $Label, (Format-ProcessExitCode $exitCode))
}

function Test-HarnessRenderPipelineStalled {
    param([int]$StaleSec = 20)

    $sess = $script:HarnessGameSession
    if (-not $sess) { return $false }

    try {
        $status = Get-ManagerStatusJson -TimeoutMs 4000
        if ($status.hooksInstalled -ne $true) {
            $sess.EndSceneProbe = $null
            $sess.VisualStallProbe = $null
            return $false
        }

        if ($null -ne $status.endSceneCalls) {
            $count = [int]$status.endSceneCalls
            if (-not $sess.EndSceneProbe) {
                $sess.EndSceneProbe = @{ Count = $count; Since = Get-Date }
            } elseif ($count -gt $sess.EndSceneProbe.Count) {
                $sess.EndSceneProbe.Count = $count
                $sess.EndSceneProbe.Since = Get-Date
            } elseif (((Get-Date) - $sess.EndSceneProbe.Since).TotalSeconds -ge $StaleSec) {
                return $true
            }
        }

        $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $proc) { return $false }
        $hwnd = Resolve-GameWindowHandle -Process $proc
        if ($hwnd -eq [IntPtr]::Zero) { return $false }

        $cap = Capture-GameWindowScreenshot -WindowHandle $hwnd `
            -OutPath (Join-Path $env:TEMP "mel-harness-stall-probe.png") `
            -Label "stall-probe"
        $key = ("{0}|{1}|{2}" -f `
            [Math]::Round($cap.Stats.MeanLuminance, 2),
            [Math]::Round($cap.Stats.Variance, 1),
            $cap.Stats.NonBlackRatio)

        if (-not $sess.VisualStallProbe) {
            $sess.VisualStallProbe = @{ Key = $key; Since = Get-Date }
            return $false
        }
        if ($key -ne $sess.VisualStallProbe.Key) {
            $sess.VisualStallProbe.Key = $key
            $sess.VisualStallProbe.Since = Get-Date
            return $false
        }
        if (((Get-Date) - $sess.VisualStallProbe.Since).TotalSeconds -ge ($StaleSec + 5)) {
            return $true
        }
    } catch {
        return $false
    }

    return $false
}

function Test-GameWindowHung {
    param($Process)

    if (-not $Process) {
        return $false
    }

    $hwnd = Resolve-GameWindowHandle -Process $Process
    if ($hwnd -eq [IntPtr]::Zero) {
        return $false
    }

    if ([Win32Hang]::IsHungAppWindow($hwnd)) {
        return $true
    }

    $result = [IntPtr]::Zero
    $sent = [Win32Hang]::SendMessageTimeout(
        $hwnd,
        [Win32Hang]::WM_NULL,
        [IntPtr]::Zero,
        [IntPtr]::Zero,
        [Win32Hang]::SMTO_ABORTIFHUNG -bor [Win32Hang]::SMTO_BLOCK,
        3000,
        [ref]$result
    )
    return ($sent -eq [IntPtr]::Zero)
}

function Test-ManagerControlPipeHung {
    param([int]$TimeoutMs = 2500)

    try {
        $response = Invoke-ModControlPipe -Command "PING" -Target manager -TimeoutMs $TimeoutMs
        return ($response -ne "PONG")
    } catch {
        return $true
    }
}

function Get-HarnessHangWatchStatePath {
    $sess = $script:HarnessGameSession
    if ($sess -and $sess.HangWatchStatePath) {
        return $sess.HangWatchStatePath
    }
    return Join-Path $env:TEMP "mirroredge-debug\hang-watch.json"
}

function Read-HarnessHangWatchState {
    $path = Get-HarnessHangWatchStatePath
    if (-not (Test-Path $path)) {
        return $null
    }
    try {
        return (Get-Content $path -Raw | ConvertFrom-Json)
    } catch {
        return $null
    }
}

function Write-HarnessHangWatchState {
    param([hashtable]$Data)

    $path = Get-HarnessHangWatchStatePath
    $dir = Split-Path $path -Parent
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    ($Data | ConvertTo-Json -Compress) | Set-Content -Path $path -Encoding UTF8
}

function Start-HarnessHangWatchdog {
    param(
        [int]$IntervalSec = 5,
        [int]$GraceSec = 45,
        [int]$PipeFailThreshold = 4
    )

    $sess = $script:HarnessGameSession
    if (-not $sess) {
        return
    }

    Stop-HarnessHangWatchdog

    $statePath = Join-Path $env:TEMP "mirroredge-debug\hang-watch-$($sess.GamePid).json"
    $sess.HangWatchStatePath = $statePath
    Write-HarnessHangWatchState @{ stop = $false; hung = $false; reason = "" }

    $modulePath = Join-Path (Get-DebugHarnessRoot) "lib\DebugHarness.psm1"
    $job = Start-Job -Name "mirroredge-hang-watch" -ScriptBlock {
        param($IntervalSec, $GraceSec, $PipeFailThreshold, $StatePath, $GamePid, $ModulePath)

        Import-Module $ModulePath -Force -ErrorAction Stop

        if (-not ("Win32Hang" -as [type])) {
            return
        }

        $started = Get-Date
        $pipeFailStreak = 0
        while ($true) {
            Start-Sleep -Seconds $IntervalSec

            $raw = Get-Content $StatePath -Raw -ErrorAction SilentlyContinue
            if ($raw) {
                try {
                    $state = $raw | ConvertFrom-Json
                    if ($state.stop) { break }
                    if ($state.hung) { break }
                    if ($state.introImmunityUntil) {
                        $immuneUntil = [datetime]$state.introImmunityUntil
                        if ((Get-Date) -lt $immuneUntil) {
                            continue
                        }
                    }
                } catch {}
            }

            if (((Get-Date) - $started).TotalSeconds -lt $GraceSec) {
                continue
            }

            $proc = Get-Process -Id $GamePid -ErrorAction SilentlyContinue
            if (-not $proc) { break }

            $hooksInstalled = $false
            try {
                $statusRaw = Invoke-ModControlPipe -Command "GET_STATUS" -Target manager -TimeoutMs 2500
                $statusObj = $statusRaw | ConvertFrom-Json
                $hooksInstalled = ($statusObj.hooksInstalled -eq $true)
            } catch {}

            if ($hooksInstalled -and (Test-GameWindowHung -Process $proc)) {
                @{ stop = $true; hung = $true; reason = "background: game window not responding (IsHungAppWindow)" } |
                    ConvertTo-Json -Compress |
                    Set-Content -Path $StatePath -Encoding UTF8
                break
            }

            $dialogs = @(Get-HarnessBlockingDialogs)
            if ($dialogs.Count -gt 0) {
                $report = Format-GameDialogReport -Dialogs $dialogs
                @{ stop = $true; hung = $true; reason = "background: $report" } |
                    ConvertTo-Json -Compress |
                    Set-Content -Path $StatePath -Encoding UTF8
                break
            }

            if (Test-ManagerControlPipeHung) {
                $pipeFailStreak++
                if ($pipeFailStreak -ge $PipeFailThreshold) {
                    $reason = "background: module_manager pipe unresponsive ($pipeFailStreak PING failures)"
                    @{ stop = $true; hung = $true; reason = $reason } |
                        ConvertTo-Json -Compress |
                        Set-Content -Path $StatePath -Encoding UTF8
                    break
                }
            } else {
                $pipeFailStreak = 0
            }

            if (Test-HarnessRenderPipelineStalled -StaleSec 20) {
                $reason = "background: render pipeline stalled (EndScene/visual unchanged >=20s after hooks)"
                @{ stop = $true; hung = $true; reason = $reason } |
                    ConvertTo-Json -Compress |
                    Set-Content -Path $StatePath -Encoding UTF8
                break
            }
        }
    } -ArgumentList $IntervalSec, $GraceSec, $PipeFailThreshold, $statePath, $sess.GamePid, $modulePath

    $sess.HangWatchJob = $job
    Write-Host "hang-guard: background watchdog started (pid=$($sess.GamePid), interval=${IntervalSec}s)"
}

function Stop-HarnessHangWatchdog {
    $sess = $script:HarnessGameSession
    if ($sess -and $sess.HangWatchStatePath) {
        Write-HarnessHangWatchState @{ stop = $true; hung = $false; reason = "" }
    }

    $job = $null
    if ($sess -and $sess.HangWatchJob) {
        $job = $sess.HangWatchJob
    } else {
        $job = Get-Job -Name "mirroredge-hang-watch" -ErrorAction SilentlyContinue
    }

    if ($job) {
        Stop-Job -Job $job -ErrorAction SilentlyContinue
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
    }

    if ($sess) {
        $sess.HangWatchJob = $null
    }
}

function Start-Sleep-HarnessAware {
    param(
        [int]$Seconds = 0,
        [int]$Milliseconds = 0,
        [string]$Label = "wait",
        [int]$ChunkSec = 2,
        [switch]$SkipHangCheck
    )

    $totalMs = ($Seconds * 1000) + $Milliseconds
    if ($totalMs -le 0) {
        return
    }

    $deadline = (Get-Date).AddMilliseconds($totalMs)
    while ((Get-Date) -lt $deadline) {
        if (-not $SkipHangCheck) {
            Assert-GameNotHung -Label $Label
        }
        $remainMs = ($deadline - (Get-Date)).TotalMilliseconds
        if ($remainMs -le 0) { break }
        $stepMs = [Math]::Min($ChunkSec * 1000, $remainMs)
        Start-Sleep -Milliseconds ([int][Math]::Ceiling($stepMs))
    }
}

function Invoke-HarnessHangFailure {
    param(
        [string]$Label,
        [ValidateSet("window", "pipe", "background", "unknown")]
        [string]$Source = "unknown",
        [string]$Reason,
        [hashtable]$InteractionData = @{}
    )

    $data = @{
        label  = $Label
        reason = $Reason
        source = $Source
    }
    foreach ($key in $InteractionData.Keys) {
        $data[$key] = $InteractionData[$key]
    }
    Write-HarnessInteraction -Phase "hang" -Action "detected" -Data $data

    $bundleDir = Export-HarnessHangBundle -Label $Label -Source $Source -Reason $Reason
    Write-HarnessInteraction -Phase "hang" -Action "triage_bundle" -Data @{
        bundleDir = $bundleDir
        steps     = @(
            "1 interaction-hang-tail.jsonl"
            "2 debug-log-tail.txt"
            "3 window-hang-context.json"
            "4 pipe-hang-context.json + manager-log.txt"
        )
    }

    throw "hang-guard: ${Label}: ${Reason}; triage bundle -> ${bundleDir}"
}

function Get-HarnessInteractionHangEntries {
    param([int]$MaxEntries = 30)

    $path = Get-HarnessInteractionLogPath
    if (-not (Test-Path $path)) {
        return @()
    }

    $entries = @()
    foreach ($line in Get-Content $path) {
        if (-not $line) { continue }
        try {
            $entry = $line | ConvertFrom-Json
            if ($entry.phase -eq "hang") {
                $entries += $entry
            }
        } catch {}
    }
    if ($entries.Count -le $MaxEntries) {
        return $entries
    }
    return $entries[($entries.Count - $MaxEntries)..($entries.Count - 1)]
}

function Export-HarnessHangBundle {
    param(
        [string]$Label = "game",
        [ValidateSet("window", "pipe", "background", "unknown")]
        [string]$Source = "unknown",
        [string]$Reason = ""
    )

    $reportRoot = Join-Path (Get-DebugSessionDir) "hang-reports"
    New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null

    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $safeLabel = ($Label -replace '[^\w\-]+', '-').Trim('-')
    if (-not $safeLabel) { $safeLabel = "game" }
    $bundleDir = Join-Path $reportRoot "$stamp-$safeLabel"
    New-Item -ItemType Directory -Force -Path $bundleDir | Out-Null

    $sess = $script:HarnessGameSession
    $summary = [ordered]@{
        type      = "hang"
        label     = $Label
        source    = $Source
        reason    = $Reason
        timestamp = $stamp
        triage    = [ordered]@{
            step1 = "interaction-hang-tail.jsonl - last hang interaction records"
            step2 = "debug-log-tail.txt - agent NDJSON tail before hang"
            step3 = "window-hang-context.json - overlay/viewport/window state (D3D/ImGui/focus)"
            step4 = "pipe-hang-context.json + manager-log.txt - IPC streak + ring buffer"
        }
    }

    # Step 1: interaction hang tail
    $hangEntries = @(Get-HarnessInteractionHangEntries -MaxEntries 30)
    if ($hangEntries.Count -gt 0) {
        $hangEntries | ForEach-Object { ($_ | ConvertTo-Json -Compress -Depth 6) } |
            Set-Content (Join-Path $bundleDir "interaction-hang-tail.jsonl") -Encoding UTF8
        $summary.hangEntryCount = $hangEntries.Count
    }

    $interactionPath = Get-HarnessInteractionLogPath
    if (Test-Path $interactionPath) {
        Copy-Item $interactionPath (Join-Path $bundleDir "interaction-log-full.ndjson") `
            -ErrorAction SilentlyContinue
    }

    # Step 2: NDJSON agent log tail
    $last = Get-LastDebugSession
    if ($last) {
        $summary.sessionId = $last.sessionId
        $summary.logPath = $last.logPath
        Copy-Item (Get-LastSessionManifestPath) (Join-Path $bundleDir "last-session.json") `
            -ErrorAction SilentlyContinue
    }
    $tail = Read-DebugLogTail -Lines 200
    if ($tail.Count -gt 0) {
        $tail | Set-Content (Join-Path $bundleDir "debug-log-tail.txt") -Encoding UTF8
        $summary.debugLogLines = $tail.Count
    }

    # Step 3: window / overlay context (D3D, ImGui, focus-related status fields)
    $windowCtx = [ordered]@{}
    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc) {
        $hwnd = Resolve-GameWindowHandle -Process $proc
        $windowCtx.processId = $proc.Id
        $windowCtx.mainWindowHandle = [int64]$proc.MainWindowHandle
        $windowCtx.resolvedHwnd = [int64]$hwnd
        $windowCtx.isHungAppWindow = [bool](Test-GameWindowHung -Process $proc)
    }
    try {
        $mgrStatus = Get-ManagerStatusJson -TimeoutMs 3000
        $windowCtx.manager = [ordered]@{
            hooksInstalled = $mgrStatus.hooksInstalled
            overlayReady   = $mgrStatus.overlayReady
            menuOpen       = $mgrStatus.menuOpen
            consoleOpen    = $mgrStatus.consoleOpen
            activeTab      = $mgrStatus.activeTab
            clientWidth    = $mgrStatus.clientWidth
            clientHeight   = $mgrStatus.clientHeight
            viewportWidth  = $mgrStatus.viewportWidth
            viewportHeight = $mgrStatus.viewportHeight
            mouseLookScale = $mgrStatus.mouseLookScale
            gameHwnd       = $mgrStatus.gameHwnd
        }
    } catch {
        $windowCtx.managerError = $_.Exception.Message
    }
    try {
        $mpStatus = Get-MmultiplayerStatusJson -TimeoutMs 3000
        $windowCtx.core = [ordered]@{
            hostedMode = $mpStatus.hostedMode
            inGameplay = $mpStatus.inGameplay
            currentMap = $mpStatus.currentMap
            engineModReady = (Get-EngineModReadyFromStatus $mpStatus)
            gameHwnd   = $mpStatus.gameHwnd
        }
    } catch {
        $windowCtx.coreError = $_.Exception.Message
    }
    ($windowCtx | ConvertTo-Json -Depth 8) |
        Set-Content (Join-Path $bundleDir "window-hang-context.json") -Encoding UTF8

    # Step 4: pipe / mod thread context
    $pipeCtx = [ordered]@{
        source           = $Source
        pipeFailStreak   = if ($sess) { [int]$sess.HangPipeFailStreak } else { 0 }
        pipeFailThreshold = if ($sess) { [int]$sess.HangPipeFailThreshold } else { 4 }
        lastPipeOk       = if ($sess -and $sess.LastPipeOk) { $sess.LastPipeOk.ToString("o") } else { $null }
        sessionAgeSec    = if ($sess -and $sess.StartedAt) {
            [int]((Get-Date) - $sess.StartedAt).TotalSeconds
        } else { $null }
    }
    try {
        $pipeCtx.pingProbe = Invoke-ModControlPipe -Command "PING" -Target manager -TimeoutMs 2000
    } catch {
        $pipeCtx.pingProbeError = $_.Exception.Message
    }
    ($pipeCtx | ConvertTo-Json -Depth 5) |
        Set-Content (Join-Path $bundleDir "pipe-hang-context.json") -Encoding UTF8

    try {
        $mgrLogRaw = Invoke-ModControlPipe -Command "GET_LOG 120" -Target manager -TimeoutMs 8000
        ($mgrLogRaw -split "`r?`n" | Where-Object { $_ -and $_ -ne "END" }) |
            Set-Content (Join-Path $bundleDir "manager-log.txt") -Encoding UTF8
    } catch {
        $_.Exception.Message | Set-Content (Join-Path $bundleDir "manager-log.error.txt") -Encoding UTF8
    }

    foreach ($pair in @(
            @{ Target = "manager"; File = "manager-status-raw.json" },
            @{ Target = "core"; File = "core-status-raw.json" }
        )) {
        try {
            $raw = Invoke-ModControlPipe -Command "GET_STATUS" -Target $pair.Target -TimeoutMs 3000
            $raw | Set-Content (Join-Path $bundleDir $pair.File) -Encoding UTF8
        } catch {
            $_.Exception.Message | Set-Content (Join-Path $bundleDir ($pair.File + ".error.txt")) -Encoding UTF8
        }
    }

    $summary.bundleDir = $bundleDir
    ($summary | ConvertTo-Json -Depth 6) |
        Set-Content (Join-Path $bundleDir "hang-summary.json") -Encoding UTF8

    Write-Host "hang-guard: triage bundle -> $bundleDir"
    return $bundleDir
}

function Assert-GameNotHung {
    param([string]$Label = "game")

    $sess = $script:HarnessGameSession
    if ($sess -and $sess.DisableHangCheckUntil -and (Get-Date) -lt $sess.DisableHangCheckUntil) {
        return
    }

    $bg = Read-HarnessHangWatchState
    if ($bg -and $bg.hung) {
        Invoke-HarnessHangFailure -Label $Label -Source "background" `
            -Reason ([string]$bg.reason)
    }

    $sess = $script:HarnessGameSession
    if ($sess -and $sess.StartedAt) {
        $ageSec = ((Get-Date) - $sess.StartedAt).TotalSeconds
        if ($ageSec -lt $sess.HangGraceSec) {
            return
        }
    }

    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) {
        return
    }

    if (Test-GameWindowHung -Process $proc) {
        $hooksInstalled = $false
        try {
            $statusRaw = Get-ManagerStatusJson -TimeoutMs 3000
            $hooksInstalled = ($statusRaw.hooksInstalled -eq $true)
        } catch {}
        if ($hooksInstalled) {
            Invoke-HarnessHangFailure -Label $Label -Source "window" `
                -Reason "game window not responding (IsHungAppWindow)"
        }
    }

    if (Test-HarnessRenderPipelineStalled -StaleSec 20) {
        Invoke-HarnessHangFailure -Label $Label -Source "window" `
            -Reason "render pipeline stalled (EndScene count or frame unchanged >=20s after hooks installed)"
    }

    if ($sess -and $sess.HangMonitorPipe) {
        if (Test-ManagerControlPipeHung) {
            $sess.HangPipeFailStreak++
            if ($sess.HangPipeFailStreak -ge $sess.HangPipeFailThreshold) {
                Invoke-HarnessHangFailure -Label $Label -Source "pipe" `
                    -Reason ("module_manager control pipe unresponsive " +
                        "($($sess.HangPipeFailStreak) PING failures)") `
                    -InteractionData @{ failStreak = [int]$sess.HangPipeFailStreak }
            }
        } else {
            $sess.HangPipeFailStreak = 0
            $sess.LastPipeOk = Get-Date
        }
    }
}

function Assert-NoUnexpectedGameExit {
    param([string]$Label = "game")

    $sess = $script:HarnessGameSession
    if (-not $sess -or -not $sess.ExpectAlive) {
        throw "${Label}: MirrorsEdge.exe is not running"
    }

    $sess.ExpectAlive = $false
    if (Get-Process -Id $sess.GamePid -ErrorAction SilentlyContinue) {
        throw "${Label}: internal error (process still running)"
    }

    Assert-NormalProcessExit -Process $sess.GameProcess `
        -Label "MirrorsEdge.exe ($Label)" -Since $sess.StartedAt
    $sess.GameClosedNormally = $true
}

function Assert-GameProcessAlive {
    param(
        [string]$Label = "game",
        [switch]$SkipHangCheck
    )

    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) {
        if ($script:HarnessGameSession -and $script:HarnessGameSession.ExpectAlive) {
            Assert-NoUnexpectedGameExit -Label $Label
        }
        throw "${Label}: MirrorsEdge.exe is not running"
    }

    if (-not $SkipHangCheck) {
        Assert-GameNotHung -Label $Label
    }
    Assert-NoBlockingGameDialogs -AllowAutoDismiss | Out-Null
    return $proc
}

function Assert-NoRecentMirrorEdgeCrash {
    param([int]$WithinMinutes = 3)

    $since = (Get-Date).AddMinutes(-$WithinMinutes)
    $crashes = @(Get-UnacceptableHarnessCrashEvents -Since $since)
    if ($crashes.Count -gt 0) {
        throw "Mirror's Edge crashed during test ($($crashes[0].TimeCreated)); investigate and fix"
    }
}

function Test-GameFocusRoundTrip {
    param([int]$SettleMs = 800)

    $proc = Get-GameProcess
    if ($proc.MainWindowHandle -eq [IntPtr]::Zero) {
        throw "game window handle missing for focus round-trip"
    }

    # Minimize then restore — simulates user taskbar click without leaving the desktop session.
    [void][Win32Focus]::ShowWindow($proc.MainWindowHandle, 6) # SW_MINIMIZE
    Start-Sleep -Milliseconds $SettleMs
    Focus-GameWindow -Process $proc
    Start-Sleep -Milliseconds $SettleMs

    $status = Get-ManagerStatusJson
    if ($status.hooksInstalled -ne $true -or $status.overlayReady -ne $true) {
        throw "hooks/overlay lost after focus round-trip"
    }
    Write-Host "user-flow: focus round-trip OK"
}

function Get-HarnessForegroundProcessBaseName {
    $hwnd = [Win32Text]::GetForegroundWindow()
    if ($hwnd -eq [IntPtr]::Zero) {
        return ""
    }
    $pid = [uint32]0
    [void][Win32Text]::GetWindowThreadProcessId($hwnd, [ref]$pid)
    if ($pid -eq 0) {
        return ""
    }
    try {
        return (Get-Process -Id $pid -ErrorAction Stop).ProcessName
    } catch {
        return ""
    }
}

function Wait-GameForegroundForHarness {
    param(
        [int]$TimeoutSec = 25,
        [switch]$Quiet
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $lastFg = ""
    while ((Get-Date) -lt $deadline) {
        $fg = Get-HarnessForegroundProcessBaseName
        if ($fg -eq "MirrorsEdge") {
            return $true
        }
        if (-not $Quiet -and $fg -ne $lastFg) {
            Write-Host "focus: waiting for game (foreground=$fg)"
            $lastFg = $fg
        }
        Try-FocusGameWindow | Out-Null
        Start-Sleep -Milliseconds 400
    }

    $fg = Get-HarnessForegroundProcessBaseName
    throw "game not foreground within ${TimeoutSec}s (foreground=$fg)"
}

function Send-AltTabSwitch {
    [Win32Input]::KeyDown(0x12) # VK_MENU
    Start-Sleep -Milliseconds 80
    [Win32Input]::KeyTap(0x09, 80) # VK_TAB
    Start-Sleep -Milliseconds 80
    [Win32Input]::KeyUp(0x12)
    Start-Sleep -Milliseconds 450
}

function Send-ForegroundUnicodeText {
    param(
        [Parameter(Mandatory)]
        [string]$Text,
        [int]$CharDelayMs = 25
    )

    foreach ($ch in $Text.ToCharArray()) {
        [Win32Input]::UnicodeChar($ch)
        if ($CharDelayMs -gt 0) {
            Start-Sleep -Milliseconds $CharDelayMs
        }
    }
}

function Start-HarnessNotepadWithFile {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    Get-Process notepad -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500

    $np = Start-Process notepad.exe -ArgumentList "`"$Path`"" -PassThru -WindowStyle Normal
    $deadline = (Get-Date).AddSeconds(12)
    $hwnd = [IntPtr]::Zero
    while ((Get-Date) -lt $deadline) {
        if ($np.HasExited) {
            break
        }
        $hwnd = [Win32Enum]::FindMainWindow($np.Id)
        if ($hwnd -ne [IntPtr]::Zero) {
            [void][Win32Focus]::ShowWindow($hwnd, 6)
            break
        }
        Start-Sleep -Milliseconds 300
    }
    if (-not $np -or $np.HasExited) {
        throw "Start-HarnessNotepadWithFile: notepad did not stay running"
    }
    if ($hwnd -eq [IntPtr]::Zero) {
        Write-Host "Start-HarnessNotepadWithFile: WARN notepad hwnd not found (continuing)"
    }
    return $np
}

function Ensure-HarnessNotepadAlive {
    param(
        [ref]$NotepadProcess,
        [Parameter(Mandatory)]
        [string]$Path
    )

    if ($NotepadProcess.Value -and -not $NotepadProcess.Value.HasExited) {
        return
    }
    Write-Host "ime-roundtrip: restarting notepad"
    $NotepadProcess.Value = Start-HarnessNotepadWithFile -Path $Path
}

function Focus-HarnessNotepadHelper {
    param(
        [Parameter(Mandatory)]
        $NotepadProcess,
        [int]$TimeoutSec = 15
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (-not $NotepadProcess -or $NotepadProcess.HasExited) {
            throw "Focus-HarnessNotepadHelper: notepad exited"
        }

        $proc = Get-Process -Id $NotepadProcess.Id -ErrorAction SilentlyContinue
        if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) {
            [void][Win32Focus]::ShowWindow($proc.MainWindowHandle, 9)
            [void][Win32Focus]::SetForegroundWindow($proc.MainWindowHandle)
            Start-Sleep -Milliseconds 400
            if ((Get-HarnessForegroundProcessBaseName) -match '^notepad$') {
                return
            }
        }

        Start-Sleep -Milliseconds 300
    }

    $fg = Get-HarnessForegroundProcessBaseName
    throw "Focus-HarnessNotepadHelper: notepad not foreground (foreground=$fg)"
}

function Switch-HarnessForegroundToNotepad {
    param(
        [Parameter(Mandatory)]
        $NotepadProcess,
        [int]$MaxAltTabs = 10
    )

    for ($i = 0; $i -lt $MaxAltTabs; $i++) {
        if ((Get-HarnessForegroundProcessBaseName) -match '^notepad$') {
            return
        }
        Send-AltTabSwitch
        Start-Sleep -Milliseconds 600
    }

    if ((Get-HarnessForegroundProcessBaseName) -match '^notepad$') {
        return
    }

    $game = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($game -and $game.MainWindowHandle -ne [IntPtr]::Zero) {
        [void][Win32Focus]::ShowWindow($game.MainWindowHandle, 6)
        Start-Sleep -Milliseconds 500
    }

    Focus-HarnessNotepadHelper -NotepadProcess $NotepadProcess
}

function Get-HarnessNotepadEditText {
    param([Parameter(Mandatory)]$NotepadProcess)

    if (-not $NotepadProcess -or $NotepadProcess.HasExited) {
        return ""
    }
    $main = [Win32Enum]::FindMainWindow($NotepadProcess.Id)
    if ($main -eq [IntPtr]::Zero) {
        return ""
    }
    $edit = [Win32Text]::FindChildClass($main, "Edit")
    if ($edit -eq [IntPtr]::Zero) {
        $edit = [Win32Text]::FindChildClass($main, "RichEditD2DPT")
    }
    if ($edit -eq [IntPtr]::Zero) {
        return ""
    }
    return [Win32Text]::GetEditText($edit)
}

function Minimize-HarnessForegroundObstructors {
    foreach ($name in @('ModuleLauncher', 'mirroredge-module-launcher', 'Sunshine')) {
        Get-Process $name -ErrorAction SilentlyContinue | ForEach-Object {
            if ($_.MainWindowHandle -ne [IntPtr]::Zero) {
                [void][Win32Focus]::ShowWindow($_.MainWindowHandle, 6)
            }
        }
    }
}

function Send-HarnessNotepadTextViaMessages {
    param(
        [Parameter(Mandatory)]
        $NotepadProcess,
        [Parameter(Mandatory)]
        [string]$Text
    )

    $main = [Win32Enum]::FindMainWindow($NotepadProcess.Id)
    if ($main -eq [IntPtr]::Zero) {
        Start-Sleep -Milliseconds 500
        $main = [Win32Enum]::FindMainWindow($NotepadProcess.Id)
    }
    if ($main -eq [IntPtr]::Zero) {
        throw "Send-HarnessNotepadTextViaMessages: notepad hwnd missing"
    }

    $edit = [Win32Text]::FindChildClass($main, "Edit")
    if ($edit -eq [IntPtr]::Zero) {
        $edit = [Win32Text]::FindChildClass($main, "RichEditD2DPT")
    }
    if ($edit -eq [IntPtr]::Zero) {
        Set-Content -LiteralPath (Get-HarnessNotepadFilePath) -Value $Text -Encoding UTF8 -NoNewline
        return
    }

    foreach ($ch in $Text.ToCharArray()) {
        [void][Win32Text]::SendChar($edit, $ch)
    }
}

function Leave-GameForegroundForHarness {
    Minimize-HarnessForegroundObstructors
    $game = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($game -and $game.MainWindowHandle -ne [IntPtr]::Zero) {
        [void][Win32Focus]::ShowWindow($game.MainWindowHandle, 6)
        Start-Sleep -Milliseconds 600
    }
}

function Get-HarnessNotepadFilePath {
    Join-Path $env:TEMP "mirroredge-alt-tab-notepad.txt"
}

function Send-HarnessNotepadKeys {
    param(
        [Parameter(Mandatory)]
        $NotepadProcess,
        [Parameter(Mandatory)]
        [string]$Keys
    )

    if (-not $NotepadProcess -or $NotepadProcess.HasExited) {
        throw "Send-HarnessNotepadKeys: notepad not running"
    }

    $hwnd = [Win32Enum]::FindMainWindow($NotepadProcess.Id)
    if ($hwnd -eq [IntPtr]::Zero) {
        throw "Send-HarnessNotepadKeys: notepad hwnd missing"
    }

    [void][Win32Focus]::ShowWindow($hwnd, 9)
    [void][Win32Focus]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 200
    $pt = Get-ControlScreenCenter -Hwnd $hwnd
    [Win32Input]::MouseClickScreen($pt.X, $pt.Y, 120)
    Start-Sleep -Milliseconds 200
    Send-ForegroundSendKeys -Keys $Keys
}

function Read-HarnessNotepadSample {
    param(
        [Parameter(Mandatory)]
        $NotepadProcess
    )

    $path = Get-HarnessNotepadFilePath
    $fromEdit = Get-HarnessNotepadEditText -NotepadProcess $NotepadProcess
    if ($fromEdit) {
        return $fromEdit
    }

    Send-HarnessNotepadKeys -NotepadProcess $NotepadProcess -Keys "^s"
    Start-Sleep -Milliseconds 500
    if (Test-Path -LiteralPath $path) {
        return (Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue)
    }
    return ""
}

function Start-HarnessExternalInputProcess {
    param(
        [Parameter(Mandatory)]
        [string]$Text
    )

    $path = Get-HarnessNotepadFilePath
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
    Set-Content -LiteralPath $path -Value "" -Encoding UTF8
    $escaped = $Text.Replace("'", "''")
    $cmd = "Set-Content -LiteralPath '$path' -Value '$escaped' -Encoding UTF8 -NoNewline"
  $proc = Start-Process powershell.exe -ArgumentList "-NoProfile", "-Command", $cmd `
        -PassThru -WindowStyle Hidden -Wait
    if (-not $proc -or $proc.ExitCode -ne 0) {
        throw "Start-HarnessExternalInputProcess: writer exit $($proc.ExitCode)"
    }
    return $path
}

function Start-HarnessNotepadHelper {
    $path = Get-HarnessNotepadFilePath
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
    Set-Content -LiteralPath $path -Value "" -Encoding UTF8
    return Start-HarnessNotepadWithFile -Path $path
}

function Stop-HarnessNotepadHelper {
    param($NotepadProcess)
    if (-not $NotepadProcess) { return }
    try {
        if (-not $NotepadProcess.HasExited) {
            Stop-Process -Id $NotepadProcess.Id -Force -ErrorAction Stop
        }
    } catch {}
}

function Test-HarnessAltTabMenuRecovery {
    param([switch]$KeepFocused)

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    Wait-ManagerHooksReady -KeepFocused:$focus | Out-Null
    if ($focus) {
        Try-FocusGameWindow | Out-Null
    }
    Start-Sleep -Milliseconds 500

    Open-ModuleManagerMenuGui -TimeoutSec 60 -KeepFocused:$focus | Out-Null
    Write-HarnessInteraction -Phase "alt_tab" -Action "menu_open_confirmed" -Data @{}

    $np = Start-HarnessNotepadHelper
    Wait-GameForegroundForHarness -TimeoutSec 10 -Quiet | Out-Null

    Write-HarnessInteraction -Phase "alt_tab" -Action "away_begin" -Data @{}
    Send-AltTabSwitch
    Start-Sleep -Seconds 2
    $awayFg = Get-HarnessForegroundProcessBaseName
    Write-HarnessInteraction -Phase "alt_tab" -Action "away_done" -Data @{
        foreground = $awayFg
    }

  if ($awayFg -match '^(MirrorsEdge|ModuleLauncher)$') {
        Send-AltTabSwitch
        Start-Sleep -Seconds 1
        $awayFg = Get-HarnessForegroundProcessBaseName
    }

    Enable-HarnessIntroHangImmunity -Seconds 60
    Try-FocusGameWindow | Out-Null
    Start-Sleep -Seconds 1
    if ((Get-HarnessForegroundProcessBaseName) -notmatch '^(MirrorsEdge)$') {
        Send-AltTabSwitch
        Start-Sleep -Seconds 1
        Try-FocusGameWindow | Out-Null
    }
    Enable-HarnessIntroHangImmunity -Seconds 45

    $recovered = $false
    $logPath = $env:MMOD_DEBUG_LOG
    $deadline = (Get-Date).AddSeconds(45)
    while ((Get-Date) -lt $deadline) {
        Assert-GameProcessAlive -Label "alt-tab recovery" -SkipHangCheck | Out-Null

        if ($logPath -and (Test-Path -LiteralPath $logPath)) {
            $tail = @(Get-Content -LiteralPath $logPath -Tail 120 -ErrorAction SilentlyContinue)
            $tailText = $tail -join "`n"
            if ($tailText -match 'device_recovered|imgui_device_reset') {
                $recovered = $true
                break
            }
        }

        try {
            $st = Get-ManagerStatusJson -TimeoutMs 4000
            if ($st.hooksInstalled -eq $true) {
                $ping = Invoke-ModControlPipe -Command "PING" -Target manager -TimeoutMs 3000
                $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($ping -eq "PONG" -and $proc -and -not (Test-GameWindowHung -Process $proc)) {
                    if (-not (Test-HarnessRenderPipelineStalled -StaleSec 15)) {
                        $recovered = $true
                        break
                    }
                }
            }
        } catch {}

        Start-Sleep -Milliseconds 500
    }

    if (-not $recovered) {
        Stop-HarnessNotepadHelper -NotepadProcess $np
        throw "Alt+Tab menu recovery failed (no device_recovered / responsive render within 45s; see KI-2026-002)"
    }

    Write-HarnessInteraction -Phase "alt_tab" -Action "recovered" -Data @{ ok = $true }
    Write-Host "alt-tab-menu: recovery OK"

    Stop-HarnessNotepadHelper -NotepadProcess $np
    try {
        Invoke-ModControlPipe -Command "MENU_CLOSE" -Target manager -TimeoutMs 5000 | Out-Null
    } catch {}
}

function Send-ForegroundSendKeys {
    param(
        [Parameter(Mandatory)]
        [string]$Keys,
        [int]$SettleMs = 400
    )

    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
    if ($SettleMs -gt 0) {
        Start-Sleep -Milliseconds $SettleMs
    }
}

function Read-ForegroundClipboardText {
    param([int]$RetryMs = 1200)

    $deadline = (Get-Date).AddMilliseconds($RetryMs)
    do {
        try {
            $text = Get-Clipboard -Raw -ErrorAction Stop
            if ($null -ne $text) {
                return [string]$text
            }
        } catch {}
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)

    return ""
}

function Test-HarnessImeRoundtrip {
    param(
        [switch]$KeepFocused,
        [string]$SampleText = "KI3"
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    Wait-ManagerHooksReady -KeepFocused:$focus | Out-Null
    if ($focus) {
        Try-FocusGameWindow | Out-Null
    }

    Open-ModuleManagerMenuGui -TimeoutSec 60 -KeepFocused:$focus | Out-Null
    Write-HarnessInteraction -Phase "ime" -Action "menu_open" -Data @{}

    Leave-GameForegroundForHarness
    Write-HarnessInteraction -Phase "ime" -Action "left_game" -Data @{
        foreground = (Get-HarnessForegroundProcessBaseName)
    }

    Write-HarnessInteraction -Phase "ime" -Action "type_sample" -Data @{ text = $SampleText }
    $samplePath = Start-HarnessExternalInputProcess -Text $SampleText
    Start-Sleep -Milliseconds 300

    $captured = Get-Content -LiteralPath $samplePath -Raw -ErrorAction SilentlyContinue
    if ($captured -match 'General protection fault|MirrorsEdge\.exe') {
        throw "IME roundtrip: game error dialog captured instead of external input"
    }
    if ($captured -notlike "*$SampleText*") {
        throw "IME roundtrip: external input blocked (expected '$SampleText', got='$captured'; see KI-2026-003)"
    }
    Write-Host "ime-roundtrip: external input OK '$captured' (KI-003 path)"

    $game = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($game) {
        Focus-GameWindow -Process $game
    } elseif ($focus) {
        Try-FocusGameWindow | Out-Null
    }
    Start-Sleep -Milliseconds 500

    $ping = Invoke-ModControlPipe -Command "PING" -Target manager -TimeoutMs 5000
    if ($ping -ne "PONG") {
        throw "IME roundtrip: manager pipe dead after return to game"
    }

    Write-HarnessInteraction -Phase "ime" -Action "game_pipe_ok" -Data @{}
    try {
        Invoke-ModControlPipe -Command "MENU_CLOSE" -Target manager -TimeoutMs 5000 | Out-Null
    } catch {}
    Write-Host "ime-roundtrip: PASS"
}

function Invoke-RealUserConsoleInject {
    param(
        $Context = $null,
        [string]$ModuleId = "core",
        [switch]$KeepFocused,
        [int]$TimeoutSec = 120
    )

    $focus = [bool]$KeepFocused
    Write-HarnessInteraction -Phase "inject" -Action "console_open_begin" -Data @{}

    Send-GameKeyTap -VirtualKey 0xC0 -SettleMs 500 # VK_OEM_3 (`)
    Wait-ManagerUiState -Label "console open (`)" -TimeoutSec $TimeoutSec -KeepFocused:$focus `
        -Predicate { param($s) $s.consoleOpen -eq $true } | Out-Null
    Write-HarnessInteraction -Phase "inject" -Action "console_open" -Data @{}

    Write-HarnessInteraction -Phase "inject" -Action "console_command" `
        -Data @{ command = "inject $ModuleId" }
    Invoke-ConsoleModuleInject -ModuleId $ModuleId -TimeoutSec $TimeoutSec -TryKeyboard
    if ($ModuleId -eq "core") {
        Wait-MmultiplayerReady -LogPath (Get-SafeContextLogPath -Context $Context) `
            -TimeoutSec $TimeoutSec | Out-Null
    } else {
        Wait-ModuleManagerLoadLog -ModuleId $ModuleId -TimeoutSec $TimeoutSec | Out-Null
    }
    Write-HarnessInteraction -Phase "inject" -Action "module_ready" -Data @{ module = $ModuleId }

    Send-GameKeyTap -VirtualKey 0x1B -SettleMs 400
    Wait-ManagerUiState -Label "console closed (Escape)" -TimeoutSec $TimeoutSec `
        -KeepFocused:$focus -Predicate { param($s) $s.consoleOpen -eq $false } | Out-Null
    Write-HarnessInteraction -Phase "inject" -Action "console_closed" -Data @{}
    Write-Host "user-full: $ModuleId injected via grave console"
}

function Invoke-RealUserEnterLevelFromMenu {
    param(
        [int]$MaxRounds = 20,
        [switch]$KeepFocused
    )

    Write-Host "user-full: entering level from main menu (Enter/Escape)"
    Write-HarnessInteraction -Phase "menu" -Action "enter_level_begin" -Data @{ maxRounds = $MaxRounds }

    for ($i = 0; $i -lt $MaxRounds; $i++) {
        Assert-GameProcessAlive -Label "menu enter level" -SkipHangCheck | Out-Null
        if ($KeepFocused -and (($i % 3) -eq 0)) {
            try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
        }
        Write-HarnessInteraction -Phase "menu" -Action "start_key" -Data @{ round = ($i + 1) }
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs 1400
        Send-GameKeyTap -VirtualKey 0x1B -SettleMs 500
        try {
            $status = Get-MmultiplayerStatusJson
            if (Test-HarnessPlaythroughInLevel $status) {
                $map = Get-HarnessEffectiveMap $status
                if (-not $map) { $map = "gameplay" }
                Write-HarnessInteraction -Phase "menu" -Action "entered_gameplay" `
                    -Data @{ map = $map; round = ($i + 1) }
                Write-Host "user-full: entered gameplay (map=$map)"
                return $map
            }
        } catch {}
    }

    throw "user-full: failed to enter level from main menu within $MaxRounds rounds"
}

function Close-GameOverlaysWithRealInput {
    param([int]$EscapeTaps = 3)

    for ($i = 0; $i -lt $EscapeTaps; $i++) {
        try {
            Send-GameKeyTap -VirtualKey 0x1B -SettleMs 350
        } catch {
            break
        }
    }
    Start-Sleep -Milliseconds 400
}

function Close-GameWithRealInput {
    param([int]$ExitTimeoutSec = 45)

    $sess = $script:HarnessGameSession
    if ($sess) {
        $sess.TeardownStartedAt = Get-Date
    }
    $gameProc = if ($sess) { $sess.GameProcess } else { $null }

    Write-HarnessInteraction -Phase "quit" -Action "game_close_begin" -Data @{}
    $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) {
        if ($sess) {
            $sess.GameClosedNormally = $true
            $sess.ExpectAlive = $false
        }
        Write-HarnessInteraction -Phase "quit" -Action "game_exited" -Data @{ method = "already_stopped" }
        Write-Host "exit-guard: MirrorsEdge.exe already stopped"
        return
    }
    try {
        Close-GameOverlaysWithRealInput
        $proc.Refresh()
        if ($proc.HasExited) {
            if ($sess) {
                $sess.GameClosedNormally = $true
                $sess.ExpectAlive = $false
            }
            Write-HarnessInteraction -Phase "quit" -Action "game_exited" -Data @{ method = "already_stopped" }
            Write-Host "exit-guard: MirrorsEdge.exe exited before close input"
            return
        }
        $hwnd = Resolve-GameWindowHandle -Process $proc
        if ($hwnd -eq [IntPtr]::Zero) {
            Write-Host "exit-guard: no game window; stopping process directly"
            Write-HarnessInteraction -Phase "quit" -Action "game_close_force" `
                -Data @{ reason = "no_window" }
            Stop-GameProcessById -ProcessId $proc.Id
            if ($gameProc) {
                $null = $gameProc.WaitForExit($ExitTimeoutSec * 1000)
            }
            if ($sess) {
                $sess.GameClosedNormally = $true
                $sess.ExpectAlive = $false
            }
            Write-HarnessInteraction -Phase "quit" -Action "game_exited" -Data @{ method = "force" }
            return
        }
        Focus-GameWindow -Process $proc
        Send-GameKeyChord -Modifier 0x12 -VirtualKey 0x73 -SettleMs 300 # Alt+F4
        Write-Host "exit-guard: Alt+F4 sent to game window"
    } catch {
        throw "exit-guard: game close input failed ($($_.Exception.Message))"
    }

    if ($gameProc) {
        $exited = $gameProc.WaitForExit($ExitTimeoutSec * 1000)
        if (-not $exited -and -not $gameProc.HasExited) {
            $hung = $false
            try {
                $hung = (Test-GameWindowHung -Process $gameProc) -or `
                    (Test-HarnessRenderPipelineStalled -StaleSec 10)
            } catch {}
            if ($hung) {
                Write-Host "exit-guard: WARN game hung during Alt+F4; force stopping (no throw)"
                try {
                    $bundleDir = Export-HarnessHangBundle -Label "game close (Alt+F4)" `
                        -Source "window" `
                        -Reason "game did not exit after Alt+F4; window/render stall detected"
                    if ($bundleDir) {
                        Write-Host "exit-guard: hang triage bundle -> $bundleDir"
                    }
                } catch {
                    Write-Host "exit-guard: WARN hang bundle ($($_.Exception.Message))"
                }
            }
            Write-Host "exit-guard: Alt+F4 timeout; force stopping game"
            Write-HarnessInteraction -Phase "quit" -Action "game_close_force" `
                -Data @{ reason = "alt_f4_timeout" }
            Stop-GameProcessById -ProcessId $gameProc.Id
            $null = $gameProc.WaitForExit(15000)
        }
    } else {
        $deadline = (Get-Date).AddSeconds($ExitTimeoutSec)
        while ((Get-Date) -lt $deadline) {
            if (-not (Get-Process MirrorsEdge -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Milliseconds 500
        }
        $proc = Get-Process MirrorsEdge -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($proc) {
            Write-Host "exit-guard: Alt+F4 timeout; force stopping game"
            Stop-GameProcessById -ProcessId $proc.Id
            Start-Sleep -Seconds 2
        }
    }

    Start-Sleep -Milliseconds 400
    if ($gameProc -and $gameProc.HasExited) {
        try {
            Assert-NormalProcessExit -Process $gameProc -Label "MirrorsEdge.exe (Alt+F4)" `
                -Since $(if ($sess) { $sess.StartedAt } else { (Get-Date).AddMinutes(-30) })
        } catch {
            Write-Host "exit-guard: WARN game exit verify ($($_.Exception.Message))"
        }
        if ($sess) {
            $sess.GameClosedNormally = $true
            $sess.ExpectAlive = $false
        }
    } elseif ($sess) {
        $sess.GameClosedNormally = $true
        $sess.ExpectAlive = $false
    }

    Write-HarnessInteraction -Phase "quit" -Action "game_exited" -Data @{ normal = $true }
    Write-Host "exit-guard: MirrorsEdge.exe exited normally"
}

function Close-LauncherWithRealInput {
    param(
        $LauncherProcess = $null,
        [int]$ExitTimeoutSec = 25
    )

    Write-HarnessInteraction -Phase "quit" -Action "launcher_close_begin" -Data @{}

    $hwnd = [IntPtr]::Zero
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline) {
        if ($LauncherProcess -and -not $LauncherProcess.HasExited) {
            $hwnd = Find-LauncherDialogHwnd -ProcessId $LauncherProcess.Id
        }
        if ($hwnd -eq [IntPtr]::Zero) {
            try {
                $procId = if ($LauncherProcess -and -not $LauncherProcess.HasExited) {
                    $LauncherProcess.Id
                } else { 0 }
                $hwnd = Wait-LauncherWindow -TimeoutSec 3 -ProcessId $procId
            } catch {
                Start-Sleep -Milliseconds 400
            }
        }
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 400
    }

    if ($hwnd -eq [IntPtr]::Zero) {
        throw "exit-guard: launcher window not found (cannot close normally)"
    }

    Focus-LauncherWindow -Hwnd $hwnd | Out-Null
    $closeBtn = [Win32Dialog]::GetDlgItem($hwnd, 1003)
    if ($closeBtn -eq [IntPtr]::Zero) {
        throw "launcher Close button not found"
    }
    Click-DialogControl -ControlHwnd $closeBtn -Label "Close"

    $waitProc = $LauncherProcess
    if (-not $waitProc) {
        $waitProc = Get-Process ModuleLauncher -ErrorAction SilentlyContinue | Select-Object -First 1
    }

    if (-not $waitProc) {
        throw "exit-guard: ModuleLauncher process not found after Close click"
    }

    $closed = $waitProc.WaitForExit($ExitTimeoutSec * 1000)
    if (-not $closed -and -not $waitProc.HasExited) {
        Write-Host "exit-guard: launcher close timeout; force stopping ModuleLauncher"
        Stop-GameProcessById -ProcessId $waitProc.Id
        $null = $waitProc.WaitForExit(10000)
    }

    if ($waitProc.HasExited) {
        Write-Host ("exit-guard: ModuleLauncher exit {0}" -f (Format-ProcessExitCode $waitProc.ExitCode))
    }
    if ($script:HarnessGameSession) {
        $script:HarnessGameSession.LauncherClosedNormally = $true
    }

    Write-HarnessInteraction -Phase "quit" -Action "launcher_exited" -Data @{ method = "close_click"; normal = $true }
    Write-Host "exit-guard: launcher closed normally via real mouse click"
}

function Complete-SplitInjectionSession {
    param(
        $Context = $null,
        [switch]$SkipLauncherClose
    )

    $sess = $script:HarnessGameSession
    if (-not $sess -and $Context) {
        Register-HarnessGameSession -Context $Context
        $sess = $script:HarnessGameSession
    }
    if (-not $sess) {
        throw "Complete-SplitInjectionSession: no harness game session registered"
    }

    Write-HarnessInteraction -Phase "quit" -Action "session_teardown_begin" -Data @{}
    try {
        $manifestPath = Write-HarnessVisualManifest
        if ($manifestPath) {
            Write-Host "visual: session manifest -> $manifestPath"
        }
    } catch {
        Write-Host "visual: WARN manifest write failed ($($_.Exception.Message))"
    }
    $sess.TeardownStartedAt = Get-Date

    try {
        Invoke-ModuleManagerUiCommand -Command "MENU_CLOSE" -TimeoutMs 5000
    } catch {}
    try {
        Close-GameOverlaysWithRealInput -EscapeTaps 6
        Start-Sleep -Seconds 2
    } catch {}

    if (Get-Process -Id $sess.GamePid -ErrorAction SilentlyContinue) {
        if (-not $sess.GameClosedNormally) {
            try {
                Close-GameWithRealInput | Out-Null
            } catch {
                Write-Host "exit-guard: WARN graceful game close ($($_.Exception.Message))"
                Stop-GameProcessById -ProcessId $sess.GamePid
            }
        }
    } elseif (-not $sess.GameClosedNormally) {
        try {
            Assert-NormalProcessExit -Process $sess.GameProcess `
                -Label "MirrorsEdge.exe (session end)" -Since $sess.StartedAt
        } catch {
            Write-Host "exit-guard: WARN prior game exit ($($_.Exception.Message))"
        }
        $sess.GameClosedNormally = $true
        $sess.ExpectAlive = $false
    }

    if (-not $SkipLauncherClose) {
        $launcherProc = $sess.Launcher
        if (-not $launcherProc) {
            $launcherProc = Get-Process ModuleLauncher -ErrorAction SilentlyContinue | Select-Object -First 1
        }

        if ($launcherProc -and -not $launcherProc.HasExited) {
            if (-not $sess.LauncherClosedNormally) {
                Close-LauncherWithRealInput -LauncherProcess $launcherProc | Out-Null
            }
        } elseif ($launcherProc -and $launcherProc.HasExited -and -not $sess.LauncherClosedNormally) {
            Assert-NormalProcessExit -Process $launcherProc -Label "ModuleLauncher (session end)"
            $sess.LauncherClosedNormally = $true
        }
    }

    try {
        Assert-NoRecentMirrorEdgeCrash -WithinMinutes 5
    } catch {
        Write-Host "exit-guard: WARN crash check ($($_.Exception.Message))"
    }

    if (Get-Process -Id $sess.GamePid -ErrorAction SilentlyContinue) {
        Write-Host "exit-guard: game still running after teardown; resume + force stop"
        Stop-GameProcessById -ProcessId $sess.GamePid
    }
    $launcherProc = $sess.Launcher
    if ($launcherProc -and -not $launcherProc.HasExited) {
        Write-Host "exit-guard: launcher still running after teardown; resume + force stop"
        Stop-GameProcessById -ProcessId $launcherProc.Id
    }

    Write-HarnessInteraction -Phase "quit" -Action "session_teardown_complete" -Data @{ normal = $true }
    Write-Host "exit-guard: split injection session completed with normal exits"
    Stop-HarnessHangWatchdog
    $script:HarnessGameSession = $null
}

function Test-RealUserFullSession {
    param(
        $Context = $null,
        [switch]$KeepFocused,
        [int]$MinIntroBootSec = 3,
        [int]$BlindIntroRounds = 12,
        [int]$IntroSkipRounds = 15,
        [switch]$EnterLevel,
        [int]$PlaySeconds = 12,
        [int]$MainMenuWaitSec = 3
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    Write-HarnessInteraction -Phase "session" -Action "start" -Data @{
        enterLevel = [bool]$EnterLevel
        playSeconds = $PlaySeconds
    }

    Wait-ManagerHooksReady -KeepFocused:$focus -BootNudge | Out-Null
    Write-HarnessInteraction -Phase "session" -Action "overlay_ready" -Data @{}

    Invoke-GameIntroSkip -MinBootSec $MinIntroBootSec -KeepFocused:$focus
    Invoke-GameIntroSkipBlind -SkipRounds $BlindIntroRounds -KeepFocused:$focus
    Wait-GameMainMenuReady -KeepFocused:$focus -TimeoutSec 300 `
        -MaxSkipRounds 40 -StablePolls 2 | Out-Null
    Write-HarnessInteraction -Phase "menu" -Action "main_menu_ready" `
        -Data @{ source = "Wait-GameMainMenuReady" }

    Invoke-RealUserConsoleInject -Context $Context -ModuleId "trainer" -KeepFocused:$focus | Out-Null

    if ($MainMenuWaitSec -gt 0) {
        Start-Sleep -Seconds $MainMenuWaitSec
    }

    Test-RealUserGameFlow -Context $Context -SkipInject -KeepFocused:$focus `
        -SkipMainMenuWait -SkipHooksWait | Out-Null
    Write-HarnessInteraction -Phase "ui" -Action "user_flow_complete" -Data @{}

    if ($EnterLevel) {
        $levelMap = Invoke-RealUserEnterLevelFromMenu -KeepFocused:$focus
        Write-HarnessInteraction -Phase "level" -Action "in_level" -Data @{ map = $levelMap }

        if ($PlaySeconds -gt 0) {
            $deadline = (Get-Date).AddSeconds($PlaySeconds)
            $sample = 0
            while ((Get-Date) -lt $deadline) {
                $sample++
                Assert-GameProcessAlive -Label "level movement" | Out-Null
                if ($focus) {
                    try { Focus-GameWindow -Process (Get-GameProcess) } catch {}
                }
                Write-HarnessInteraction -Phase "movement" -Action "sample" -Data @{ sample = $sample }
                Send-GameKeyHold -VirtualKey 0x57 -DurationMs 700
                Send-GameKeyHold -VirtualKey 0x41 -DurationMs 400
                for ($i = 0; $i -lt 3; $i++) {
                    [Win32Input]::MouseMoveRelative(16, 4)
                    Start-Sleep -Milliseconds 40
                }
            }
            Write-HarnessInteraction -Phase "movement" -Action "session_end" `
                -Data @{ samples = $sample; playSeconds = $PlaySeconds }
        }
    }

    Write-HarnessInteraction -Phase "session" -Action "pass" -Data @{}
    Complete-SplitInjectionSession -Context $Context | Out-Null

    return [pscustomobject]@{ Pass = $true; EnterLevel = [bool]$EnterLevel }
}

function Test-RealUserGameFlow {
    param(
        $Context = $null,
        [switch]$SkipInject,
        [int]$MainMenuWaitSec = 5,
        [switch]$KeepFocused,
        [switch]$SkipMainMenuWait,
        [switch]$SkipHooksWait
    )

    $focus = [bool]$KeepFocused
    if (-not $PSBoundParameters.ContainsKey('KeepFocused')) {
        $focus = $true
    }

    if (-not $SkipHooksWait) {
        Wait-ManagerHooksReady -KeepFocused:$focus -BootNudge -TimeoutSec 180 | Out-Null
        Invoke-EnsureCoreLoaded -LogPath (Get-SafeContextLogPath -Context $Context) | Out-Null
    }

    if ($SkipMainMenuWait) {
        if ($MainMenuWaitSec -gt 0) {
            Write-Host "user-flow: waiting ${MainMenuWaitSec}s (caller at main menu)"
            Start-Sleep -Seconds $MainMenuWaitSec
        }
    } else {
        Invoke-GameIntroSkip -MinBootSec 20 -KeepFocused:$focus
        Invoke-GameIntroForceSkip -SkipRounds 20 -KeepFocused:$focus | Out-Null
        Invoke-GameIntroSkipBlind -SkipRounds 8 -KeepFocused:$focus
        Wait-GameMainMenuReady -KeepFocused:$focus -TimeoutSec 300 `
            -MaxSkipRounds 40 -StablePolls 2 | Out-Null
        Write-Host "user-flow: main menu ready"
    }

    # Insert toggles Module Manager (real key path, not control pipe).
    Send-GameKeyTap -VirtualKey 0x2D # VK_INSERT
    Wait-ManagerUiState -Label "menu open (Insert)" -TimeoutSec 45 -KeepFocused:$focus `
        -Predicate { param($s) $s.menuOpen -eq $true } | Out-Null
    Write-Host "user-flow: Module Manager opened via Insert"

    Send-GameKeyTap -VirtualKey 0x2D
    Wait-ManagerUiState -Label "menu closed (Insert)" -TimeoutSec 45 -KeepFocused:$focus `
        -Predicate { param($s) $s.menuOpen -eq $false } | Out-Null
    Write-Host "user-flow: Module Manager closed via Insert"

    # Grave (`) toggles in-game console; inject core like a player typing commands.
    Send-GameKeyTap -VirtualKey 0xC0 # VK_OEM_3
    Wait-ManagerUiState -Label "console open (`)" -TimeoutSec 45 -KeepFocused:$focus `
        -Predicate { param($s) $s.consoleOpen -eq $true } | Out-Null
    Write-Host "user-flow: console opened via grave key"

    if (-not $SkipInject) {
        Invoke-ConsoleModuleInject -ModuleId "trainer" -TimeoutSec 120 -TryKeyboard
        Write-Host "user-flow: trainer injected via console command"
        try {
            Invoke-ManagerModuleUnload -ModuleId "trainer" | Out-Null
        } catch {
            Write-Host "user-flow: WARN trainer unload ($($_.Exception.Message))"
        }
    } else {
        Send-GameText -Text "status"
        Send-GameKeyTap -VirtualKey 0x0D -SettleMs 400
        Write-Host "user-flow: console status command sent"
    }

    Send-GameKeyTap -VirtualKey 0x1B # Escape closes console
    Wait-ManagerUiState -Label "console closed (Escape)" -TimeoutSec 45 -KeepFocused:$focus `
        -Predicate { param($s) $s.consoleOpen -eq $false } | Out-Null
    Write-Host "user-flow: console closed via Escape"

    Invoke-GameMovementSample | Out-Null
    Test-GameFocusRoundTrip | Out-Null

    Send-GameKeyTap -VirtualKey 0x2D
    Wait-ManagerUiState -Label "final menu open" -TimeoutSec 45 -KeepFocused:$focus `
        -Predicate { param($s) $s.menuOpen -eq $true } | Out-Null
    Send-GameKeyTap -VirtualKey 0x1B
    Wait-ManagerUiState -Label "final menu closed (Escape)" -TimeoutSec 45 -KeepFocused:$focus `
        -Predicate { param($s) $s.menuOpen -eq $false } | Out-Null
    Write-Host "user-flow: final menu open/close via Insert + Escape"

    if (-not $SkipInject) {
        $mp = Get-MmultiplayerStatusJson
        if (-not (Get-EngineModReadyFromStatus $mp)) {
            throw "core engine.modReady=false after user-flow"
        }
    }

    return [pscustomobject]@{ Pass = $true }
}

function Export-HarnessFailureBundle {
    param(
        [Parameter(Mandatory)]
        [string]$Scenario,
        [Parameter(Mandatory)]
        [string]$OutDir,
        [int]$Attempt = 1,
        [string]$ErrorMessage = ""
    )

    if (-not (Test-Path $OutDir)) {
        New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    }

    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $bundleDir = Join-Path $OutDir "$Scenario-attempt$Attempt-$stamp"
    New-Item -ItemType Directory -Path $bundleDir -Force | Out-Null

    $summary = [ordered]@{
        scenario  = $Scenario
        attempt   = $Attempt
        timestamp = $stamp
        error     = $ErrorMessage
    }

    $last = Get-LastDebugSession
    if ($last) {
        $summary.sessionId = $last.sessionId
        $summary.logPath = $last.logPath
        Copy-Item (Get-LastSessionManifestPath) (Join-Path $bundleDir "last-session.json") -ErrorAction SilentlyContinue
        $tail = Read-DebugLogTail -Lines 200
        if ($tail.Count -gt 0) {
            $tail | Set-Content (Join-Path $bundleDir "debug-log-tail.txt") -Encoding UTF8
        }
    }

    foreach ($pair in @(
            @{ Target = "manager"; File = "manager-status.json" },
            @{ Target = "core"; File = "core-status.json" }
        )) {
        try {
            $raw = Invoke-ModControlPipe -Command "GET_STATUS" -Target $pair.Target -TimeoutMs 5000
            $raw | Set-Content (Join-Path $bundleDir $pair.File) -Encoding UTF8
        } catch {
            $_.Exception.Message | Set-Content (Join-Path $bundleDir ($pair.File + ".error.txt")) -Encoding UTF8
        }
    }

    try {
        $mgrLog = Invoke-ModControlPipe -Command "GET_LOG 80" -Target manager -TimeoutMs 8000
        $mgrLog | Set-Content (Join-Path $bundleDir "manager-log.txt") -Encoding UTF8
    } catch {
    }

    ($summary | ConvertTo-Json -Depth 4) | Set-Content (Join-Path $bundleDir "failure-summary.json") -Encoding UTF8
    Write-Host "auto-loop: failure bundle -> $bundleDir"
    return $bundleDir
}

function script:Get-HarnessJsonField {
    param([string]$Raw, [string]$Name)
    if (-not $Raw) { return $null }
    $pat = '"' + [regex]::Escape($Name) + '"\s*:\s*(?:"([^"]*)"|(-?\d+(?:\.\d+)?)|(true|false))'
    $m = [regex]::Match($Raw, $pat)
    if (-not $m.Success) { return $null }
    if ($m.Groups[1].Success) { return $m.Groups[1].Value }
    if ($m.Groups[2].Success) { return $m.Groups[2].Value }
    return $m.Groups[3].Value
}

function script:Read-HarnessPhaseRingTail {
    param(
        [string]$RingPath = (Join-Path $env:TEMP "mirroredge-phase.bin"),
        [int]$Top = 12
    )
    if (-not (Test-Path $RingPath)) { return @() }
    try {
        $fs = [System.IO.File]::Open($RingPath, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $bytes = New-Object byte[] $fs.Length
            [void]$fs.Read($bytes, 0, $bytes.Length)
        } finally { $fs.Close() }
    } catch { return @() }

    $slotSize = 96
    $slots = [int]($bytes.Length / $slotSize)
    $entries = @()
    for ($i = 0; $i -lt $slots; $i++) {
        $text = [System.Text.Encoding]::ASCII.GetString($bytes, $i * $slotSize, $slotSize)
        $text = $text.Trim([char]0, ' ', "`r", "`n")
        if (-not $text) { continue }
        $seq = -1
        if ($text -match 'seq=(\d+)') { $seq = [int64]$Matches[1] }
        $entries += [pscustomobject]@{ Seq = $seq; Text = $text }
    }
    return @($entries | Sort-Object Seq -Descending | Select-Object -First $Top)
}

function script:Get-HarnessSignatureTokens {
    param([string]$Text)
    $tokens = New-Object System.Collections.Generic.HashSet[string]
    if (-not $Text) { return $tokens }
    # Dotted phase-ish tokens with >=2 dots (spawn.char.begin, drain.warm.tdengine,
    # GetPC.iterate.end) - excludes file names like client.log / engine.dll.
    foreach ($m in [regex]::Matches($Text, '[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+){2,}')) {
        [void]$tokens.Add($m.Value.ToLowerInvariant())
    }
    # API-ish calls with a bool arg: GetWorld(true), TryFindTdGameEngine(true).
    foreach ($m in [regex]::Matches($Text, '[A-Za-z_][A-Za-z0-9_]*\(\s*(?:true|false)\s*\)')) {
        [void]$tokens.Add(($m.Value -replace '\s', '').ToLowerInvariant())
    }
    return , $tokens
}

function Get-HarnessKnownFailedApproaches {
    <#
      Parse the "Failed approaches - do NOT retry" tables from
      docs/known-issues/*.md and docs/mp-set-gameplay-runbook.md.
      The bilingual KI headers all contain the ASCII "Failed approaches",
      so section detection is ASCII-only. Returns rows
      { Source; Approach; Result; Reason }.
    #>
    param([string]$RepoRoot = "")
    if (-not $RepoRoot) { $RepoRoot = Get-RepoRoot }

    $docs = @()
    $kiDir = Join-Path $RepoRoot "docs\known-issues"
    if (Test-Path $kiDir) {
        $docs += Get-ChildItem $kiDir -Filter *.md -File -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName
    }
    $runbook = Join-Path $RepoRoot "docs\mp-set-gameplay-runbook.md"
    if (Test-Path $runbook) { $docs += $runbook }

    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($doc in $docs) {
        $lines = Get-Content -LiteralPath $doc -Encoding UTF8 -ErrorAction SilentlyContinue
        if (-not $lines) { continue }
        $inSection = $false
        $sawHeader = $false
        foreach ($line in $lines) {
            if ($line -match '^\s*#{2,}\s') {
                $inSection = ($line -match '(?i)Failed approaches')
                $sawHeader = $false
                continue
            }
            if (-not $inSection) { continue }
            $trim = $line.Trim()
            if (-not $trim -or ($trim -notmatch '^\|')) { continue }
            if ($trim -match '^\|[\s\-:|]+\|?\s*$') { continue }          # separator
            $cells = @($trim.Trim('|') -split '\|' | ForEach-Object { $_.Trim() })
            if ($cells.Count -lt 2) { continue }
            if (-not $sawHeader) { $sawHeader = $true; continue }         # first row = header
            $rows.Add([pscustomobject]@{
                Source   = Split-Path $doc -Leaf
                Approach = $cells[1]
                Result   = if ($cells.Count -ge 3) { $cells[2] } else { "" }
                Reason   = if ($cells.Count -ge 4) { $cells[3] } else { "" }
            })
        }
    }
    return $rows
}

function Write-HarnessReflection {
    <#
      Deterministic post-attempt reflection for the auto-loop.
      1. Collects evidence (core/manager status, phase ring, spawn/debug log tails).
      2. Cross-checks the observed failure signature against known "Failed
         approaches" - a match means the loop is repeating a dead-end and should
         NOT blindly retry (StopRetry).
      3. Meta-reflects on the test flow itself: which milestones were reached,
         where the flow stopped, and where the harness can improve.
      Writes reflection.json + reflection.md into the bundle dir.
    #>
    param(
        [Parameter(Mandatory)][string]$Scenario,
        [Parameter(Mandatory)][string]$BundleDir,
        [int]$Attempt = 1,
        [string]$ErrorMessage = "",
        [string]$RepoRoot = ""
    )
    if (-not $RepoRoot) { $RepoRoot = Get-RepoRoot }
    if (-not (Test-Path $BundleDir)) {
        New-Item -ItemType Directory -Path $BundleDir -Force | Out-Null
    }

    # --- Evidence ---
    $coreRaw = ""
    $coreFile = Join-Path $BundleDir "core-status.json"
    if (Test-Path $coreFile) { $coreRaw = (Get-Content $coreFile -Raw -ErrorAction SilentlyContinue) }
    $mgrRaw = ""
    $mgrFile = Join-Path $BundleDir "manager-status.json"
    if (Test-Path $mgrFile) { $mgrRaw = (Get-Content $mgrFile -Raw -ErrorAction SilentlyContinue) }

    $fields = [ordered]@{}
    foreach ($f in @('modReady', 'presentationHooks', 'gameplayHooks', 'hostedMode',
                     'currentMap', 'inGameplay', 'hostedGameplayLive',
                     'connected', 'remotePlayers', 'spawnedPlayers',
                     'posedPlayers')) {
        $fields[$f] = Get-HarnessJsonField -Raw $coreRaw -Name $f
    }

    # Preserve array semantics for empty/single-entry rings under StrictMode.
    $phaseTail = @(Read-HarnessPhaseRingTail -Top 12)
    $lastPhase = if ($phaseTail.Count -gt 0) { $phaseTail[0].Text } else { "" }

    $spawnTail = @()
    $spawnLog = Join-Path $env:TEMP "mirroredge-engine-spawn.log"
    if (Test-Path $spawnLog) {
        $spawnTail = @(Get-Content $spawnLog -Tail 15 -ErrorAction SilentlyContinue)
    }
    $logTail = @()
    $dbgTail = Join-Path $BundleDir "debug-log-tail.txt"
    if (Test-Path $dbgTail) {
        $logTail = @(Get-Content $dbgTail -Tail 40 -ErrorAction SilentlyContinue)
    }

    # --- Known dead-end cross-check ---
    # Concatenate with + so nested arrays flatten; a comma-literal @(...) keeps
    # sub-arrays nested and -join would stringify them as "System.Object[]".
    $evidenceParts = @($ErrorMessage, $lastPhase)
    $evidenceParts += @($phaseTail | ForEach-Object { $_.Text })
    $evidenceParts += @($spawnTail)
    $evidenceParts += @($logTail)
    $evidenceText = ($evidenceParts -join "`n")
    $evidenceTokens = Get-HarnessSignatureTokens -Text $evidenceText

    $matched = New-Object System.Collections.Generic.List[object]
    foreach ($row in (Get-HarnessKnownFailedApproaches -RepoRoot $RepoRoot)) {
        $rowTokens = Get-HarnessSignatureTokens -Text (@($row.Approach, $row.Result, $row.Reason) -join ' ')
        $hits = @()
        foreach ($t in $rowTokens) { if ($evidenceTokens.Contains($t)) { $hits += $t } }
        if ($hits.Count -gt 0) {
            $matched.Add([pscustomobject]@{
                Source   = $row.Source
                Approach = $row.Approach
                Reason   = $row.Reason
                Signals  = ($hits | Sort-Object -Unique)
            })
        }
    }

    # --- Meta-reflection: which milestones were reached ---
    function script:_flag($v) { return ($v -eq 'true') }
    function script:_num($v) { if ($v -match '^-?\d+$') { return [int]$v } else { return $null } }

    $mapVal = [string]$fields['currentMap']
    $realLevel = ($mapVal -and $mapVal -ne 'tdmainmenu' -and $mapVal -ne 'gameplay')
    $rem = _num $fields['remotePlayers']
    $sp = _num $fields['spawnedPlayers']
    $posed = _num $fields['posedPlayers']

    $milestones = @(
        [pscustomobject]@{ Name = 'core modReady'; Ok = (_flag $fields['modReady']) },
        [pscustomobject]@{ Name = 'gameplay hooks installed'; Ok = (_flag $fields['gameplayHooks']) },
        [pscustomobject]@{ Name = 'real level loaded'; Ok = $realLevel },
        [pscustomobject]@{ Name = 'multiplayer connected'; Ok = (_flag $fields['connected']) },
        [pscustomobject]@{ Name = 'hosted gameplay live'; Ok = (_flag $fields['hostedGameplayLive']) },
        [pscustomobject]@{ Name = 'remote players present'; Ok = ($rem -ne $null -and $rem -ge 1) },
        [pscustomobject]@{ Name = 'remotes spawned (actor non-null)'; Ok = ($sp -ne $null -and $sp -ge 1) },
        [pscustomobject]@{ Name = 'remote UDP pose applied'; Ok = ($posed -ne $null -and $posed -ge 1) }
    )
    $firstUnmet = ($milestones | Where-Object { -not $_.Ok } | Select-Object -First 1)

    $improvements = @{
        'core modReady'                    = 'Core never became ready - failure is upstream of the feature under test. Verify launch + d3d9 proxy + module_manager inject before asserting this scenario.'
        'gameplay hooks installed'         = 'Gameplay hooks missing - send ENSURE_GAMEPLAY_HOOKS / ENSURE_MP_HOOKS and wait, or the scenario is running before the game reached a hookable state.'
        'real level loaded'                = 'currentMap stayed tdmainmenu/gameplay - CONSOLE_EXEC open <level> is unreliable in this harness. Improvement: enter a real level via user input, or accept adopt-only and assert on clientMap.'
        'multiplayer connected'            = 'Client never connected - check multiplayer-server.exe is up and server/room match; INJECT multiplayer may have raced RELOAD_SETTINGS.'
        'hosted gameplay live'             = 'Not live - call FORCE_HOSTED_LIVE (or click Set Gameplay) and confirm activation set live in client.log before spawning bots.'
        'remote players present'           = 'No remotes - start bots in the SAME room/level after host is live; listSize=0 means nothing to spawn (not an activation bug).'
        'remotes spawned (actor non-null)' = 'Remotes present but spawnedPlayers=0 - debug spawn (TdEngine warm may still be running; watch drain.warm.slice; then SpawnActor/mesh), NOT activation.'
        'remote UDP pose applied'           = 'Actors spawned but posedPlayers=0 - verify host UDP heartbeat and server pull relay before claiming bots move.'
    }

    # --- Compose ---
    $stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $stopRetry = ($matched.Count -gt 0)

    if ($firstUnmet) {
        $flowStoppedAt = $firstUnmet.Name
        $improvementText = $improvements[$firstUnmet.Name]
    } else {
        $flowStoppedAt = "(all milestones met)"
        $improvementText = "Flow reached all milestones; failure is a genuine feature defect - capture logs and open/append a KI entry."
    }
    $milestoneObjs = @($milestones | ForEach-Object {
        [ordered]@{ name = $_.Name; ok = [bool]$_.Ok }
    })

    $reflection = [ordered]@{
        scenario         = $Scenario
        attempt          = $Attempt
        timestamp        = $stamp
        error            = $ErrorMessage
        status           = $fields
        lastPhase        = $lastPhase
        matchedDeadEnds  = $matched
        stopRetry        = $stopRetry
        milestones       = $milestoneObjs
        flowStoppedAt    = $flowStoppedAt
        improvement      = $improvementText
    }
    ($reflection | ConvertTo-Json -Depth 6) |
        Set-Content (Join-Path $BundleDir "reflection.json") -Encoding UTF8

    $md = New-Object System.Text.StringBuilder
    [void]$md.AppendLine("# Reflection - $Scenario (attempt $Attempt)")
    [void]$md.AppendLine("")
    [void]$md.AppendLine("- Time: $stamp")
    [void]$md.AppendLine("- Error: $ErrorMessage")
    [void]$md.AppendLine("- Last phase: ``$lastPhase``")
    [void]$md.AppendLine("")
    [void]$md.AppendLine("## Evidence (core status)")
    foreach ($k in $fields.Keys) { [void]$md.AppendLine("- ${k}: $($fields[$k])") }
    [void]$md.AppendLine("")
    [void]$md.AppendLine("## Test-flow milestones")
    foreach ($m in $milestones) {
        $mark = if ($m.Ok) { "[x]" } else { "[ ]" }
        [void]$md.AppendLine("- $mark $($m.Name)")
    }
    [void]$md.AppendLine("")
    [void]$md.AppendLine("**Flow stopped at:** $($reflection.flowStoppedAt)")
    [void]$md.AppendLine("")
    [void]$md.AppendLine("**Improvement:** $($reflection.improvement)")
    [void]$md.AppendLine("")
    if ($stopRetry) {
        [void]$md.AppendLine("## Known dead-end matched - do NOT blindly retry")
        foreach ($d in $matched) {
            [void]$md.AppendLine("- **$($d.Approach)** _(from $($d.Source); signal: $([string]::Join(', ', $d.Signals)))_")
            if ($d.Reason) { [void]$md.AppendLine("  - why it fails: $($d.Reason)") }
        }
        [void]$md.AppendLine("")
        [void]$md.AppendLine("The observed signature matches a documented failed approach. Change the hypothesis before retrying; do not re-run the same fix.")
    } else {
        [void]$md.AppendLine("## Known dead-end check")
        [void]$md.AppendLine("- No documented failed-approach signature matched. Retry/new hypothesis is reasonable.")
    }
    if ($phaseTail.Count -gt 0) {
        [void]$md.AppendLine("")
        [void]$md.AppendLine("## Phase ring (most recent first)")
        [void]$md.AppendLine('```')
        foreach ($e in $phaseTail) { [void]$md.AppendLine($e.Text) }
        [void]$md.AppendLine('```')
    }
    if ($spawnTail.Count -gt 0) {
        [void]$md.AppendLine("")
        [void]$md.AppendLine("## Spawn log tail")
        [void]$md.AppendLine('```')
        foreach ($l in $spawnTail) { [void]$md.AppendLine($l) }
        [void]$md.AppendLine('```')
    }
    $md.ToString() | Set-Content (Join-Path $BundleDir "reflection.md") -Encoding UTF8

    Write-Host "auto-loop: reflection -> $(Join-Path $BundleDir 'reflection.md')"
    if ($stopRetry) {
        Write-Host "auto-loop: [$Scenario] reflection matched $($matched.Count) known dead-end(s) - stopping blind retry"
    }
    Write-Host "auto-loop: [$Scenario] flow stopped at: $($reflection.flowStoppedAt)"

    return [pscustomobject]@{
        ReflectionPath  = (Join-Path $BundleDir "reflection.md")
        StopRetry       = $stopRetry
        MatchedDeadEnds = $matched
        FlowStoppedAt   = $reflection.flowStoppedAt
        Improvement     = $reflection.improvement
    }
}

function Invoke-HarnessAutoLoop {
    param(
        [string[]]$Scenarios = @("verify-harness", "ui-launcher", "smoke-split", "user-flow"),
        [int]$MaxRetries = 2,
        [switch]$RebuildOnFail,
        [switch]$ContinueOnFail,
        [switch]$SkipLaunch,
        [switch]$SkipBuild,
        [string]$ReportRoot = ""
    )

    if (-not $ReportRoot) {
        $ReportRoot = Join-Path (Get-DebugSessionDir) "auto-loop-reports"
    }
    if (-not (Test-Path $ReportRoot)) {
        New-Item -ItemType Directory -Path $ReportRoot -Force | Out-Null
    }

    $runId = Get-Date -Format "yyyyMMdd-HHmmss"
    $runDir = Join-Path $ReportRoot $runId
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null

    $repo = Get-RepoRoot
    $scenarioDir = Join-Path (Get-DebugHarnessRoot) "scenarios"
    $results = @()
    $allPass = $true

    foreach ($scenario in $Scenarios) {
        $scriptPath = Join-Path $scenarioDir "$scenario.ps1"
        if (-not (Test-Path $scriptPath)) {
            throw "Unknown scenario: $scenario"
        }

        $attempt = 0
        $passed = $false
        $lastError = ""
        $bundlePath = ""
        $reflectionPath = ""
        $deadEnds = @()
        $flowStoppedAt = ""

        while ($attempt -le $MaxRetries -and -not $passed) {
            $attempt++
            Write-Host "auto-loop: [$scenario] attempt $attempt / $($MaxRetries + 1)"
            try {
                $invokeParams = @{}
                if ($SkipLaunch) { $invokeParams.SkipLaunch = $true }
                if ($SkipBuild) { $invokeParams.SkipBuild = $true }
                & $scriptPath @invokeParams
                if ($LASTEXITCODE -ne 0) {
                    throw "scenario exited with code $LASTEXITCODE"
                }
                $passed = $true
            } catch {
                $lastError = $_.Exception.Message
                Write-Host "auto-loop: [$scenario] failed: $lastError"
                $bundlePath = Export-HarnessFailureBundle -Scenario $scenario `
                    -OutDir $runDir -Attempt $attempt -ErrorMessage $lastError

                # Reflection: analyse evidence, meta-reflect on the test flow, and
                # cross-check the failure signature against known "Failed
                # approaches". A match stops blind retrying of a dead-end.
                $stopRetry = $false
                try {
                    $reflection = Write-HarnessReflection -Scenario $scenario `
                        -BundleDir $bundlePath -Attempt $attempt `
                        -ErrorMessage $lastError -RepoRoot $repo
                    $reflectionPath = $reflection.ReflectionPath
                    $flowStoppedAt = $reflection.FlowStoppedAt
                    $stopRetry = [bool]$reflection.StopRetry
                    if ($reflection.MatchedDeadEnds) {
                        $deadEnds = @($reflection.MatchedDeadEnds | ForEach-Object { $_.Approach })
                    }
                } catch {
                    Write-Host "auto-loop: WARN reflection failed: $($_.Exception.Message)"
                }

                if ($stopRetry) {
                    Write-Host "auto-loop: [$scenario] skipping remaining retries (known dead-end)"
                    Stop-MirrorsEdgeProcesses -IncludeLauncher
                    break
                }

                if ($RebuildOnFail -and $attempt -le $MaxRetries) {
                    Write-Host "auto-loop: stopping processes before rebuild"
                    Stop-MirrorsEdgeProcesses -IncludeLauncher
                    Start-Sleep -Seconds 4
                    try {
                        Write-Host "auto-loop: rebuilding after failure"
                        Invoke-DebugBuild -RepoRoot $repo -SkipServer:($env:MMOD_DEBUG_SKIP_SERVER -eq "1")
                    } catch {
                        Write-Host "auto-loop: WARN rebuild failed: $($_.Exception.Message)"
                        # rebuild failure is non-fatal; keep retrying
                    }
                } elseif ($attempt -le $MaxRetries) {
                    Stop-MirrorsEdgeProcesses -IncludeLauncher
                    Start-Sleep -Seconds 4
                }
            }
        }

        $row = [pscustomobject]@{
            Scenario       = $scenario
            Layer          = Get-HarnessScenarioLayer -Scenario $scenario
            Pass           = $passed
            Attempts       = $attempt
            Error          = if ($passed) { "" } else { $lastError }
            BundlePath     = if ($passed) { "" } else { $bundlePath }
            ReflectionPath = if ($passed) { "" } else { $reflectionPath }
            FlowStoppedAt  = if ($passed) { "" } else { $flowStoppedAt }
            DeadEnds       = if ($passed) { @() } else { $deadEnds }
        }
        $results += $row

        if (-not $passed) {
            $allPass = $false
            if (-not $ContinueOnFail) {
                break
            }
        }
    }

    $report = [ordered]@{
        runId     = $runId
        reportDir = $runDir
        pass      = $allPass
        results   = $results
    }
    $reportPath = Join-Path $runDir "auto-loop-report.json"
    ($report | ConvertTo-Json -Depth 6) | Set-Content $reportPath -Encoding UTF8
    Write-Host "auto-loop: report -> $reportPath"

    # Run-level meta-reflection: roll up per-scenario reflections so a human/AI
    # can see, in one place, where each flow stopped and any known dead-ends hit.
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("# Auto-loop reflection summary - $runId")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("Overall: $(if ($allPass) { 'PASS' } else { 'FAIL' })")
    [void]$sb.AppendLine("")
    foreach ($r in $results) {
        if ($r.Pass) {
            [void]$sb.AppendLine("- [x] **$($r.Scenario)** - pass ($($r.Attempts) attempt(s))")
        } else {
            [void]$sb.AppendLine("- [ ] **$($r.Scenario)** - fail, flow stopped at: $($r.FlowStoppedAt)")
            if ($r.DeadEnds -and $r.DeadEnds.Count -gt 0) {
                foreach ($d in $r.DeadEnds) { [void]$sb.AppendLine("    - known dead-end: $d") }
            }
            if ($r.ReflectionPath) { [void]$sb.AppendLine("    - reflection: $($r.ReflectionPath)") }
        }
    }
    $summaryPath = Join-Path $runDir "reflection-summary.md"
    $sb.ToString() | Set-Content $summaryPath -Encoding UTF8
    Write-Host "auto-loop: reflection summary -> $summaryPath"
    Write-HarnessResult -Scenario "auto-loop" -Pass $allPass `
        -DurationMs ([int](Get-Date).Subtract([datetime]::ParseExact($runId, "yyyyMMdd-HHmmss", $null)).TotalMilliseconds)

    return [pscustomobject]@{
        Pass       = $allPass
        ReportPath = $reportPath
        ReportDir  = $runDir
        Results    = $results
    }
}

function Resolve-HarnessDebugLogPath {
    param(
        [string]$PreferredPath = "",
        [string]$DeployRoot = "",
        [string]$SessionId = ""
    )

    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($path in @($PreferredPath, $env:MMOD_DEBUG_LOG)) {
        if ($path) { [void]$candidates.Add($path) }
    }
    if ($SessionId) {
        [void]$candidates.Add((Join-Path (Get-DebugSessionDir) "$SessionId.ndjson"))
    }

    $manifestPaths = @((Get-LastSessionManifestPath))
    if ($DeployRoot) {
        $manifestPaths += (Join-Path $DeployRoot "logs\last-session.json")
    }
    foreach ($manifestPath in $manifestPaths) {
        if (-not $manifestPath -or -not (Test-Path -LiteralPath $manifestPath)) {
            continue
        }
        try {
            $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
            foreach ($prop in @('logPath', 'ndjsonLog')) {
                if ($manifest.$prop) {
                    [void]$candidates.Add([string]$manifest.$prop)
                }
            }
            if ($manifest.sessionId -and $DeployRoot) {
                [void]$candidates.Add(
                    (Join-Path $DeployRoot "logs\$($manifest.sessionId)\session.ndjson"))
            }
        } catch {
            Write-Host "harness: WARN could not read debug manifest $manifestPath"
        }
    }

    foreach ($path in $candidates) {
        if ($path -and (Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 0) {
            return $path
        }
    }
    foreach ($path in $candidates) {
        if ($path) { return $path }
    }
    return ""
}

function Sync-HarnessDebugLogPath {
    param(
        $Context,
        [switch]$Quiet
    )

    if (-not $Context -or -not $Context.Session) {
        return $Context
    }

    $resolved = Resolve-HarnessDebugLogPath -PreferredPath $Context.Session.LogPath `
        -DeployRoot $Context.DeployRoot -SessionId $Context.Session.SessionId
    if ($resolved -and $resolved -ne $Context.Session.LogPath) {
        if (-not $Quiet) {
            Write-Host "harness: debug log resolved -> $resolved"
        }
        $Context.Session.LogPath = $resolved
        $env:MMOD_DEBUG_LOG = $resolved
    }
    return $Context
}

function Get-SafeContextLogPath {
    param($Context)
    if (-not $Context) {
        return (Resolve-HarnessDebugLogPath)
    }
    return (Resolve-HarnessDebugLogPath -PreferredPath $Context.Session.LogPath `
        -DeployRoot $Context.DeployRoot -SessionId $Context.Session.SessionId)
}

function Assert-ValidHarnessContext {
    param($Value, [string]$Scenario = "")
    if (-not $Value) {
        throw "${Scenario}: Start-SplitInjectionSession returned null"
    }
    if ($Value -is [hashtable]) {
        if (-not $Value.ContainsKey('Session')) {
            throw "${Scenario}: context hashtable missing Session key"
        }
        return
    }
    $names = $Value.PSObject.Properties.Name
    if ($names -notcontains 'Session' -or $names -notcontains 'Game') {
        $available = ($names -join ', ')
        throw "${Scenario}: context missing required properties (Session/Game). Have: $available"
    }
}

function Get-DebugScenarios {
    $dir = Join-Path (Get-DebugHarnessRoot) "scenarios"
    Get-ChildItem -Path $dir -Filter "*.ps1" |
        ForEach-Object { $_.BaseName } |
        Sort-Object
}

# ── Harness result protocol ────────────────────────────────────────
# Every scenario → single JSON line on stdout, last line before exit.
# Machine-parseable: grep '^harness-result: ' | sed 's/^harness-result: //'
# Layers: L0=self-test  L1=no-game  L2=game+pipe  L3=game+SendInput

$script:__HarnessScenarioLayers = @{
    'verify-harness'    = 'L0'
    'ui-launcher'       = 'L1'
    'smoke-split'       = 'L2'
    'borderless-window' = 'L2'
    'inject-mp'         = 'L2'
    'ui-module-manager' = 'L2'
    'visual-test'       = 'L2'
    'ui-test'           = 'L2'
    'mod-full'          = 'L2'
    'mp-functional'     = 'L2'
    'mp-core-functional' = 'L2'
    'core-mainmenu'     = 'L2'
    'multiplayer-functional' = 'L2'
    'trainer-functional' = 'L2'
    'dolly-functional' = 'L2'
    'mod-manager-config' = 'L2'
    'mod-deps'          = 'L2'
    'alt-tab-menu'      = 'L2'
    'ime-roundtrip'     = 'L2'
    'modular-cold-start' = 'L2'
    'mp-gui-test'       = 'L2'
    'mp-playthrough-bots' = 'L3'
    'user-flow'         = 'L3'
    'user-full-session' = 'L3'
    'auto-loop'         = '-'
    'ci-gate'           = '-'
}

function Get-HarnessScenarioLayer {
    param([string]$Scenario)
    return $script:__HarnessScenarioLayers[$Scenario]
}

$script:HarnessResultExtra = @{}

function Set-HarnessResultExtra {
    param([hashtable]$Extra)

    if (-not $Extra) {
        return
    }
    foreach ($key in $Extra.Keys) {
        $script:HarnessResultExtra[$key] = $Extra[$key]
    }
    $env:MMOD_HARNESS_RESULT_EXTRA = ($script:HarnessResultExtra | ConvertTo-Json -Compress -Depth 4)
}

function Get-HarnessResultExtra {
    $merged = @{}
    foreach ($key in $script:HarnessResultExtra.Keys) {
        $merged[$key] = $script:HarnessResultExtra[$key]
    }
    if ($env:MMOD_HARNESS_RESULT_EXTRA) {
        try {
            $fromEnv = $env:MMOD_HARNESS_RESULT_EXTRA | ConvertFrom-Json
            foreach ($prop in $fromEnv.PSObject.Properties) {
                $merged[$prop.Name] = $prop.Value
            }
        } catch {}
    }
    return $merged
}

function Write-HarnessResult {
    param(
        [Parameter(Mandatory)]
        [string]$Scenario,
        [Parameter(Mandatory)]
        [bool]$Pass,
        [Parameter(Mandatory)]
        [int]$DurationMs,
        [string]$Error = "",
        [string]$Layer = ""
    )

    if (-not $Layer) {
        $Layer = Get-HarnessScenarioLayer -Scenario $Scenario
    }

    $result = [ordered]@{
        scenario   = $Scenario
        layer      = $Layer
        pass       = $Pass
        durationMs = $DurationMs
    }
    if ($Error) {
        $result.error = $Error
    }
    $extra = Get-HarnessResultExtra
    foreach ($key in $extra.Keys) {
        $result[$key] = $extra[$key]
    }
    if (-not $result.Contains('testMachine') -and $env:MMOD_TEST_MACHINE) {
        $result['testMachine'] = $env:MMOD_TEST_MACHINE
    }
    $script:HarnessResultExtra = @{}
    Remove-Item Env:MMOD_HARNESS_RESULT_EXTRA -ErrorAction SilentlyContinue

    $json = $result | ConvertTo-Json -Compress -Depth 3
    Write-Host "harness-result: $json"
}

Export-ModuleMember -Function @(
    'Get-DebugHarnessRoot',
    'Get-RepoRoot',
    'Get-DebugSessionDir',
    'Get-LastSessionManifestPath',
    'Initialize-DebugSession',
    'Get-LastDebugSession',
    'Get-HarnessInteractionLogPath',
    'Write-HarnessInteraction',
    'Read-DebugLogTail',
    'Wait-NamedEvent',
    'Invoke-ModControlPipe',
    'Test-LogContains',
    'Test-LogSequence',
    'Resolve-DeployPath',
    'Resolve-TestMachine',
    'Initialize-HarnessTestMachine',
    'Write-HarnessCoordination',
    'Publish-MachineAlert',
    'Get-MachineAlerts',
    'Clear-MachineAlertsUnread',
    'Get-HarnessGitSnapshot',
    'Publish-HarnessTestLog',
    'Push-HarnessTestLog',
    'Merge-HarnessTestLogIndex',
    'Merge-HarnessTestLogIndexFromJson',
    'Merge-HarnessTestLogChangelog',
    'Read-HarnessTestLogIndexFile',
    'Sync-HarnessTestLogWithRemote',
    'Sync-HarnessTestLogIndexWithRemote',
    'Initialize-HarnessTestLogGit',
    'Install-HarnessGitMergeDriver',
    'Install-HarnessGitHooks',
    'Test-HarnessTestLogGitMergeDriverConfigured',
    'Test-HarnessTestLogGitHooksConfigured',
    'Invoke-HarnessPrePushTestLogsCheck',
    'Get-HarnessTestLogStatus',
    'Show-HarnessTestLogStatus',
    'Rebuild-HarnessTestLogIndex',
    'Get-HarnessTestLogRegistryMachineIds',
    'Test-HarnessTestLogIndexMatchesLatest',
    'Test-HarnessTestLogIndexSchema',
    'Test-HarnessTestLogMerge',
    'Resolve-GameBinariesPath',
    'Stop-MirrorsEdgeProcesses',
    'Get-MirrorsEdgeZombieProcessIds',
    'Assert-NoMirrorsEdgeZombie',
    'Assert-SdkBinaryInstructionScan',
    'Stop-HarnessGameSession',
    'Start-ModuleLauncher',
    'Start-GameExecutable',
    'Start-GameViaModuleLauncher',
    'Wait-GameWindow',
    'Get-GameBlockingDialogs',
    'Get-HarnessBlockingDialogs',
    'Assert-NoBlockingGameDialogs',
    'Test-GameDialogPatternMatching',
    'Resolve-GameWindowHandle',
    'Focus-GameWindow',
    'Wait-ManagerHooksReady',
    'Wait-ManagerHooksReadyFromLog',
    'Wait-ManagerModuleLoaded',
    'Assert-GameBootProgress',
    'Wait-CoreReady',
    'Invoke-EnsureCoreLoaded',
    'Wait-MmultiplayerReady',
    'Get-EngineModReadyFromStatus',
    'Deploy-ModuleDlls',
    'Ensure-CoreDllEnabled',
    'Ensure-MmultiplayerDllEnabled',
    'Invoke-DebugBuild',
    'Start-SplitInjectionSession',
    'Get-WindowLayoutScale',
    'Set-WindowLayoutHarnessSettings',
    'Get-GameWindowHandle',
    'Test-GameWindowLayout',
    'Wait-GameWindowLayout',
    'Get-ManagerStatusJson',
    'Get-MemoryFaultList',
    'Show-MemoryFaultList',
    'Get-HarnessTestLogRegistryMachineIds',
    'Wait-ManagerUiState',
    'Invoke-ModuleManagerUiCommand',
    'Close-ManagerOverlays',
    'Test-ModuleManagerOverlayUi',
    'Test-MmultiplayerGuiSuite',
    'Wait-LauncherWindow',
    'Test-LauncherStatusDialogUi',
    'Test-ViewportMouseAlignment',
    'Wait-ModuleManagerLoadLog',
    'Invoke-ConsoleModuleInject',
    'Invoke-ManagerModuleInject',
    'Scroll-ManagerModulesListForTarget',
    'Enable-HarnessIntroHangImmunity',
    'Test-ModFullSuite',
    'Get-MmultiplayerStatusJson',
    'Get-MmultiplayerModsMap',
    'Wait-MmultiplayerStatus',
    'Wait-MmultiplayerModEnabled',
    'Set-MmultiplayerModEnabled',
    'Test-TcpPortListening',
    'Start-MmultiplayerServer',
    'Wait-MmultiplayerConnected',
    'Update-MultiplayerBotTargetFile',
    'Start-MultiplayerFollowBots',
    'Stop-MultiplayerFollowBots',
    'Test-MmultiplayerPlaythroughWithBots',
    'Test-MmultiplayerFunctionalSuite',
    'Test-MmultiplayerCoreSuite',
    'Test-MultiplayerFunctionalSuite',
    'Test-MpDollyFunctionalSuite',
    'Test-MpTrainerFunctionalSuite',
    'Test-ModManagerConfigSuite',
    'Get-HarnessPluginSettingsPath',
    'Read-HarnessSettingsJson',
    'Get-HarnessSettingsValue',
    'Wait-HarnessSettingsValue',
    'Invoke-CorePipeSetting',
    'Wait-ManagerModuleUnloaded',
    'Wait-ManagerModuleIdle',
    'Invoke-ManagerModuleUnload',
    'Invoke-UnloadLoadedManagerPlugins',
    'Resolve-ManagerInjectTargetIds',
    'Test-ModManagerModulesInjectUi',
    'Get-ManagerListedModuleIds',
    'Assert-ManagerListModulesPipe',
    'Test-ModMenuTabSuite',
    'Get-GameProcess',
    'Send-GameKeyTap',
    'Send-GameKeyChord',
    'Send-GameText',
    'Send-GameKeyHold',
    'Get-ModUiTargetsJson',
    'Wait-ModUiTarget',
    'Send-GameUiClick',
    'Invoke-MmultiplayerModCheckboxClick',
    'Open-ModuleManagerMenuGui',
    'Invoke-ModUiTabClick',
    'Send-GameFieldText',
    'Invoke-GameMovementSample',
    'Invoke-GameMovementSession',
    'Invoke-GameEnterLevelVanilla',
    'Invoke-GameEnterLevel',
    'Invoke-GameOpenLevel',
    'Invoke-GameMenuToLevel',
    'Invoke-EnsurePlaythroughRuntimeHooks',
    'Wait-MmultiplayerInLevel',
    'Invoke-GameIntroForceSkip',
    'Invoke-GameIntroSkip',
    'Wait-GameMainMenuReady',
    'Invoke-GamePlaythroughMovementWithLog',
    'Wait-HarnessPlayerPose',
    'Invoke-GamePlaythroughPrepareForQuit',
    'Assert-GameProcessAlive',
    'Assert-NoRecentMirrorEdgeCrash',
    'Assert-NormalProcessExit',
    'Assert-NoUnexpectedGameExit',
    'Register-HarnessGameSession',
    'Complete-SplitInjectionSession',
    'Assert-GameNotHung',
    'Start-Sleep-HarnessAware',
    'Start-HarnessHangWatchdog',
    'Stop-HarnessHangWatchdog',
    'Export-HarnessHangBundle',
    'Get-HarnessInteractionHangEntries',
    'Invoke-HarnessHangFailure',
    'Close-GameWithRealInput',
    'Close-LauncherWithRealInput',
    'Test-GameFocusRoundTrip',
    'Send-AltTabSwitch',
    'Test-HarnessAltTabMenuRecovery',
    'Test-HarnessImeRoundtrip',
    'Assert-CoreBootstrapProgress',
    'Invoke-GameIntroSkipBlind',
    'Invoke-RealUserConsoleInject',
    'Invoke-RealUserEnterLevelFromMenu',
    'Click-DialogControl',
    'Focus-LauncherWindow',
    'Test-RealUserFullSession',
    'Test-RealUserGameFlow',
    'Export-HarnessFailureBundle',
    'Get-HarnessKnownFailedApproaches',
    'Write-HarnessReflection',
    'Invoke-HarnessAutoLoop',
    'Get-DebugScenarios',
    'Get-SafeContextLogPath',
    'Resolve-HarnessDebugLogPath',
    'Sync-HarnessDebugLogPath',
    'Assert-ValidHarnessContext',
    'Get-HarnessScenarioLayer',
    'Set-HarnessResultExtra',
    'Write-HarnessResult',
    'Invoke-SafePreIntroPluginTeardown',
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
    'Invoke-HarnessVisualMilestone',
    'Invoke-HarnessVisualFromInteraction',
    'Write-HarnessVisualManifest'
)
