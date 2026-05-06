#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

HRESULT DesktopPreview_Init(HINSTANCE hInstance, HWND navbarHwnd);
void DesktopPreview_Shutdown();
void DesktopPreview_Show(int desktopIndex, LONG dotXpx, LONG dotYpx);
void DesktopPreview_Hide();
