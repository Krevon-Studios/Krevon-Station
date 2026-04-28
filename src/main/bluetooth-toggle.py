"""
bluetooth-toggle.py — enable or disable the Bluetooth radio via WinRT.

Uses Windows.Devices.Radios — same API path as Windows Settings (no admin).

Requirements:
  pip install winrt-Windows.Devices.Radios winrt-Windows.Foundation

Usage:
  python bluetooth-toggle.py --enable
  python bluetooth-toggle.py --disable
"""

import sys
import io

if sys.stdout is None:
    sys.stdout = io.TextIOWrapper(io.FileIO(1, closefd=False), encoding='utf-8', line_buffering=True)
elif hasattr(sys.stdout, 'buffer'):
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', line_buffering=True)
if sys.stderr is None:
    sys.stderr = io.TextIOWrapper(io.FileIO(2, closefd=False), encoding='utf-8', line_buffering=True)
elif hasattr(sys.stderr, 'buffer'):
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', line_buffering=True)

import asyncio
import winrt.windows.devices.radios as wdr


async def toggle(enable: bool) -> None:
    all_radios = await wdr.Radio.get_radios_async()
    for radio in all_radios:
        if radio.kind == wdr.RadioKind.BLUETOOTH:
            target = wdr.RadioState.ON if enable else wdr.RadioState.OFF
            await radio.set_state_async(target)
            return
    print('No Bluetooth radio found', file=sys.stderr)


if __name__ == '__main__':
    if '--enable' in sys.argv:
        enable = True
    elif '--disable' in sys.argv:
        enable = False
    else:
        sys.exit(0)

    try:
        asyncio.run(toggle(enable))
    except Exception as e:
        print(str(e), file=sys.stderr, flush=True)
        sys.exit(1)
