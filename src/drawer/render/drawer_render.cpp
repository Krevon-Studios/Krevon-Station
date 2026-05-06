// drawer_render.cpp - Top-level drawer render orchestration and panel dispatch.
#include "../panels/drawer_panel.h"
#include "drawer_render.h"
#include <algorithm>

float DrawerPanel_GetHeight(DrawerPanel panel)
{
    if (panel == DrawerPanel::Sound)
    {
        SoundPanel_UpdateMetrics();
        return g_sndTargetH;
    }
    if (panel == DrawerPanel::Wifi)
    {
        WifiPanel_UpdateMetrics();
        return g_wifiTargetH;
    }
    if (panel == DrawerPanel::Bluetooth)
    {
        BluetoothPanel_UpdateMetrics();
        return g_btTargetH;
    }
    if (panel == DrawerPanel::Power)
        return D_H_POWER;
    return D_H_MAIN;
}

float Drawer_GetCurrentPanelHeight()
{
    const float activeH = DrawerPanel_GetHeight(g_activePanel);
    const float targetH = DrawerPanel_GetHeight(g_targetPanel);
    return activeH + (targetH - activeH) * EaseOutCubic(g_panelAnimProgress);
}

void DrawerPanel_Render(DrawerPanel panel, float opacity)
{
    switch (panel)
    {
    case DrawerPanel::Main:  MainPanel_Render(opacity); break;
    case DrawerPanel::Power: PowerPanel_Render(opacity); break;
    case DrawerPanel::Sound: SoundPanel_Render(opacity); break;
    case DrawerPanel::Wifi:  WifiPanel_Render(opacity); break;
    case DrawerPanel::Bluetooth: BluetoothPanel_Render(opacity); break;
    }
}

void Render()
{
    if (!g_ctx || !g_swap) return;

    const float alpha = EaseOutCubic(g_animProgress);
    const D2D1_SIZE_F sz = g_ctx->GetSize();

    g_ctx->BeginDraw();
    g_ctx->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (alpha < 0.01f)
    {
        g_ctx->EndDraw();
        g_swap->Present(0, 0);
        return;
    }

    D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters();
    layerParams.opacity = alpha;
    g_ctx->PushLayer(layerParams, nullptr);

    SoundPanel_UpdateMetrics();
    WifiPanel_UpdateMetrics();
    BluetoothPanel_UpdateMetrics();

    const float currentHeight = Drawer_GetCurrentPanelHeight();
    NotificationPanel_UpdateMetrics(currentHeight);
    const float totalVisibleHeight = currentHeight + ((g_notifPanelH > 0.01f) ? (NOTIF_PANEL_GAP + g_notifPanelH) : 0.0f);

    if (g_drawerHwnd)
    {
        RECT rc;
        GetClientRect(g_drawerHwnd, &rc);
        int w = rc.right - rc.left;
        int h = static_cast<int>(totalVisibleHeight * g_dpiScale + 0.5f);

        static int lastH = -1;
        if (h != lastH)
        {
            HRGN rgn = CreateRectRgn(0, 0, w, h);
            SetWindowRgn(g_drawerHwnd, rgn, FALSE);
            lastH = h;
        }
    }

    const float bgScale = 0.96f + 0.04f * alpha;
    g_ctx->SetTransform(D2D1::Matrix3x2F::Scale(bgScale, bgScale, D2D1::Point2F(sz.width * 0.5f, currentHeight * 0.5f)));

    g_brBg->SetOpacity(1.0f);
    g_ctx->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, sz.width, currentHeight), 16.0f, 16.0f),
        g_brBg.Get());

    const float slideY = (1.0f - alpha) * 12.0f;
    g_ctx->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -slideY));

    float mainAlpha = 0.0f;
    float powerAlpha = 0.0f;
    float soundAlpha = 0.0f;
    float wifiAlpha = 0.0f;
    float btAlpha = 0.0f;

    auto getAlpha = [&](DrawerPanel p) -> float& {
        if (p == DrawerPanel::Main) return mainAlpha;
        if (p == DrawerPanel::Power) return powerAlpha;
        if (p == DrawerPanel::Wifi) return wifiAlpha;
        if (p == DrawerPanel::Bluetooth) return btAlpha;
        return soundAlpha;
    };

    if (g_activePanel == g_targetPanel) {
        getAlpha(g_activePanel) = 1.0f;
    } else {
        getAlpha(g_activePanel) = 1.0f - g_panelAnimProgress;
        getAlpha(g_targetPanel) = g_panelAnimProgress;
    }

    if (g_activePanel == DrawerPanel::Sound || g_targetPanel == DrawerPanel::Sound)
    {
        if (g_sndDeviceRects.size() != g_animEndpoints.size()) {
            g_sndDeviceRects.resize(g_animEndpoints.size());
            g_sndDeviceHovT.resize(g_animEndpoints.size(), 0.0f);
        }
        if (g_sndAppIconRects.size() != g_animSessions.size()) {
            g_sndAppIconRects.resize(g_animSessions.size());
            g_sndAppIconHovT.resize(g_animSessions.size(), 0.0f);
            g_sndAppSliderRects.resize(g_animSessions.size());
            g_sndAppSliderThumbRects.resize(g_animSessions.size());
            g_sndAppSliderHovT.resize(g_animSessions.size(), 0.0f);
        }
    }

    if (g_activePanel == DrawerPanel::Wifi || g_targetPanel == DrawerPanel::Wifi)
    {
        if (g_wifiNetRects.size() != g_animWifiNetworks.size()) {
            g_wifiNetRects.resize(g_animWifiNetworks.size());
            g_wifiNetHovT.resize(g_animWifiNetworks.size(), 0.0f);
        }
    }

    if (g_activePanel == DrawerPanel::Bluetooth || g_targetPanel == DrawerPanel::Bluetooth)
    {
        if (g_btDevRects.size() != g_animBtDevices.size()) {
            g_btDevRects.resize(g_animBtDevices.size());
            g_btDevHovT.resize(g_animBtDevices.size(), 0.0f);
        }
    }

    if (g_notifRowRects.size() != g_animNotifications.size()) {
        g_notifRowRects.resize(g_animNotifications.size());
        g_notifCloseRects.resize(g_animNotifications.size());
        g_notifRowHovT.resize(g_animNotifications.size(), 0.0f);
        g_notifCloseHovT.resize(g_animNotifications.size(), 0.0f);
    }

    DrawerPanel_Render(DrawerPanel::Main, mainAlpha);
    DrawerPanel_Render(DrawerPanel::Power, powerAlpha);
    DrawerPanel_Render(DrawerPanel::Sound, soundAlpha);
    DrawerPanel_Render(DrawerPanel::Wifi, wifiAlpha);
    DrawerPanel_Render(DrawerPanel::Bluetooth, btAlpha);
    NotificationPanel_Render(currentHeight, 1.0f);

    g_ctx->PopLayer();
    g_ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    g_ctx->EndDraw();
    g_swap->Present(0, 0);
}
