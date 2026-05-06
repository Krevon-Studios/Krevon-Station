// drawer.cpp - Drawer module public API and lifecycle.
#include "drawer.h"
#include "../interaction/drawer_animation.h"
#include "../interaction/drawer_input.h"
#include "../panels/drawer_panel.h"
#include "../render/drawer_render.h"
#include "../model/drawer_sound_model.h"
#include "drawer_wndproc.h"
#include "status/notification_status.h"

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

static constexpr wchar_t DRAWER_CLASS[] = L"KrevonDrawer";

HWND Drawer_Create(HINSTANCE hInstance, HWND navbarHwnd)
{
    g_navbarHwnd = navbarHwnd;

    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = DrawerWndProc;
    wcex.hInstance     = hInstance;
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = DRAWER_CLASS;
    RegisterClassExW(&wcex);

    const UINT dpi   = GetDpiForWindow(navbarHwnd);
    const int navH   = MulDiv(NAVBAR_HEIGHT_DIP, dpi, 96);
    const int drawW  = MulDiv(static_cast<int>(D_W), dpi, 96);
    const int drawH  = MulDiv(static_cast<int>(DRAWER_WINDOW_MAX_H), dpi, 96);

    HMONITOR hMon = MonitorFromWindow(navbarHwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    const int x = mi.rcMonitor.right - drawW - MulDiv(8, dpi, 96);
    const int y = mi.rcMonitor.top   + navH  + MulDiv(8, dpi, 96);

    g_drawerHwnd = CreateWindowExW(
        // WS_EX_NOREDIRECTIONBITMAP: tells DWM not to allocate a redirection
        // surface for this window; DComp owns the composited output instead.
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        DRAWER_CLASS, L"KrevonDrawer", WS_POPUP,
        x, y, drawW, drawH,
        navbarHwnd, nullptr, hInstance, nullptr);

    if (!g_drawerHwnd) return nullptr;

    if (FAILED(DrawerInit(g_drawerHwnd)))
    {
        DestroyWindow(g_drawerHwnd);
        g_drawerHwnd = nullptr;
    }
    return g_drawerHwnd;
}

static void SyncWifiPanelLists()
{
    bool changed = false;
    for (auto& a : g_animWifiNetworks) {
        if (!a.isRemoving) {
            auto it = std::find_if(g_snap.wifiNetworks.begin(), g_snap.wifiNetworks.end(),
                [&](const WifiNetwork& e) { return e.ssid == a.data.ssid; });
            if (it == g_snap.wifiNetworks.end()) {
                a.isRemoving = true;
                changed = true;
            } else {
                a.data = *it;
            }
        }
    }
    for (const auto& e : g_snap.wifiNetworks) {
        auto it = std::find_if(g_animWifiNetworks.begin(), g_animWifiNetworks.end(),
            [&](const AnimWifiNetwork& a) { return a.data.ssid == e.ssid; });
        if (it == g_animWifiNetworks.end()) {
            g_animWifiNetworks.push_back({ e, 0.0f, 0.9f, false });
            changed = true;
        } else if (it->isRemoving) {
            it->isRemoving = false;
            it->data = e;
            changed = true;
        } else {
            it->data = e; // Ensure we update the connected state
        }
    }

    // Ensure the animated list order matches the snapshot order (connected at top, then by signal)
    std::sort(g_animWifiNetworks.begin(), g_animWifiNetworks.end(), [](const AnimWifiNetwork& a, const AnimWifiNetwork& b) {
        if (a.isRemoving && !b.isRemoving) return false;
        if (!a.isRemoving && b.isRemoving) return true;
        if (a.data.isConnected && !b.data.isConnected) return true;
        if (!a.data.isConnected && b.data.isConnected) return false;
        return a.data.signalQuality > b.data.signalQuality;
    });

    if (changed && g_drawerHwnd) {
        SetTimer(g_drawerHwnd, SND_LIST_TIMER, 16, nullptr);
    }
}

static void SyncBtPanelLists()
{
    bool changed = false;
    for (auto& a : g_animBtDevices) {
        if (!a.isRemoving) {
            auto it = std::find_if(g_snap.bluetoothDevices.begin(), g_snap.bluetoothDevices.end(),
                [&](const BluetoothDevice& e) { return e.addressString == a.data.addressString; });
            if (it == g_snap.bluetoothDevices.end()) {
                a.isRemoving = true;
                changed = true;
            } else {
                a.data = *it;
            }
        }
    }
    for (const auto& e : g_snap.bluetoothDevices) {
        auto it = std::find_if(g_animBtDevices.begin(), g_animBtDevices.end(),
            [&](const AnimBluetoothDevice& a) { return a.data.addressString == e.addressString; });
        if (it == g_animBtDevices.end()) {
            g_animBtDevices.push_back({ e, 0.0f, 0.9f, false });
            changed = true;
        } else if (it->isRemoving) {
            it->isRemoving = false;
            it->data = e;
            changed = true;
        } else {
            it->data = e; 
        }
    }

    std::sort(g_animBtDevices.begin(), g_animBtDevices.end(), [](const AnimBluetoothDevice& a, const AnimBluetoothDevice& b) {
        if (a.isRemoving && !b.isRemoving) return false;
        if (!a.isRemoving && b.isRemoving) return true;
        
        if (a.data.isConnected && !b.data.isConnected) return true;
        if (!a.data.isConnected && b.data.isConnected) return false;
        return a.data.name < b.data.name;
    });

    if (changed && g_drawerHwnd) {
        SetTimer(g_drawerHwnd, SND_LIST_TIMER, 16, nullptr);
    }
}

static void SyncNotificationPanelLists(const std::vector<NotificationInfo>& notifications)
{
    bool changed = false;
    for (auto& a : g_animNotifications)
    {
        if (!a.isRemoving)
        {
            auto it = std::find_if(notifications.begin(), notifications.end(),
                [&](const NotificationInfo& n) { return a.data.id == n.id && a.data.appUserModelId == n.appUserModelId; });
            if (it == notifications.end())
            {
                a.isRemoving = true;
                changed = true;
            }
            else
            {
                a.data = *it;
            }
        }
    }

    for (const auto& n : notifications)
    {
        auto it = std::find_if(g_animNotifications.begin(), g_animNotifications.end(),
            [&](const AnimNotification& a) { return a.data.id == n.id && a.data.appUserModelId == n.appUserModelId; });
        if (it == g_animNotifications.end())
        {
            g_animNotifications.push_back({ n, 0.0f, 0.96f, false });
            changed = true;
        }
        else if (it->isRemoving)
        {
            it->isRemoving = false;
            it->data = n;
            changed = true;
        }
        else
        {
            it->data = n;
        }
    }

    std::sort(g_animNotifications.begin(), g_animNotifications.end(), [](const AnimNotification& a, const AnimNotification& b) {
        if (a.isRemoving && !b.isRemoving) return false;
        if (!a.isRemoving && b.isRemoving) return true;
        if (a.data.createdUnix != b.data.createdUnix)
            return a.data.createdUnix > b.data.createdUnix;
        return a.data.id > b.data.id;
    });

    if (changed && g_drawerHwnd)
        SetTimer(g_drawerHwnd, SND_LIST_TIMER, 16, nullptr);
}

void Drawer_Toggle(const StatusSnapshot& snapshot)
{
    if (!g_drawerHwnd) return;
    g_snap     = snapshot;
    SyncSoundPanelLists();
    SyncWifiPanelLists();
    SyncBtPanelLists();
    SyncNotificationPanelLists(NotificationStatus_GetNotifications());
    g_animOpen = !g_open;
    g_open     = g_animOpen;

    if (g_open)
    {
        const UINT dpi  = GetDpiForWindow(g_navbarHwnd);
        const int navH  = MulDiv(NAVBAR_HEIGHT_DIP, dpi, 96);
        const int drawW = MulDiv(static_cast<int>(D_W), dpi, 96);
        int drawH = MulDiv(static_cast<int>(DRAWER_WINDOW_MAX_H), dpi, 96);

        HMONITOR hMon = MonitorFromWindow(g_navbarHwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMon, &mi);

        const int x = mi.rcMonitor.right - drawW - MulDiv(8, dpi, 96);
        const int y = mi.rcMonitor.top   + navH  + MulDiv(4, dpi, 96);
        const int minH = MulDiv(static_cast<int>(D_H_MAIN), dpi, 96);
        const int screenH = static_cast<int>(mi.rcMonitor.bottom - y - MulDiv(8, dpi, 96));
        const int availableH = (std::max)(minH, screenH);
        drawH = (std::min)(drawH, availableH);

        SetWindowPos(g_drawerHwnd, HWND_TOPMOST, x, y, drawW, drawH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g_swap->ResizeBuffers(0, static_cast<UINT>(drawW), static_cast<UINT>(drawH),
                              DXGI_FORMAT_UNKNOWN, 0);
        RebuildTarget();
        // Recommit DComp so the resized swap chain stays bound to the HWND visual
        if (g_dcompDevice)
        {
            g_dcompVisual->SetContent(g_swap.Get());
            g_dcompTarget->SetRoot(g_dcompVisual.Get());
            g_dcompDevice->Commit();
        }
        DrawerInput_InstallMouseHook();

        // Reset to Main panel whenever the drawer opens
        g_activePanel = DrawerPanel::Main;
        g_targetPanel = DrawerPanel::Main;
        g_panelAnimProgress = 1.0f;

        StartAnim(1.0f, ANIM_IN_MS);
    }
    else
    {
        DrawerInput_UninstallMouseHook();
        // Kill any in-progress panel transition so it can't fire during the
        // close animation and visually snap the content back to another panel.
        KillTimer(g_drawerHwnd, PANEL_TIMER);
        g_panelAnimDurMs    = 0.0f;
        g_activePanel       = g_targetPanel; // commit whatever state we're at
        StartAnim(0.0f, ANIM_OUT_MS);
    }
}

void Drawer_UpdateSnapshot(const StatusSnapshot& snapshot)
{
    const bool toggleSettled = !g_wifiTogglePending || snapshot.wifiRadioOn == g_wifiToggleTargetOn;
    g_snap = snapshot;
    if (toggleSettled)
        g_wifiTogglePending = false;

    const bool btToggleSettled = !g_btTogglePending || snapshot.bluetooth == (g_btToggleTargetOn ? BluetoothIconState::On : BluetoothIconState::Off) || snapshot.bluetooth == BluetoothIconState::Connected;
    if (btToggleSettled)
        g_btTogglePending = false;

    SyncSoundPanelLists();
    SyncWifiPanelLists();
    SyncBtPanelLists();
    if (g_open) Render();
}

void Drawer_UpdateNotifications(const std::vector<NotificationInfo>& notifications)
{
    SyncNotificationPanelLists(notifications);
    if (g_open) Render();
}

bool Drawer_IsOpen()
{
    return g_open;
}

void Drawer_Destroy()
{
    if (g_drawerHwnd)
    {
        DrawerInput_UninstallMouseHook();
        KillTimer(g_drawerHwnd, ANIM_TIMER);
        DestroyWindow(g_drawerHwnd);
        g_drawerHwnd = nullptr;
    }
    g_avatarBmp.Reset();
    g_svgUserRound.Reset(); g_svgCamera.Reset();  g_svgSettings.Reset();
    g_svgLock.Reset();      g_svgPower.Reset();   g_svgSettings2.Reset();
    g_svgChevron.Reset();
    g_svgSpeaker.Reset();   g_svgHeadphones.Reset();
    g_svgX.Reset();         g_svgTrash.Reset();
    for (auto& s : g_svgWifi) s.Reset();
    for (auto& s : g_svgBt)   s.Reset();
    for (auto& s : g_svgVol)  s.Reset();
    g_brWhite.Reset();  g_brGrey.Reset();  g_brBtn.Reset();   g_brBtnHov.Reset();
    g_brPill.Reset();   g_brPillHov.Reset(); g_brPillIco.Reset();
    g_brTrack.Reset();  g_brFill.Reset();  g_brThumb.Reset(); g_brBg.Reset();
    g_brSep.Reset();
    g_tfName.Reset();   g_tfSub.Reset();   g_tfPct.Reset();   g_tfSndHdg.Reset();
    g_tfNotifHeader.Reset(); g_tfNotifApp.Reset(); g_tfNotifTitle.Reset(); g_tfNotifBody.Reset(); g_tfNotifButton.Reset();
    g_dw.Reset();
    g_target.Reset();   g_ctx.Reset();     g_d2dDev.Reset();  g_fac.Reset();
    // DComp must be released before the swap chain
    g_dcompVisual.Reset(); g_dcompTarget.Reset(); g_dcompDevice.Reset();
    g_swap.Reset();     g_d3d.Reset();
    g_notificationIcons.clear();
}
