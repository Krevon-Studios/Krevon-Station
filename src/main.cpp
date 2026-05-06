#include "window.h"
#include <objbase.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_     LPWSTR,
    _In_     int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    timeBeginPeriod(1);

    if (!Window_RegisterClass(hInstance))
        return 1;

    HWND hWnd = Window_Create(hInstance);
    if (!hWnd)
        return 1;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    timeEndPeriod(1);
    return static_cast<int>(msg.wParam);
}
