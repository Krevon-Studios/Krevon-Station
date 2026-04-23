$code = @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32;

public class RegMonitor {
    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern int RegNotifyChangeKeyValue(
        IntPtr hKey,
        bool bWatchSubtree,
        uint dwNotifyFilter,
        IntPtr hEvent,
        bool fAsynchronous);
        
    [DllImport("advapi32.dll", CharSet = CharSet.Auto)]
    public static extern int RegOpenKeyEx(
        IntPtr hKey,
        string lpSubKey,
        uint ulOptions,
        int samDesired,
        out IntPtr phkResult);
        
    public static void Watch() {
        IntPtr HKEY_CURRENT_USER = new IntPtr(unchecked((int)0x80000001));
        IntPtr hKey;
        // KEY_NOTIFY (0x0010) | KEY_QUERY_VALUE (0x0001)
        if (RegOpenKeyEx(HKEY_CURRENT_USER, @"SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops", 0, 0x0011, out hKey) == 0) {
            while (true) {
                // Read current values
                using (RegistryKey rk = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops")) {
                    if (rk != null) {
                        byte[] ids = rk.GetValue("VirtualDesktopIDs") as byte[];
                        byte[] cur = rk.GetValue("CurrentVirtualDesktop") as byte[];
                        string idStr = ids != null ? BitConverter.ToString(ids).Replace("-", "") : "";
                        string curStr = cur != null ? BitConverter.ToString(cur).Replace("-", "") : "";
                        Console.WriteLine(idStr + "|" + curStr);
                    }
                }
                
                // Block until change (REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_ATTRIBUTES | REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_SECURITY)
                // We'll use REG_NOTIFY_CHANGE_LAST_SET (0x00000004)
                RegNotifyChangeKeyValue(hKey, false, 0x00000004, IntPtr.Zero, false);
            }
        }
    }
}
"@
Add-Type -TypeDefinition $code

[RegMonitor]::Watch()
