$ErrorActionPreference = "Stop"

$outDir    = Join-Path $PWD "resources\python"
$buildDir  = "build\pyinstaller"
$specDir   = "build\pyinstaller"
$srcDir    = Join-Path $PWD "src\main"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

# Per-script PyInstaller flags.
#
# audio-monitor   — pycaw uses comtypes which generates COM type-library wrappers at build time.
#                   Without --collect-all=comtypes the generated stubs are missing on other
#                   machines and every COM callback fails silently.
#
# network-monitor — psutil ships platform-specific binary extensions loaded dynamically;
#                   --collect-all ensures every DLL/pyd is bundled.
#
# notification-monitor — winrt-* are namespace packages with sub-modules discovered at
#                        runtime; PyInstaller misses them without --collect-all.
#
# wifi-scan / wifi-toggle — pure ctypes against system DLLs, no extra collection needed.

$scriptConfig = @{
    "audio-monitor.py" = @(
        "--collect-all=comtypes",
        "--collect-all=pycaw",
        "--hidden-import=comtypes.stream",
        "--hidden-import=pycaw.pycaw"
    )
    "network-monitor.py" = @(
        "--collect-all=psutil"
    )
    "notification-monitor.py" = @(
        "--collect-all=winrt",
        "--hidden-import=winrt.windows.foundation",
        "--hidden-import=winrt.windows.foundation.collections",
        "--hidden-import=winrt.windows.ui.notifications",
        "--hidden-import=winrt.windows.ui.notifications.management"
    )
    "wifi-scan.py"         = @()
    "wifi-toggle.py"       = @()
    # bluetooth-scan uses WinRT for radio state + bthprops.dll for device enumeration
    "bluetooth-scan.py"    = @(
        "--collect-all=winrt",
        "--hidden-import=winrt.windows.devices.radios",
        "--hidden-import=winrt.windows.foundation"
    )
    # bluetooth-toggle uses winrt Radio API — same as Windows Settings (no admin)
    "bluetooth-toggle.py"  = @(
        "--collect-all=winrt",
        "--hidden-import=winrt.windows.devices.radios",
        "--hidden-import=winrt.windows.foundation"
    )
    "bluetooth-connect.py" = @()
}

Write-Host "Compiling Python scripts into executables..."

foreach ($script in $scriptConfig.Keys) {
    Write-Host ""
    Write-Host "==> $script"
    $scriptPath  = Join-Path $srcDir $script
    $extraFlags  = $scriptConfig[$script]

    $pyArgs = @(
        "-m", "PyInstaller",
        "--onefile",
        "--distpath", $outDir,
        "--workpath", $buildDir,
        "--specpath", $specDir
    ) + $extraFlags + @($scriptPath)

    py @pyArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "PyInstaller failed for $script (exit $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "Done! Executables written to $outDir"
