using System;
using System.Runtime.InteropServices;

class KB {
    [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")]public static extern void keybd_event(byte vk,byte s,uint f,IntPtr e);
    [DllImport("user32.dll")]public static extern bool AllowSetForegroundWindow(int pid);
    [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
    [DllImport("user32.dll")]public static extern bool BringWindowToTop(IntPtr h);
    public const uint KEYEVENTF_KEYUP=2;

    static void Main(string[] args) {
        uint pid = 0;
        if (args.Length > 0) uint.TryParse(args[0], out pid);
        
        IntPtr hwnd = IntPtr.Zero;
        var procs = System.Diagnostics.Process.GetProcessesByName("MirrorsEdge");
        if (procs.Length > 0) hwnd = procs[0].MainWindowHandle;
        
        Console.WriteLine("HWND: " + hwnd);
        if (hwnd == IntPtr.Zero) return;
        
        AllowSetForegroundWindow((int)pid);
        System.Threading.Thread.Sleep(50);
        ShowWindow(hwnd, 5);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        System.Threading.Thread.Sleep(100);
        
        // Enter x5
        for (int i=0;i<5;i++) { keybd_event(0x0D,0,0,IntPtr.Zero); System.Threading.Thread.Sleep(50); keybd_event(0x0D,0,KEYEVENTF_KEYUP,IntPtr.Zero); System.Threading.Thread.Sleep(300); }
        // Escape x3
        for (int i=0;i<3;i++) { keybd_event(0x1B,0,0,IntPtr.Zero); System.Threading.Thread.Sleep(50); keybd_event(0x1B,0,KEYEVENTF_KEYUP,IntPtr.Zero); System.Threading.Thread.Sleep(300); }
        
        Console.WriteLine("Keys sent");
    }
}
