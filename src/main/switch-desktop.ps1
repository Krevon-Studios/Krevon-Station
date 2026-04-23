$code = @"
using System;
using System.Runtime.InteropServices;

public static class DesktopSwitcher {
    [DllImport("user32.dll")]
    private static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);

    private const uint KEYEVENTF_KEYUP = 0x0002;

    public static void SwitchWithHotkeys(int from, int to) {
        int diff = to - from;
        byte arrow = diff > 0 ? (byte)0x27 : (byte)0x25;
        int steps = Math.Abs(diff);

        keybd_event(0x5B, 0, 0, UIntPtr.Zero);
        keybd_event(0x11, 0, 0, UIntPtr.Zero);
        for (int i = 0; i < steps; i++) {
            keybd_event(arrow, 0, 0, UIntPtr.Zero);
            keybd_event(arrow, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        }
        keybd_event(0x11, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        keybd_event(0x5B, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
    }
}
"@

Add-Type -TypeDefinition $code

$helperCandidates = @(
    (Join-Path $PSScriptRoot 'vendor\VirtualDesktopHelper.exe'),
    (Join-Path $PSScriptRoot '..\src\main\vendor\VirtualDesktopHelper.exe'),
    (Join-Path $PSScriptRoot '..\main\vendor\VirtualDesktopHelper.exe')
)

$helperPath = $null
foreach ($candidate in $helperCandidates) {
    if (-not (Test-Path -LiteralPath $candidate)) { continue }

    & $candidate /Quiet /GetCurrentDesktop | Out-Null
    if ($LASTEXITCODE -ge 0) {
        $helperPath = $candidate
        break
    }

    [Console]::Error.WriteLine("virtual desktop helper check failed for '$candidate' with exit code $LASTEXITCODE")
}

if (-not $helperPath) {
    [Console]::Error.WriteLine('virtual desktop helper unavailable; using Win+Ctrl+Arrow fallback')
}

while ($true) {
    $line = [Console]::ReadLine()
    if ($null -eq $line) { break }

    $parts = $line.Trim() -split '\|'
    if ($parts.Length -lt 2) { continue }

    $to = [int]$parts[0]
    $from = [int]$parts[1]

    if ($to -eq $from) { continue }

    if ($helperPath) {
        & $helperPath /Quiet /Animation:Off /Switch:$to | Out-Null
        if ($LASTEXITCODE -ge 0) {
            continue
        }

        [Console]::Error.WriteLine("virtual desktop helper switch failed with exit code $LASTEXITCODE")
    }

    [DesktopSwitcher]::SwitchWithHotkeys($from, $to)
}
