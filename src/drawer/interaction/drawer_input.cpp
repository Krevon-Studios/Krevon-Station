// drawer_input.cpp - Drawer hit testing and input actions.
#include "drawer.h"
#include "drawer_input.h"
#include "drawer_animation.h"
#include "../panels/drawer_panel.h"
#include "../render/drawer_render.h"
#include <powrprof.h>

#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "Advapi32.lib")

static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && g_open && g_drawerHwnd)
    {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || 
            wParam == WM_MBUTTONDOWN || wParam == WM_NCLBUTTONDOWN || 
            wParam == WM_NCRBUTTONDOWN || wParam == WM_NCMBUTTONDOWN)
        {
            MSLLHOOKSTRUCT* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            HWND clickedHwnd = WindowFromPoint(ms->pt);

            if (clickedHwnd != g_drawerHwnd && clickedHwnd != g_navbarHwnd)
            {
                Drawer_Toggle(g_snap);
                PostMessageW(g_navbarHwnd, WM_APP_DRAWER_CLOSED, 0, 0);
            }
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

// ── Win32 Power Helper ────────────────────────────────────────────────────────
static void ExecutePowerAction(int item)
{
    if (item == PWR_ITEM_SLEEP)
    {
        SetSuspendState(FALSE, FALSE, FALSE);
    }
    else
    {
        HANDLE hToken;
        TOKEN_PRIVILEGES tkp;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        {
            LookupPrivilegeValue(nullptr, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
            tkp.PrivilegeCount = 1;
            tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, 0);
            CloseHandle(hToken);
        }
        if (item == PWR_ITEM_RESTART)
            ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
        else if (item == PWR_ITEM_SHUTDOWN)
            ExitWindowsEx(EWX_POWEROFF | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
    }
}

int HitButton(D2D1_POINT_2F p)
{
    if (g_activePanel != DrawerPanel::Main) return -1;
    for (int i = 0; i < BTN_COUNT; ++i)
        if (PtInEll(g_btnEllipse[i], p)) return i;
    return -1;
}

int HitPill(D2D1_POINT_2F p)
{
    if (g_activePanel != DrawerPanel::Main) return -1;
    for (int i = 0; i < PILL_COUNT; ++i)
        if (PtInRectF(g_pillRect[i], p)) return i;
    return -1;
}

int HitPowerItem(D2D1_POINT_2F p)
{
    if (g_activePanel != DrawerPanel::Power) return -1;
    for (int i = 0; i < PWR_ITEM_COUNT; ++i)
        if (PtInRectF(g_pwrItemRect[i], p)) return i;
    return -1;
}

void DrawerInput_InstallMouseHook()
{
    if (!g_mouseHook)
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, GetModuleHandleW(nullptr), 0);
}

void DrawerInput_UninstallMouseHook()
{
    if (g_mouseHook)
    {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
}

static void ResetSoundScroll()
{
    g_sndDeviceScrollY = 0.0f;
    g_sndAppScrollY = 0.0f;
    g_sndDeviceScrollTargetY = 0.0f;
    g_sndAppScrollTargetY = 0.0f;
    g_sndDeviceScrollVelY = 0.0f;
    g_sndAppScrollVelY = 0.0f;
}

static void BeginWifiToggleFeedback(HWND hWnd)
{
    g_wifiTogglePending = true;
    g_wifiToggleTargetOn = !g_snap.wifiRadioOn;
    g_snap.wifiRadioOn = g_wifiToggleTargetOn;
    if (!g_snap.wifiRadioOn)
    {
        g_snap.wifi = WifiIconState::Disconnected;
        g_snap.wifiSsid.clear();
    }
    SetTimer(hWnd, HOVER_TIMER, 16, nullptr);
}

static void BeginBtToggleFeedback(HWND hWnd)
{
    g_btTogglePending = true;
    g_btToggleTargetOn = (g_snap.bluetooth == BluetoothIconState::Off);
    g_snap.bluetooth = g_btToggleTargetOn ? BluetoothIconState::On : BluetoothIconState::Off;
    if (!g_btToggleTargetOn)
    {
        g_snap.bluetoothDeviceName.clear();
    }
    SetTimer(hWnd, HOVER_TIMER, 16, nullptr);
}

static bool HasBusyBluetoothDevice()
{
    for (const auto& dev : g_animBtDevices)
    {
        if (dev.data.isConnecting || dev.data.isDisconnecting)
            return true;
    }
    return false;
}

void DrawerPanel_UpdateHover(DrawerPanel panel, D2D1_POINT_2F p)
{
    NotificationPanel_UpdateHover(p);

    const int  newBtn   = (panel == DrawerPanel::Main) ? HitButton(p) : -1;
    const int  newPill  = (panel == DrawerPanel::Main) ? HitPill(p) : -1;
    const bool newVol   = (panel == DrawerPanel::Main) && PtInRectF(g_volIconRect, p);
    const bool newVolSet= (panel == DrawerPanel::Main) && PtInRectF(g_volSetRect, p);
    const bool newAv    = (panel == DrawerPanel::Main) && PtInEll(g_avEllipse, p);
    const int  newPwr   = (panel == DrawerPanel::Power) ? HitPowerItem(p) : -1;
    const bool newBack  = (panel == DrawerPanel::Power || panel == DrawerPanel::Sound || panel == DrawerPanel::Wifi || panel == DrawerPanel::Bluetooth) && PtInEll(g_backEllipse, p);

    int newWifiNet = -1;
    bool newWifiToggle = false;
    bool newWifiRefresh = false;

    if (panel == DrawerPanel::Wifi)
    {
        for (size_t i = 0; i < g_wifiNetRects.size(); ++i)
            if (PtInRectF(g_wifiNetRects[i], p) && PtInRectF(g_wifiClipRect, p)) { newWifiNet = static_cast<int>(i); break; }
        if (PtInRectF(g_wifiToggleRect, p)) newWifiToggle = true;
        if (PtInRectF(g_wifiRefreshRect, p)) newWifiRefresh = true;
    }

    int newBtDev = -1;
    bool newBtToggle = false;
    bool newBtRefresh = false;

    if (panel == DrawerPanel::Bluetooth)
    {
        for (size_t i = 0; i < g_btDevRects.size(); ++i)
            if (PtInRectF(g_btDevRects[i], p) && PtInRectF(g_btClipRect, p)) { newBtDev = static_cast<int>(i); break; }
        if (PtInRectF(g_btToggleRect, p)) newBtToggle = true;
        if (PtInRectF(g_btRefreshRect, p)) newBtRefresh = true;
    }

    int newSndDevice = -1;
    int newSndAppIcon = -1;
    int newSndAppSlider = -1;

    if (panel == DrawerPanel::Sound)
    {
        for (size_t i = 0; i < g_sndDeviceRects.size(); ++i)
            if (PtInRectF(g_sndDeviceRects[i], p) && PtInRectF(g_sndDeviceClipRect, p)) { newSndDevice = static_cast<int>(i); break; }
        for (size_t i = 0; i < g_sndAppIconRects.size(); ++i)
            if (PtInRectF(g_sndAppIconRects[i], p) && PtInRectF(g_sndAppClipRect, p)) { newSndAppIcon = static_cast<int>(i); break; }
        for (size_t i = 0; i < g_sndAppSliderRects.size(); ++i)
        {
            float tMidY = (g_sndAppSliderRects[i].top + g_sndAppSliderRects[i].bottom) * 0.5f;
            if (p.x >= g_sndAppSliderRects[i].left && p.x <= g_sndAppSliderRects[i].right && fabsf(p.y - tMidY) <= THUMB_R + 4.0f && PtInRectF(g_sndAppClipRect, p))
            {
                newSndAppSlider = static_cast<int>(i);
                break;
            }
        }
    }

    if (newBtn != g_btnHov || newPill != g_pillHov || newVol != g_volIconHov || newVolSet != g_volSetHov || newAv != g_avHov ||
        newPwr != g_pwrItemHov || newBack != g_backHov ||
        newSndDevice != g_sndDeviceHovIdx || newSndAppIcon != g_sndAppIconHovIdx || newSndAppSlider != g_sndAppSliderHovIdx ||
        newWifiNet != g_wifiNetHovIdx || newWifiToggle != g_wifiToggleHov || newWifiRefresh != g_wifiRefreshHov ||
        newBtDev != g_btDevHovIdx || newBtToggle != g_btToggleHov || newBtRefresh != g_btRefreshHov)
    {
        g_btnHov = newBtn; g_pillHov = newPill; g_volIconHov = newVol; g_volSetHov = newVolSet; g_avHov = newAv;
        g_pwrItemHov = newPwr; g_backHov = newBack;
        g_sndDeviceHovIdx = newSndDevice; g_sndAppIconHovIdx = newSndAppIcon; g_sndAppSliderHovIdx = newSndAppSlider;
        g_wifiNetHovIdx = newWifiNet; g_wifiToggleHov = newWifiToggle; g_wifiRefreshHov = newWifiRefresh;
        g_btDevHovIdx = newBtDev; g_btToggleHov = newBtToggle; g_btRefreshHov = newBtRefresh;
        SetTimer(g_drawerHwnd, HOVER_TIMER, 16, nullptr);
    }
}

bool DrawerPanel_HandleWheel(DrawerPanel panel, HWND hWnd, D2D1_POINT_2F p, float delta)
{
    if (NotificationPanel_HandleWheel(hWnd, p, delta))
        return true;

    if (panel == DrawerPanel::Wifi)
    {
        g_wifiScrollTargetY -= delta * 60.0f;
        SetTimer(hWnd, SCROLL_TIMER, 16, nullptr);
        return true;
    }

    if (panel == DrawerPanel::Bluetooth)
    {
        g_btScrollTargetY -= delta * 60.0f;
        SetTimer(hWnd, SCROLL_TIMER, 16, nullptr);
        return true;
    }

    if (panel != DrawerPanel::Sound)
        return false;

    if (p.y < g_sndMixerY)
        g_sndDeviceScrollTargetY -= delta * 60.0f;
    else
        g_sndAppScrollTargetY -= delta * 60.0f;
    SetTimer(hWnd, SCROLL_TIMER, 16, nullptr);
    return true;
}

bool DrawerPanel_HandleClick(DrawerPanel panel, HWND hWnd, D2D1_POINT_2F p)
{
    if (NotificationPanel_HandleClick(hWnd, p))
        return true;

    if (panel == DrawerPanel::Power || panel == DrawerPanel::Sound || panel == DrawerPanel::Wifi || panel == DrawerPanel::Bluetooth)
    {
        if (PtInEll(g_backEllipse, p))
        {
            g_panelAnimProgress = 0.0f;
            StartPanelAnim(DrawerPanel::Main);
            return true;
        }
    }

    if (panel == DrawerPanel::Wifi)
    {
        if (PtInRectF(g_wifiToggleRect, p))
        {
            BeginWifiToggleFeedback(hWnd);
            StatusMonitor_ToggleWifi();
            Render();
            return true;
        }
        if (PtInRectF(g_wifiRefreshRect, p))
        {
            g_wifiRefreshClick = true;
            g_wifiRefreshSpinT = 1.0f; // Start 1s spin animation
            StatusMonitor_ScanWifi();
            SetTimer(hWnd, HOVER_TIMER, 16, nullptr); // Use hover timer for spinning
            Render();
            return true;
        }
        for (size_t i = 0; i < g_wifiNetRects.size(); ++i)
        {
            if (PtInRectF(g_wifiNetRects[i], p) && PtInRectF(g_wifiClipRect, p))
            {
                g_wifiNetClickIdx = static_cast<int>(i);
                SetTimer(hWnd, HOVER_TIMER, 16, nullptr);
                
                if (!g_animWifiNetworks[i].data.hasProfile)
                {
                    Drawer_Toggle(g_snap);
                    PostMessageW(g_navbarHwnd, WM_APP_DRAWER_CLOSED, 0, 0);
                    ShellExecuteW(nullptr, L"open", L"ms-settings:network-wifi", nullptr, nullptr, SW_SHOWNORMAL);
                }
                else
                {
                    for (auto& net : g_animWifiNetworks)
                    {
                        net.data.isConnecting = false;
                        net.data.isConnected = false;
                    }
                    g_animWifiNetworks[i].data.isConnecting = true;
                    for (auto& net : g_snap.wifiNetworks)
                    {
                        net.isConnecting = (net.ssid == g_animWifiNetworks[i].data.ssid);
                        if (net.isConnecting)
                            net.isConnected = false;
                    }
                    StatusMonitor_ConnectWifi(g_animWifiNetworks[i].data.ssid, g_animWifiNetworks[i].data.profileName);
                }
                Render();
                return true;
            }
        }
        return false;
    }

    if (panel == DrawerPanel::Bluetooth)
    {
        if (PtInRectF(g_btToggleRect, p))
        {
            BeginBtToggleFeedback(hWnd);
            StatusMonitor_ToggleBluetooth();
            Render();
            return true;
        }
        if (PtInRectF(g_btRefreshRect, p))
        {
            g_btRefreshClick = true;
            g_btRefreshSpinT = 1.0f;
            StatusMonitor_RefreshNonWifi();
            SetTimer(hWnd, HOVER_TIMER, 16, nullptr);
            Render();
            return true;
        }
        for (size_t i = 0; i < g_btDevRects.size(); ++i)
        {
            if (PtInRectF(g_btDevRects[i], p) && PtInRectF(g_btClipRect, p))
            {
                g_btDevClickIdx = static_cast<int>(i);
                SetTimer(hWnd, HOVER_TIMER, 16, nullptr);

                if (HasBusyBluetoothDevice())
                {
                    Render();
                    return true;
                }

                if (g_animBtDevices[i].data.isConnected)
                {
                    // Already connected — disconnect it
                    if (StatusMonitor_DisconnectBluetooth(g_animBtDevices[i].data.addressString))
                    {
                        for (auto& dev : g_animBtDevices)
                        {
                            dev.data.isConnecting = false;
                            dev.data.isDisconnecting = false;
                        }
                        g_animBtDevices[i].data.isDisconnecting = true;
                        for (auto& dev : g_snap.bluetoothDevices)
                        {
                            dev.isConnecting = false;
                            dev.isDisconnecting = (dev.addressString == g_animBtDevices[i].data.addressString);
                        }
                    }
                }
                else
                {
                    // Mark as connecting optimistically
                    if (StatusMonitor_ConnectBluetooth(g_animBtDevices[i].data.addressString))
                    {
                        for (auto& dev : g_animBtDevices)
                        {
                            dev.data.isConnecting = false;
                            dev.data.isDisconnecting = false;
                        }
                        g_animBtDevices[i].data.isConnecting = true;
                        for (auto& dev : g_snap.bluetoothDevices)
                        {
                            dev.isConnecting = (dev.addressString == g_animBtDevices[i].data.addressString);
                            dev.isDisconnecting = false;
                            if (dev.isConnecting)
                                dev.isConnected = false;
                        }
                    }
                }
                Render();
                return true;
            }
        }
        return false;
    }

    if (panel == DrawerPanel::Power)
    {
        int pwrItem = HitPowerItem(p);
        if (pwrItem != -1)
        {
            Drawer_Toggle(g_snap);
            PostMessageW(g_navbarHwnd, WM_APP_DRAWER_CLOSED, 0, 0);
            ExecutePowerAction(pwrItem);
            return true;
        }
        return false;
    }

    if (panel == DrawerPanel::Sound)
    {
        for (size_t i = 0; i < g_sndDeviceRects.size(); ++i)
        {
            if (PtInRectF(g_sndDeviceRects[i], p) && PtInRectF(g_sndDeviceClipRect, p))
            {
                const bool switched = StatusMonitor_SetDefaultAudioEndpoint(g_animEndpoints[i].data.id);
                if (switched)
                {
                    for (auto& ep : g_animEndpoints)
                        ep.data.isActive = (ep.data.id == g_animEndpoints[i].data.id);
                    Render();
                }
                StatusMonitor_RefreshNonWifi();
                return true;
            }
        }

        for (size_t i = 0; i < g_sndAppIconRects.size(); ++i)
        {
            if (PtInRectF(g_sndAppIconRects[i], p) && PtInRectF(g_sndAppClipRect, p))
            {
                bool newMute = !g_animSessions[i].data.muted;
                g_animSessions[i].data.muted = newMute;
                StatusMonitor_SetSessionMute(g_animSessions[i].data.processId, newMute);
                Render();
                return true;
            }
        }

        for (size_t i = 0; i < g_sndAppSliderRects.size(); ++i)
        {
            float tW = g_sndAppSliderRects[i].right - g_sndAppSliderRects[i].left;
            float tMidY = (g_sndAppSliderRects[i].top + g_sndAppSliderRects[i].bottom) * 0.5f;
            if (p.x >= g_sndAppSliderRects[i].left && p.x <= g_sndAppSliderRects[i].right && fabsf(p.y - tMidY) <= THUMB_R + 4.0f && tW > 0.0f && PtInRectF(g_sndAppClipRect, p))
            {
                SetCapture(hWnd);
                g_sndDraggingAppIdx = static_cast<int>(i);
                const float level = Clamp01((p.x - g_sndAppSliderRects[i].left) / tW);
                g_animSessions[i].data.volume = level;
                StatusMonitor_SetSessionVolume(g_animSessions[i].data.processId, level);
                Render();
                return true;
            }
        }
        return false;
    }

    if (panel != DrawerPanel::Main)
        return false;

    if (PtInEll(g_avEllipse, p))
    {
        Drawer_Toggle(g_snap);
        PostMessageW(g_navbarHwnd, WM_APP_DRAWER_CLOSED, 0, 0);
        ShellExecuteW(nullptr, L"open", L"ms-settings:yourinfo", nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }

    for (int i = 0; i < PILL_COUNT; ++i)
    {
        if (PtInRectF(g_pillRect[i], p))
        {
            if (p.x < g_pillRect[i].right - 30.0f)
            {
                if (i == PILL_WIFI)
                {
                    BeginWifiToggleFeedback(hWnd);
                    StatusMonitor_ToggleWifi();
                    Render();
                }
                else if (i == PILL_BT)
                {
                    BeginBtToggleFeedback(hWnd);
                    StatusMonitor_ToggleBluetooth();
                    Render();
                }
            }
            else
            {
                if (i == PILL_WIFI)
                {
                    g_wifiScrollY = 0.0f;
                    g_wifiScrollTargetY = 0.0f;
                    g_panelAnimProgress = 0.0f;
                    StartPanelAnim(DrawerPanel::Wifi);
                    StatusMonitor_ScanWifi();
                }
                else if (i == PILL_BT)
                {
                    g_btScrollY = 0.0f;
                    g_btScrollTargetY = 0.0f;
                    g_panelAnimProgress = 0.0f;
                    StartPanelAnim(DrawerPanel::Bluetooth);
                }
            }
            return true;
        }
    }

    if (PtInRectF(g_volIconRect, p))
    {
        if (!g_snap.volumeMuted)
        {
            g_prevVolumeLevel  = g_snap.volumeLevel;
            g_snap.volumeLevel = 0.0f;
            g_snap.volumeMuted = true;
            g_snap.volume      = VolumeIconState::Muted;
            StatusMonitor_SetMute(true);
        }
        else
        {
            g_snap.volumeLevel = g_prevVolumeLevel;
            g_snap.volumeMuted = false;
            StatusMonitor_SetMute(false);
            StatusMonitor_RefreshNonWifi();
        }
        Render();
        return true;
    }

    if (PtInRectF(g_volSetRect, p))
    {
        ResetSoundScroll();
        g_panelAnimProgress = 0.0f;
        StartPanelAnim(DrawerPanel::Sound);
        return true;
    }

    const float tW = g_sliderTrackRect.right - g_sliderTrackRect.left;
    const float tMidY = (g_sliderTrackRect.top + g_sliderTrackRect.bottom) * 0.5f;
    if (p.x >= g_sliderTrackRect.left && p.x <= g_sliderTrackRect.right && fabsf(p.y - tMidY) <= THUMB_R + 4.0f && tW > 0.0f)
    {
        SetCapture(hWnd);
        g_dragging = true;
        const float level = Clamp01((p.x - g_sliderTrackRect.left) / tW);
        g_snap.volumeLevel = level;
        StatusMonitor_SetVolume(level);
        Render();
        return true;
    }

    switch (HitButton(p))
    {
    case BTN_CAM:
        Drawer_Toggle(g_snap);
        PostMessageW(g_navbarHwnd, WM_APP_DRAWER_CLOSED, 0, 0);
        SetTimer(hWnd, SCREENSHOT_TIMER, static_cast<UINT>(ANIM_OUT_MS) + 30, nullptr);
        return true;
    case BTN_SET:
        Drawer_Toggle(g_snap);
        PostMessageW(g_navbarHwnd, WM_APP_DRAWER_CLOSED, 0, 0);
        ShellExecuteW(nullptr, L"open", L"ms-settings:", nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    case BTN_LOCK:
        Drawer_Toggle(g_snap);
        PostMessageW(g_navbarHwnd, WM_APP_DRAWER_CLOSED, 0, 0);
        LockWorkStation();
        return true;
    case BTN_PWR:
        g_panelAnimProgress = 0.0f;
        StartPanelAnim(DrawerPanel::Power);
        return true;
    default:
        break;
    }

    return true;
}

void DrawerPanel_Reset(DrawerPanel)
{
    g_btnHov = -1; g_pillHov = -1; g_pwrItemHov = -1;
    g_volIconHov = false; g_volSetHov = false; g_avHov = false; g_backHov = false;
    g_sndDeviceHovIdx = -1; g_sndAppIconHovIdx = -1; g_sndAppSliderHovIdx = -1;
    g_wifiNetHovIdx = -1; g_wifiToggleHov = false; g_wifiRefreshHov = false;
    g_btDevHovIdx = -1; g_btToggleHov = false; g_btRefreshHov = false;
    NotificationPanel_ResetHover();
    SetTimer(g_drawerHwnd, HOVER_TIMER, 16, nullptr);
}

bool DrawerInput_HandleMouseWheel(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    ScreenToClient(hWnd, &pt);
    D2D1_POINT_2F p = ToDip(pt.x, pt.y);
    float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
    return DrawerPanel_HandleWheel(g_activePanel, hWnd, p, delta);
}

void DrawerInput_HandleMouseMove(HWND hWnd, WPARAM, LPARAM lParam)
{
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
    TrackMouseEvent(&tme);

    auto p = ToDip(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    DrawerPanel_UpdateHover(g_activePanel, p);

    if (g_dragging)
    {
        const float tW = g_sliderTrackRect.right - g_sliderTrackRect.left;
        if (tW > 0.0f)
        {
            const float level = Clamp01((static_cast<float>(GET_X_LPARAM(lParam)) / g_dpiScale - g_sliderTrackRect.left) / tW);
            g_snap.volumeLevel = level;
            StatusMonitor_SetVolume(level);
            Render();
        }
    }

    if (g_sndDraggingAppIdx != -1 && g_activePanel == DrawerPanel::Sound)
    {
        int idx = g_sndDraggingAppIdx;
        if (idx >= 0 && idx < static_cast<int>(g_sndAppSliderRects.size()))
        {
            const float tW = g_sndAppSliderRects[idx].right - g_sndAppSliderRects[idx].left;
            if (tW > 0.0f)
            {
                const float level = Clamp01((p.x - g_sndAppSliderRects[idx].left) / tW);
                g_snap.audioSessions[idx].volume = level;
                StatusMonitor_SetSessionVolume(g_snap.audioSessions[idx].processId, level);
                Render();
            }
        }
    }
}

void DrawerInput_HandleLButtonDown(HWND hWnd, WPARAM, LPARAM lParam)
{
    auto p = ToDip(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    DrawerPanel_HandleClick(g_activePanel, hWnd, p);
}

void DrawerInput_HandleLButtonUp(HWND)
{
    if (g_dragging)
    {
        g_dragging = false;
        ReleaseCapture();
        StatusMonitor_RefreshNonWifi();
    }
    if (g_sndDraggingAppIdx != -1)
    {
        g_sndDraggingAppIdx = -1;
        ReleaseCapture();
        StatusMonitor_RefreshNonWifi();
    }
    if (g_wifiRefreshClick)
    {
        g_wifiRefreshClick = false;
        Render();
    }
    if (g_btRefreshClick)
    {
        g_btRefreshClick = false;
        Render();
    }
    if (g_wifiNetClickIdx != -1)
    {
        g_wifiNetClickIdx = -1;
        Render();
    }
    if (g_btDevClickIdx != -1)
    {
        g_btDevClickIdx = -1;
        Render();
    }
    if (g_notifClickIdx != -1)
    {
        g_notifClickIdx = -1;
        Render();
    }
    if (g_notifClearClick)
    {
        g_notifClearClick = false;
        Render();
    }
}

void DrawerInput_HandleMouseLeave(HWND hWnd)
{
    DrawerPanel_Reset(g_activePanel);
    SetTimer(hWnd, HOVER_TIMER, 16, nullptr);
}
