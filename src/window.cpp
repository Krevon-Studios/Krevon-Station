#include "window.h"
#include "accent_theme.h"
#include "shell.h"
#include "renderer.h"
#include "drawer.h"
#include "media_island.h"
#include "desktop_preview.h"
#include "status/status_monitor.h"
#include "status/notification_status.h"
#include "virtual_desktop.h"
#include "updater.h"
#include <windowsx.h>

static constexpr wchar_t CLASS_NAME[] = L"KrevonStationBar";
static constexpr UINT_PTR HOVER_ANIM_TIMER_ID = 1;
static constexpr UINT_PTR STATUS_REFRESH_TIMER_ID = 2;
static constexpr UINT_PTR VIRTUAL_DESKTOP_REFRESH_TIMER_ID = 3;
static constexpr UINT_PTR CLOCK_TIMER_ID = 4;
static constexpr UINT HOVER_ANIM_TIMER_MS = 16;
static constexpr UINT STATUS_REFRESH_TIMER_MS = 5000;
static constexpr UINT VIRTUAL_DESKTOP_REFRESH_TIMER_MS = 250;
static constexpr UINT CLOCK_TIMER_MS = 1000;

static void RefreshVirtualDesktopPager(HWND hWnd, bool forcePaint)
{
    const VirtualDesktopSnapshot before = VirtualDesktop_GetSnapshot();
    VirtualDesktop_Refresh();
    const VirtualDesktopSnapshot after = VirtualDesktop_GetSnapshot();

    if (forcePaint || before != after)
    {
        Renderer_SetVirtualDesktopSnapshot(after);
        SetTimer(hWnd, HOVER_ANIM_TIMER_ID, HOVER_ANIM_TIMER_MS, nullptr);
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        AppBar_Register(hWnd);
        Tray_Add(hWnd);
        AccentTheme_Init(hWnd);
        Renderer_Init(hWnd);
        VirtualDesktop_Init();
        Renderer_SetVirtualDesktopSnapshot(VirtualDesktop_GetSnapshot());
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        DesktopPreview_Init(cs->hInstance, hWnd);
        StatusMonitor_Init(hWnd);
        NotificationStatus_Init(hWnd);
        Updater_CheckForUpdatesAsync(hWnd, false);
        Renderer_SetStatusSnapshot(StatusMonitor_GetSnapshot());
        SetTimer(hWnd, STATUS_REFRESH_TIMER_ID, STATUS_REFRESH_TIMER_MS, nullptr);
        SetTimer(hWnd, VIRTUAL_DESKTOP_REFRESH_TIMER_ID, VIRTUAL_DESKTOP_REFRESH_TIMER_MS, nullptr);
        SetTimer(hWnd, CLOCK_TIMER_ID, CLOCK_TIMER_MS, nullptr);
        // Create the quick-settings drawer panel
        Drawer_Create(cs->hInstance, hWnd);
        MediaIsland_Create(cs->hInstance, hWnd);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hWnd, HOVER_ANIM_TIMER_ID);
        KillTimer(hWnd, STATUS_REFRESH_TIMER_ID);
        KillTimer(hWnd, VIRTUAL_DESKTOP_REFRESH_TIMER_ID);
        KillTimer(hWnd, CLOCK_TIMER_ID);
        DesktopPreview_Shutdown();
        MediaIsland_Destroy();
        Drawer_Destroy();
        NotificationStatus_Shutdown();
        StatusMonitor_Shutdown();
        VirtualDesktop_Shutdown();
        Renderer_Destroy();
        AccentTheme_Shutdown();
        Tray_Remove(hWnd);
        AppBar_Remove(hWnd);
        PostQuitMessage(0);
        return 0;

    // Notify the system when the appbar is activated
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            APPBARDATA abd = { sizeof(abd), hWnd };
            SHAppBarMessage(ABM_ACTIVATE, &abd);
        }
        return 0;

    // Notify the system whenever our position changes
    case WM_WINDOWPOSCHANGED:
    {
        APPBARDATA abd = { sizeof(abd), hWnd };
        SHAppBarMessage(ABM_WINDOWPOSCHANGED, &abd);
        return 0;
    }

    // System notifications: taskbar moved, full-screen app, autohide state
    case WM_APPBAR_CALLBACK:
        switch (static_cast<UINT>(wParam))
        {
        case ABN_POSCHANGED:
            // Another appbar or the taskbar changed — re-negotiate our position
            AppBar_SetPos(hWnd);
            break;

        case ABN_STATECHANGE:
        {
            APPBARDATA abd   = { sizeof(abd), hWnd };
            UINT       state = static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &abd));
            SetWindowPos(hWnd,
                (state & ABS_ALWAYSONTOP) ? HWND_TOPMOST : HWND_BOTTOM,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        }

        case ABN_FULLSCREENAPP:
            // Drop below full-screen apps; come back when they close
            SetWindowPos(hWnd,
                lParam ? HWND_BOTTOM : HWND_TOPMOST,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        }
        return 0;

    case WM_DPICHANGED:
        AppBar_SetPos(hWnd);
        return 0;

    case WM_SIZE:
        Renderer_Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_DEVICECHANGE:
        StatusMonitor_RefreshAll();
        return 0;

    case WM_APP_STATUS_PROVIDER_EVENT:
        if (wParam == STATUS_PROVIDER_EVENT_WIFI)
            StatusMonitor_RefreshWifiAsync();
        else if (wParam == STATUS_PROVIDER_EVENT_MEDIA)
            StatusMonitor_RefreshMedia(true);
        else if (wParam == STATUS_PROVIDER_EVENT_MEDIA_READY)
            StatusMonitor_RefreshMedia(false);
        else
            StatusMonitor_RefreshNonWifi();
        return 0;

    case WM_APP_STATUS_CHANGED:
        Renderer_SetStatusSnapshot(StatusMonitor_GetSnapshot());
        MediaIsland_UpdateSnapshot(StatusMonitor_GetSnapshot());
        Drawer_UpdateSnapshot(StatusMonitor_GetSnapshot());
        SetTimer(hWnd, HOVER_ANIM_TIMER_ID, HOVER_ANIM_TIMER_MS, nullptr);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_APP_NOTIFICATIONS_CHANGED:
        Drawer_UpdateNotifications(NotificationStatus_GetNotifications());
        return 0;

    case WM_APP_ACCENT_CHANGED:
        Renderer_UpdateAccentTheme();
        MediaIsland_UpdateAccentTheme();
        Drawer_UpdateAccentTheme();
        SetTimer(hWnd, HOVER_ANIM_TIMER_ID, HOVER_ANIM_TIMER_MS, nullptr);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    // Drawer closed itself via a button — clear the pill highlight
    case WM_APP_DRAWER_CLOSED:
        Renderer_SetDrawerOpen(false);
        SetTimer(hWnd, HOVER_ANIM_TIMER_ID, HOVER_ANIM_TIMER_MS, nullptr);
        return 0;

    case WM_MOUSEMOVE:
    {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);

        const bool hit = Renderer_HitTestStatusBar(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        Renderer_SetHover(hit);

        if (Renderer_HitTestMediaIsland(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
            MediaIsland_Show(StatusMonitor_GetSnapshot());

        int desktopIndex = -1;
        if (Renderer_HitTestDesktopPager(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &desktopIndex))
            DesktopPreview_Show(desktopIndex, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        else
            DesktopPreview_Hide();

        SetTimer(hWnd, HOVER_ANIM_TIMER_ID, HOVER_ANIM_TIMER_MS, nullptr);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        int desktopIndex = -1;
        const bool isDesktopPagerHit = (msg == WM_LBUTTONDOWN)
            && Renderer_HitTestDesktopPager(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &desktopIndex);
        if (isDesktopPagerHit)
        {
            DesktopPreview_Hide();
            VirtualDesktop_SwitchToIndex(desktopIndex);
            RefreshVirtualDesktopPager(hWnd, true);
            return 0;
        }

        // Click on status cluster → toggle the drawer
        const bool isStatusHit = (msg == WM_LBUTTONDOWN) && Renderer_HitTestStatusBar(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (isStatusHit)
        {
            DesktopPreview_Hide();
            Drawer_Toggle(StatusMonitor_GetSnapshot());
            // Keep pill lit while drawer is open; fade out when it closes
            Renderer_SetDrawerOpen(Drawer_IsOpen());
            SetTimer(hWnd, HOVER_ANIM_TIMER_ID, HOVER_ANIM_TIMER_MS, nullptr);
        }
        else if (Drawer_IsOpen())
        {
            DesktopPreview_Hide();
            Drawer_Toggle(StatusMonitor_GetSnapshot());
            Renderer_SetDrawerOpen(false);
            SetTimer(hWnd, HOVER_ANIM_TIMER_ID, HOVER_ANIM_TIMER_MS, nullptr);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        int desktopIndex = -1;
        if (Renderer_HitTestDesktopPager(pt.x, pt.y, &desktopIndex))
        {
            const VirtualDesktopSnapshot snapshot = VirtualDesktop_GetSnapshot();
            if (snapshot.available && snapshot.count > 1 && snapshot.currentIndex >= 0)
            {
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                const int next = snapshot.currentIndex + (delta < 0 ? 1 : -1);
                const int target = (std::max)(0, (std::min)(snapshot.count - 1, next));
                if (target != snapshot.currentIndex)
                {
                    DesktopPreview_Hide();
                    VirtualDesktop_SwitchToIndex(target);
                    RefreshVirtualDesktopPager(hWnd, true);
                }
            }
            return 0;
        }
        break;
    }

    case WM_MOUSELEAVE:
        DesktopPreview_Hide();
        Renderer_SetHover(false);
        SetTimer(hWnd, HOVER_ANIM_TIMER_ID, HOVER_ANIM_TIMER_MS, nullptr);
        return 0;

    // D3D swap chain owns the surface — suppress GDI erase and paint
    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wParam == HOVER_ANIM_TIMER_ID)
        {
            const bool animating = Renderer_TickAnimation();
            Renderer_Render(hWnd);
            ValidateRect(hWnd, nullptr);  // suppress redundant WM_PAINT
            if (!animating)
                KillTimer(hWnd, HOVER_ANIM_TIMER_ID);
            else
                SetTimer(hWnd, HOVER_ANIM_TIMER_ID, Renderer_GetAnimationTimerIntervalMs(), nullptr);
            return 0;
        }
        if (wParam == STATUS_REFRESH_TIMER_ID)
        {
            StatusMonitor_RefreshAll();
            return 0;
        }
        if (wParam == VIRTUAL_DESKTOP_REFRESH_TIMER_ID)
        {
            RefreshVirtualDesktopPager(hWnd, false);
            return 0;
        }
        if (wParam == CLOCK_TIMER_ID)
        {
            MediaIsland_TickClock();
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        Renderer_Render(hWnd);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_CHECK_UPDATES, L"Check for updates...");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Quit");
            // Must set foreground window or menu won't dismiss on click-away
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_TRAY_CHECK_UPDATES)
            Updater_CheckForUpdatesAsync(hWnd, true);
        else if (LOWORD(wParam) == IDM_TRAY_EXIT)
            DestroyWindow(hWnd);
        return 0;

    // Block OS move/resize hit-test targets — we own our position entirely
    case WM_NCHITTEST:
        return HTCLIENT;

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

ATOM Window_RegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex    = { sizeof(wcex) };
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.hInstance      = hInstance;
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wcex.lpszClassName  = CLASS_NAME;
    return RegisterClassExW(&wcex);
}

HWND Window_Create(HINSTANCE hInstance)
{
    // Size is irrelevant here — AppBar_SetPos called in WM_CREATE
    // resizes and repositions before the window is ever shown
    return CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        CLASS_NAME,
        L"KrevonStation",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr, hInstance, nullptr);
}
