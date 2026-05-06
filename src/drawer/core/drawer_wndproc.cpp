// drawer_wndproc.cpp - Thin window procedure for the drawer window.
#include "drawer_wndproc.h"
#include "../interaction/drawer_animation.h"
#include "../interaction/drawer_input.h"
#include "../render/drawer_render.h"

LRESULT CALLBACK DrawerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TIMER:
        if (DrawerAnimation_HandleTimer(hWnd, wParam))
            return 0;
        break;

    case WM_MOUSEWHEEL:
        if (DrawerInput_HandleMouseWheel(hWnd, wParam, lParam))
            return 0;
        break;

    case WM_MOUSEMOVE:
        DrawerInput_HandleMouseMove(hWnd, wParam, lParam);
        return 0;

    case WM_LBUTTONDOWN:
        DrawerInput_HandleLButtonDown(hWnd, wParam, lParam);
        return 0;

    case WM_LBUTTONUP:
        DrawerInput_HandleLButtonUp(hWnd);
        return 0;

    case WM_MOUSELEAVE:
        DrawerInput_HandleMouseLeave(hWnd);
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        Render();
        EndPaint(hWnd, &ps);
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
