Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;using System.Text;
public class W32 {
    [DllImport("user32.dll")]public static extern IntPtr FindWindow(string cn,string tn);
    [DllImport("user32.dll")]public static extern IntPtr FindWindowEx(IntPtr p,IntPtr c,string cn,string tn);
    [DllImport("user32.dll")]public static extern int GetWindowText(IntPtr h,StringBuilder t,int m);
    [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
    [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr d,int id);
    public const uint WM_CLOSE=0x0010;
    public const uint BM_CLICK=0x00F5;
}
'@

$h = [W32]::FindWindow("#32770", "Message")
Write-Host "Message window: $h"

if ($h -ne [IntPtr]::Zero) {
    $sb = New-Object System.Text.StringBuilder(512)
    [W32]::GetWindowText($h, $sb, 512) | Out-Null
    
    # Find OK button
    for ($id=1; $id -le 10; $id++) {
        $c = [W32]::GetDlgItem($h, $id)
        if ($c -ne [IntPtr]::Zero) {
            $sb2 = New-Object System.Text.StringBuilder(256)
            [W32]::GetWindowText($c, $sb2, 256) | Out-Null
            Write-Host "  Child $id : '$($sb2.ToString())'"
        }
    }
    
    # Click first button (usually OK)
    $ok = [W32]::GetDlgItem($h, 1)
    if ($ok -ne [IntPtr]::Zero) {
        Write-Host "Clicking button..."
        [W32]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -M 100
        [W32]::SendMessage($ok, [W32]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    } else {
        # Close the dialog
        [W32]::SendMessage($h, [W32]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    }
}

# Monitor game after dismissal
Start-Sleep 5
$p = Get-Process "MirrorsEdge" -EA 0
if ($p) {
    Write-Host "Game Title: $($p.MainWindowTitle) HWND: $($p.MainWindowHandle)"
    Write-Host "CPU: $($p.CPU)s WS: $([math]::Round($p.WorkingSet64/1MB))MB Threads: $($p.Threads.Count)"
}
