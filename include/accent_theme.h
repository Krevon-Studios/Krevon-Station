#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_3.h>

#define WM_APP_ACCENT_CHANGED (WM_APP + 5)

struct AccentThemePalette
{
    D2D1_COLOR_F accent;
    D2D1_COLOR_F fill;
    D2D1_COLOR_F pill;
    D2D1_COLOR_F pillHover;
    D2D1_COLOR_F pillIcon;
};

void AccentTheme_Init(HWND notifyHwnd);
void AccentTheme_Shutdown();
AccentThemePalette AccentTheme_GetPalette();
