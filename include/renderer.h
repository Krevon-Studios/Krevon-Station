#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "status/status_types.h"
#include "virtual_desktop.h"

HRESULT Renderer_Init(HWND hWnd);
void    Renderer_Render(HWND hWnd);
void    Renderer_Resize(UINT width, UINT height);
void    Renderer_Destroy();
void    Renderer_SetStatusSnapshot(const StatusSnapshot& snapshot);
void    Renderer_SetVirtualDesktopSnapshot(const VirtualDesktopSnapshot& snapshot);

// Returns true if (xPx, yPx) in physical pixels hits the status icon group
bool    Renderer_HitTestStatusBar(LONG xPx, LONG yPx);
bool    Renderer_HitTestMediaIsland(LONG xPx, LONG yPx);
bool    Renderer_GetMediaIslandRectPx(RECT* rect);
bool    Renderer_HitTestDesktopPager(LONG xPx, LONG yPx, int* index);
void    Renderer_SetHover(bool hovered);
// Keep the status pill highlighted while the drawer is open
void    Renderer_SetDrawerOpen(bool open);
bool    Renderer_TickAnimation();
UINT    Renderer_GetAnimationTimerIntervalMs();
