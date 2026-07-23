using System;
using System.Runtime.InteropServices;
using System.Text;

class W32 {
    [DllImport("user32.dll")]public static extern IntPtr FindWindow(string cn,string tn);
    [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr d,int id);
    [DllImport("user32.dll", CharSet=CharSet.Auto, EntryPoint="SendMessageW")]
    public static extern IntPtr SendMessageStr(IntPtr h,uint m,int w,StringBuilder l);
    [DllImport("user32.dll", EntryPoint="SendMessageW")]
    public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
    [DllImport("user32.dll")]public static extern IntPtr GetWindow(IntPtr h,uint r);
    public const uint GW_CHILD=5;
    public const uint GW_HWNDNEXT=2;
    public const uint WM_GETTEXT=0x000D;
    public const uint BM_CLICK=0x00F5;
    
    public static void Main() {
        IntPtr h = FindWindow("#32770", "Message");
        if (h != IntPtr.Zero) {
            IntPtr child = GetWindow(h, GW_CHILD);
            while (child != IntPtr.Zero) {
                StringBuilder sb = new StringBuilder(1024);
                SendMessageStr(child, WM_GETTEXT, 1024, sb);
                string txt = sb.ToString();
                if (txt.Length > 0) {
                    Console.WriteLine("Child: " + txt);
                }
                child = GetWindow(child, GW_HWNDNEXT);
            }
            
            IntPtr ok = GetDlgItem(h, 2);
            if (ok != IntPtr.Zero) {
                SendMessage(ok, BM_CLICK, IntPtr.Zero, IntPtr.Zero);
                Console.WriteLine("Dismissed");
            }
        } else {
            Console.WriteLine("No Message dialog");
        }
    }
}
