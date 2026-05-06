// drawer_animation.cpp - Timer-driven drawer animation helpers.
#include "drawer_animation.h"
#include "../panels/drawer_panel.h"
#include "../render/drawer_render.h"

void StartAnim(float target, float durMs)
{
    g_animFrom   = g_animProgress;
    g_animTarget = target;
    g_animStart  = GetTickCount64();
    g_animDurMs  = durMs;
    SetTimer(g_drawerHwnd, ANIM_TIMER, 16, nullptr);
}

// Returns true while animation is still running.
bool TickAnim()
{
    if (g_animDurMs <= 0.0f) return false;
    const float t = Clamp01(static_cast<float>(GetTickCount64() - g_animStart) / g_animDurMs);
    g_animProgress = g_animFrom + (g_animTarget - g_animFrom) * t;
    if (t >= 1.0f) { g_animProgress = g_animTarget; g_animDurMs = 0.0f; return false; }
    return true;
}

void StartPanelAnim(DrawerPanel target)
{
    const float fromHeight = Drawer_GetCurrentPanelHeight();
    const float toHeight = DrawerPanel_GetHeight(target);

    g_targetPanel = target;
    g_panelAnimFrom = g_panelAnimProgress;
    g_panelAnimTarget = 1.0f;
    g_panelAnimStart = GetTickCount64();
    g_panelAnimDurMs = HeightMorphDurationMs(fromHeight, toHeight);
    SetTimer(g_drawerHwnd, PANEL_TIMER, 16, nullptr);
}

bool TickPanelAnim()
{
    if (g_panelAnimDurMs <= 0.0f) return false;
    const float t = Clamp01(static_cast<float>(GetTickCount64() - g_panelAnimStart) / g_panelAnimDurMs);
    g_panelAnimProgress = g_panelAnimFrom + (g_panelAnimTarget - g_panelAnimFrom) * EaseOutCubic(t);
    if (t >= 1.0f) { 
        g_panelAnimProgress = g_panelAnimTarget; 
        g_panelAnimDurMs = 0.0f; 
        g_activePanel = g_targetPanel;
        return false; 
    }
    return true;
}

// ── Hover animation ────────────────────────────────────────────────────────────

static bool TickHoverAnimation()
{
    const float step = (16.0f / HOV_DUR_MS);
    bool anyChanging = false;

    auto tickValue = [&](float& value, float target) {
        float next = Clamp01(value + (target - value > 0 ? step : -step));
        if (fabsf(next - target) < step) next = target;
        if (next != value) { value = next; anyChanging = true; }
    };

    tickValue(g_avHovT, g_avHov ? 1.0f : 0.0f);
    for (int i = 0; i < BTN_COUNT; ++i) tickValue(g_btnHovT[i], (g_btnHov == i) ? 1.0f : 0.0f);
    for (int i = 0; i < PILL_COUNT; ++i) tickValue(g_pillHovT[i], (g_pillHov == i) ? 1.0f : 0.0f);
    tickValue(g_volIconHovT, g_volIconHov ? 1.0f : 0.0f);
    tickValue(g_volSetHovT, g_volSetHov ? 1.0f : 0.0f);
    tickValue(g_backHovT, g_backHov ? 1.0f : 0.0f);
    for (int i = 0; i < PWR_ITEM_COUNT; ++i) tickValue(g_pwrItemHovT[i], (g_pwrItemHov == i) ? 1.0f : 0.0f);
    for (size_t i = 0; i < g_sndDeviceHovT.size(); ++i) tickValue(g_sndDeviceHovT[i], (g_sndDeviceHovIdx == static_cast<int>(i)) ? 1.0f : 0.0f);
    for (size_t i = 0; i < g_sndAppIconHovT.size(); ++i) tickValue(g_sndAppIconHovT[i], (g_sndAppIconHovIdx == static_cast<int>(i)) ? 1.0f : 0.0f);
    for (size_t i = 0; i < g_sndAppSliderHovT.size(); ++i) tickValue(g_sndAppSliderHovT[i], (g_sndAppSliderHovIdx == static_cast<int>(i)) ? 1.0f : 0.0f);

    for (size_t i = 0; i < g_wifiNetHovT.size(); ++i) tickValue(g_wifiNetHovT[i], (g_wifiNetHovIdx == static_cast<int>(i)) ? 1.0f : 0.0f);
    tickValue(g_wifiToggleHovT, g_wifiToggleHov ? 1.0f : 0.0f);
    tickValue(g_wifiRefreshHovT, g_wifiRefreshHov ? 1.0f : 0.0f);

    for (size_t i = 0; i < g_btDevHovT.size(); ++i) tickValue(g_btDevHovT[i], (g_btDevHovIdx == static_cast<int>(i)) ? 1.0f : 0.0f);
    tickValue(g_btToggleHovT, g_btToggleHov ? 1.0f : 0.0f);
    tickValue(g_btRefreshHovT, g_btRefreshHov ? 1.0f : 0.0f);
    for (size_t i = 0; i < g_notifRowHovT.size(); ++i) tickValue(g_notifRowHovT[i], (g_notifRowHovIdx == static_cast<int>(i)) ? 1.0f : 0.0f);
    for (size_t i = 0; i < g_notifCloseHovT.size(); ++i) tickValue(g_notifCloseHovT[i], (g_notifCloseHovIdx == static_cast<int>(i)) ? 1.0f : 0.0f);
    tickValue(g_notifClearHovT, g_notifClearHov ? 1.0f : 0.0f);

    if (g_wifiRefreshSpinT > 0.0f)
    {
        g_wifiRefreshSpinT -= (16.0f / 1000.0f) * 1.5f;
        if (g_wifiRefreshSpinT < 0.0f) g_wifiRefreshSpinT = 0.0f;
        anyChanging = true;
    }

    if (g_btRefreshSpinT > 0.0f)
    {
        g_btRefreshSpinT -= (16.0f / 1000.0f) * 1.5f;
        if (g_btRefreshSpinT < 0.0f) g_btRefreshSpinT = 0.0f;
        anyChanging = true;
    }

    bool isConnecting = false;
    for (const auto& w : g_animWifiNetworks) {
        if (w.data.isConnecting) { isConnecting = true; break; }
    }
    if (isConnecting || g_wifiTogglePending) {
        g_wifiConnectingAngle += (16.0f / 1000.0f) * 360.0f;
        if (g_wifiConnectingAngle >= 360.0f) g_wifiConnectingAngle -= 360.0f;
        anyChanging = true;
    }

    bool isBtConnecting = false;
    for (const auto& b : g_animBtDevices) {
        if (b.data.isConnecting || b.data.isDisconnecting) { isBtConnecting = true; break; }
    }
    if (isBtConnecting || g_btTogglePending) {
        g_btConnectingAngle += (16.0f / 1000.0f) * 360.0f;
        if (g_btConnectingAngle >= 360.0f) g_btConnectingAngle -= 360.0f;
        anyChanging = true;
    }

    return anyChanging;
}

static bool TickSoundListAnimation()
{
    const float step = (16.0f / HEIGHT_MORPH_LIST_MS);
    bool anyChanging = false;

    auto tickList = [&](auto& list) {
        for (auto it = list.begin(); it != list.end(); ) {
            float target = it->isRemoving ? 0.0f : 1.0f;
            if (it->alpha != target) {
                it->alpha = Clamp01(it->alpha + (target > it->alpha ? step : -step));
                it->scale = 0.9f + 0.1f * it->alpha;
                anyChanging = true;
            }
            if (it->isRemoving && it->alpha == 0.0f) {
                it = list.erase(it);
            } else {
                ++it;
            }
        }
    };

    tickList(g_animEndpoints);
    tickList(g_animSessions);
    tickList(g_animWifiNetworks);
    tickList(g_animBtDevices);

    for (auto it = g_animNotifications.begin(); it != g_animNotifications.end(); )
    {
        const float target = it->isRemoving ? 0.0f : 1.0f;
        if (it->alpha != target)
        {
            it->alpha = Clamp01(it->alpha + (target > it->alpha ? step : -step));
            it->scale = 0.96f + 0.04f * EaseOutCubic(it->alpha);
            anyChanging = true;
        }
        if (it->isRemoving && it->alpha == 0.0f)
        {
            it = g_animNotifications.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return anyChanging;
}

static bool TickScrollAnimation()
{
    constexpr float stiffness = 0.18f;
    constexpr float damping   = 0.78f;
    constexpr float threshold = 0.15f;
    constexpr float notifEase = 0.32f;

    auto tickSpring = [&](float& pos, float& vel, float target) -> bool {
        vel = vel * damping + (target - pos) * stiffness;
        pos += vel;
        if (fabsf(vel) < threshold && fabsf(target - pos) < threshold) {
            pos = target;
            vel = 0.0f;
            return false;
        }
        return true;
    };

    auto tickEase = [&](float& pos, float target) -> bool {
        const float delta = target - pos;
        if (fabsf(delta) < threshold) {
            pos = target;
            return false;
        }
        pos += delta * notifEase;
        return true;
    };

    bool anyChanging = false;
    anyChanging |= tickSpring(g_sndDeviceScrollY, g_sndDeviceScrollVelY, g_sndDeviceScrollTargetY);
    anyChanging |= tickSpring(g_sndAppScrollY,    g_sndAppScrollVelY,    g_sndAppScrollTargetY);
    anyChanging |= tickSpring(g_wifiScrollY,      g_wifiScrollVelY,      g_wifiScrollTargetY);
    anyChanging |= tickSpring(g_btScrollY,        g_btScrollVelY,        g_btScrollTargetY);
    g_notifScrollVelY = 0.0f;
    anyChanging |= tickEase(g_notifScrollY, g_notifScrollTargetY);
    return anyChanging;
}

bool DrawerAnimation_HandleTimer(HWND hWnd, WPARAM timerId)
{
    if (timerId == ANIM_TIMER)
    {
        const bool still = TickAnim();
        Render();
        if (!still)
        {
            KillTimer(hWnd, ANIM_TIMER);
            if (!g_animOpen)
                ShowWindow(hWnd, SW_HIDE);
        }
        return true;
    }

    if (timerId == PANEL_TIMER)
    {
        const bool still = TickPanelAnim();
        Render();
        if (!still) KillTimer(hWnd, PANEL_TIMER);
        return true;
    }

    if (timerId == HOVER_TIMER)
    {
        const bool anyChanging = TickHoverAnimation();
        Render();
        if (!anyChanging) KillTimer(hWnd, HOVER_TIMER);
        return true;
    }

    if (timerId == SCREENSHOT_TIMER)
    {
        KillTimer(hWnd, SCREENSHOT_TIMER);
        INPUT inp[2] = {};
        inp[0].type       = INPUT_KEYBOARD;
        inp[0].ki.wVk     = VK_SNAPSHOT;
        inp[1]            = inp[0];
        inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inp, sizeof(INPUT));
        return true;
    }

    if (timerId == SND_LIST_TIMER)
    {
        const bool anyChanging = TickSoundListAnimation();
        Render();
        if (!anyChanging) KillTimer(hWnd, SND_LIST_TIMER);
        return true;
    }

    if (timerId == SCROLL_TIMER)
    {
        const bool anyChanging = TickScrollAnimation();
        Render();
        if (!anyChanging) KillTimer(hWnd, SCROLL_TIMER);
        return true;
    }

    return false;
}
