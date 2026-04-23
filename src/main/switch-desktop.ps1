$code = @"
using System;
using System.Runtime.InteropServices;

public static class DesktopSwitcher {
    [DllImport("user32.dll")]
    static extern void keybd_event(byte vk, byte scan, uint flags, uint extra);

    public static void Switch(int from, int to) {
        int diff   = to - from;
        byte arrow = diff > 0 ? (byte)0x27 : (byte)0x25; // VK_RIGHT / VK_LEFT
        int steps  = Math.Abs(diff);

        keybd_event(0x5B, 0, 0, 0); // LWin  ↓
        keybd_event(0x11, 0, 0, 0); // Ctrl  ↓
        for (int i = 0; i < steps; i++) {
            keybd_event(arrow, 0, 0, 0);
            keybd_event(arrow, 0, 2, 0); // KEYEVENTF_KEYUP
        }
        keybd_event(0x11, 0, 2, 0); // Ctrl  ↑
        keybd_event(0x5B, 0, 2, 0); // LWin  ↑
    }
}
"@

Add-Type -TypeDefinition $code

# Read stdin lines: "targetIndex|fromIndex"
while ($true) {
    $line = [Console]::ReadLine()
    if ($null -eq $line) { break }
    $parts = $line.Trim() -split '\|'
    if ($parts.Length -lt 2) { continue }
    $to   = [int]$parts[0]
    $from = [int]$parts[1]
    if ($to -ne $from) {
        [DesktopSwitcher]::Switch($from, $to)
    }
}
