// drawer_panel_main.cpp - Main quick-settings panel rendering.
#include "drawer_panel.h"
#include "../render/drawer_render.h"
#include <algorithm>

void MainPanel_Render(float opacity)
{
    const D2D1_SIZE_F sz = g_ctx->GetSize();
    if (opacity > 0.01f)
    {
        D2D1_LAYER_PARAMETERS mainLayerParams = D2D1::LayerParameters();
        mainLayerParams.opacity = opacity;
        g_ctx->PushLayer(mainLayerParams, nullptr);


    // ── ROW 1: [avatar]           [cam] [set] [lock] [pwr] ───────────────────
    const float row1Top = D_PAD;
    const float row1Mid = row1Top + ROW1_H * 0.5f;

    // Avatar on the far left
    const float avCX = D_PAD + AVATAR_R;
    g_avEllipse = D2D1::Ellipse(D2D1::Point2F(avCX, row1Mid), AVATAR_R, AVATAR_R);

    g_brBtn->SetOpacity(1.0f);
    g_ctx->FillEllipse(g_avEllipse, g_brBtn.Get());

    if (g_avatarBmp)
    {
        ComPtr<ID2D1EllipseGeometry> geo;
        g_fac->CreateEllipseGeometry(g_avEllipse, &geo);
        g_ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get()), nullptr);
        g_ctx->DrawBitmap(g_avatarBmp.Get(),
            D2D1::RectF(avCX - AVATAR_R, row1Mid - AVATAR_R, avCX + AVATAR_R, row1Mid + AVATAR_R));
        g_ctx->PopLayer();
    }
    else
    {
        g_brWhite->SetOpacity(0.85f);
        DrawSvg(g_svgUserRound.Get(), avCX, row1Mid, BTN_ICON_SZ);
        g_brWhite->SetOpacity(1.0f);
    }

    // Avatar hover ring (light grey, smooth fade) — alpha is already inherited from layer
    if (g_avHovT > 0.0f)
    {
        ComPtr<ID2D1SolidColorBrush> brRing;
        g_ctx->CreateSolidColorBrush(
            D2D1::ColorF(0.75f, 0.75f, 0.75f, 0.55f * g_avHovT), &brRing);
        g_ctx->DrawEllipse(g_avEllipse, brRing.Get(), 1.5f);
    }

    // 4 Action buttons grouped on the right
    const ID2D1SvgDocument* btnIcons[BTN_COUNT] = {
        g_svgCamera.Get(), g_svgSettings.Get(), g_svgLock.Get(), g_svgPower.Get()
    };

    const float btnGap = 8.0f;
    const float btnStartX = sz.width - D_PAD - BTN_R - (BTN_COUNT - 1) * (BTN_R * 2.0f + btnGap);

    for (int i = 0; i < BTN_COUNT; ++i)
    {
        const float cx = btnStartX + i * (BTN_R * 2.0f + btnGap);
        g_btnEllipse[i] = D2D1::Ellipse(D2D1::Point2F(cx, row1Mid), BTN_R, BTN_R);

        // Smooth interpolation between rest and hovered circle colour
        D2D1_COLOR_F btnCol = LerpColor(CLR_BTN, CLR_BTN_HOV, g_btnHovT[i]);
        ComPtr<ID2D1SolidColorBrush> brBtnLerp;
        g_ctx->CreateSolidColorBrush(btnCol, &brBtnLerp);
        brBtnLerp->SetOpacity(1.0f);
        g_ctx->FillEllipse(g_btnEllipse[i], brBtnLerp.Get());

        g_brWhite->SetOpacity(1.0f);
        DrawSvg(const_cast<ID2D1SvgDocument*>(btnIcons[i]), cx, row1Mid, BTN_ICON_SZ);
    }

    // ── Separator ─────────────────────────────────────────────────────────────
    const float sepY = row1Top + ROW1_H + GAP_SEP_TOP;
    g_brSep->SetOpacity(1.0f);
    g_ctx->DrawLine(D2D1::Point2F(0.0f, sepY), D2D1::Point2F(sz.width, sepY), g_brSep.Get(), 1.0f);

    // ── ROW 2: WiFi pill | Bluetooth pill ─────────────────────────────────────
    const float row2Top = sepY + 1.0f + GAP_SEP_BOT;
    const float pillW   = (sz.width - D_PAD * 2.0f - 8.0f) * 0.5f;


    for (int p = 0; p < PILL_COUNT; ++p)
    {
        const float px = D_PAD + p * (pillW + 8.0f);
        const float py = row2Top;
        g_pillRect[p] = D2D1::RectF(px, py, px + pillW, py + PILL_H);

        // State check
        bool isWifiPending = (p == PILL_WIFI && g_wifiTogglePending);
        bool wifiRadioOn = isWifiPending ? g_wifiToggleTargetOn : g_snap.wifiRadioOn;
        bool isBtPending = (p == PILL_BT && g_btTogglePending);
        bool btRadioOn = isBtPending ? g_btToggleTargetOn : (g_snap.bluetooth != BluetoothIconState::Off);
        bool isOff = (p == PILL_WIFI && !wifiRadioOn) || (p == PILL_BT && !btRadioOn);

        // Entire Pill Background — smooth hover interpolation
        ComPtr<ID2D1SolidColorBrush> dynamicPillBg;
        if (isOff)
        {
            D2D1_COLOR_F offRest = D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f);
            D2D1_COLOR_F offHov  = D2D1::ColorF(0.16f, 0.16f, 0.16f, 1.0f);
            g_ctx->CreateSolidColorBrush(LerpColor(offRest, offHov, g_pillHovT[p]), &dynamicPillBg);
        }
        else
        {
            g_ctx->CreateSolidColorBrush(LerpColor(g_clrPill, g_clrPillHov, g_pillHovT[p]), &dynamicPillBg);
        }
        dynamicPillBg->SetOpacity(1.0f);
        g_ctx->FillRoundedRectangle(
            D2D1::RoundedRect(g_pillRect[p], PILL_R, PILL_R), dynamicPillBg.Get());

        // Icon circle (smaller, centered)
        const float iCX = px + 12.0f + 14.0f; // padding 12, radius 14 => diameter 28
        const float iCY = py + PILL_H * 0.5f;

        if (isOff)
        {
            // OFF State: Grey circle, Grey icon
            ComPtr<ID2D1SolidColorBrush> offCircleBg;
            g_ctx->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.18f, 1.0f), &offCircleBg);
            offCircleBg->SetOpacity(1.0f);
            g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iCX, iCY), 14.0f, 14.0f), offCircleBg.Get());
            if (isWifiPending || isBtPending)
            {
                D2D1_MATRIX_3X2_F iconTr;
                g_ctx->GetTransform(&iconTr);
                float angle = isWifiPending ? g_wifiConnectingAngle : g_btConnectingAngle;
                g_ctx->SetTransform(D2D1::Matrix3x2F::Rotation(angle, D2D1::Point2F(iCX, iCY)) * iconTr);
                DrawSvgColored(g_svgRefresh.Get(), iCX, iCY, 15.0f, D2D1::ColorF(0.72f, 0.72f, 0.72f, 1.0f));
                g_ctx->SetTransform(iconTr);
            }
            else
            {
                DrawSvgColored(p == PILL_WIFI ? WifiSvg(g_snap.wifi) : BtSvg(g_snap.bluetooth),
                               iCX, iCY, 16.0f, D2D1::ColorF(0.6f, 0.6f, 0.6f, 1.0f));
            }
        }
        else
        {
            // ON State: Dark tinted circle, light pastel icon
            g_brPillIco->SetOpacity(1.0f);
            g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iCX, iCY), 14.0f, 14.0f), g_brPillIco.Get());
            if (isWifiPending || isBtPending)
            {
                D2D1_MATRIX_3X2_F iconTr;
                g_ctx->GetTransform(&iconTr);
                float angle = isWifiPending ? g_wifiConnectingAngle : g_btConnectingAngle;
                g_ctx->SetTransform(D2D1::Matrix3x2F::Rotation(angle, D2D1::Point2F(iCX, iCY)) * iconTr);
                DrawSvgColored(g_svgRefresh.Get(), iCX, iCY, 15.0f, g_clrFill);
                g_ctx->SetTransform(iconTr);
            }
            else
            {
                DrawSvgColored(p == PILL_WIFI ? WifiSvg(g_snap.wifi) : BtSvg(g_snap.bluetooth),
                               iCX, iCY, 16.0f, g_clrFill);
            }
        }

        // Text
        const float tX = iCX + 14.0f + 12.0f;
        const float tW = pillW - (tX - px) - 36.0f; // extra space for chevron and line
        const wchar_t* title = (p == PILL_WIFI) ? L"Wi-Fi" : L"Bluetooth";

        const float textMidY = py + PILL_H * 0.5f + 1.0f; // slight downward nudge for optical centering
        g_brWhite->SetOpacity(1.0f);
        g_ctx->DrawText(title, static_cast<UINT32>(wcslen(title)), g_tfName.Get(),
            D2D1::RectF(tX, textMidY - 16.0f, tX + tW, textMidY + 1.0f), g_brWhite.Get());

        std::wstring sub;
        if (isWifiPending || isBtPending)
        {
            bool targetOn = isWifiPending ? g_wifiToggleTargetOn : g_btToggleTargetOn;
            sub = targetOn ? L"Turning on..." : L"Turning off...";
        }
        else if (isOff)
        {
            sub = L"Off";
        }
        else
        {
            sub = (p == PILL_WIFI)
                ? (g_snap.wifiSsid.empty()            ? L"Not connected" : g_snap.wifiSsid)
                : (g_snap.bluetoothDeviceName.empty()  ? L"Not connected" : g_snap.bluetoothDeviceName);
        }

        g_brGrey->SetOpacity(1.0f);
        g_ctx->DrawText(sub.c_str(), static_cast<UINT32>(sub.size()), g_tfSub.Get(),
            D2D1::RectF(tX, textMidY - 1.0f, tX + tW, textMidY + 16.0f), g_brGrey.Get());

        // Vertical separator before the chevron
        const float vSepX = px + pillW - 30.0f;
        g_brSep->SetOpacity(1.0f);
        g_ctx->DrawLine(D2D1::Point2F(vSepX, py + 12.0f), D2D1::Point2F(vSepX, py + PILL_H - 12.0f), g_brSep.Get(), 1.5f);

        // Chevron
        g_brWhite->SetOpacity(0.5f);
        DrawSvg(g_svgChevron.Get(), px + pillW - 14.0f, iCY, 14.0f);
    }

    // ── ROW 3: [vol-icon] [slider] pct% [settings-2] ─────────────────────────
    const float row3Top = row2Top + ROW2_H + GAP_R2_R3;
    const float row3Mid = row3Top + ROW3_H * 0.5f;
    const float viCX    = D_PAD + VOL_ICON_SZ * 0.5f;

    g_volIconRect = D2D1::RectF(D_PAD, row3Mid - VOL_ICON_SZ,
                                 D_PAD + VOL_ICON_SZ * 1.5f, row3Mid + VOL_ICON_SZ);

    // Vol-icon hover ring — alpha inherited from layer
    if (g_volIconHovT > 0.0f)
    {
        const float ringR = VOL_ICON_SZ * 0.9f;
        ComPtr<ID2D1SolidColorBrush> brVolRing;
        g_ctx->CreateSolidColorBrush(
            D2D1::ColorF(0.75f, 0.75f, 0.75f, 0.22f * g_volIconHovT), &brVolRing);
        g_ctx->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(viCX + 2.0f, row3Mid), ringR, ringR), brVolRing.Get());
    }

    g_brWhite->SetOpacity(g_volIconHov ? 1.0f : 0.85f);
    DrawSvg(VolSvg(g_snap.volume), viCX + 2.0f, row3Mid, VOL_ICON_SZ);

    // Pct text + settings-2 icon
    float displayVol = g_snap.volumeMuted ? 0.0f : g_snap.volumeLevel;

    wchar_t pctBuf[8];
    swprintf_s(pctBuf, L"%d%%", static_cast<int>(displayVol * 100.0f + 0.5f));
    const float eq2X = sz.width - D_PAD - VOL_ICON_SZ;
    const float pctW = 36.0f;
    const float pctX = eq2X - pctW - 6.0f;
    g_brGrey->SetOpacity(1.0f);
    g_ctx->DrawText(pctBuf, static_cast<UINT32>(wcslen(pctBuf)), g_tfPct.Get(),
        D2D1::RectF(pctX, row3Mid - 9.0f, pctX + pctW, row3Mid + 9.0f), g_brGrey.Get());
    const float setCX = eq2X + VOL_ICON_SZ * 0.5f;
    g_volSetRect = D2D1::RectF(setCX - VOL_ICON_SZ, row3Mid - VOL_ICON_SZ,
                               setCX + VOL_ICON_SZ, row3Mid + VOL_ICON_SZ);

    if (g_volSetHovT > 0.0f)
    {
        const float ringR = VOL_ICON_SZ * 0.9f;
        ComPtr<ID2D1SolidColorBrush> brSetRing;
        g_ctx->CreateSolidColorBrush(
            D2D1::ColorF(0.75f, 0.75f, 0.75f, 0.22f * g_volSetHovT), &brSetRing);
        g_ctx->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(setCX, row3Mid), ringR, ringR), brSetRing.Get());
    }

    g_brWhite->SetOpacity(g_volSetHov ? 1.0f : 0.5f);
    DrawSvg(g_svgSettings2.Get(), setCX, row3Mid, VOL_ICON_SZ);

    // Slider
    const float slL = D_PAD + VOL_ICON_SZ * 1.5f + 8.0f;
    const float slR = pctX - 6.0f;
    g_sliderTrackRect = D2D1::RectF(slL, row3Mid - SLIDER_H * 0.5f,
                                     slR, row3Mid + SLIDER_H * 0.5f);
    g_sliderLeft  = slL * g_dpiScale;
    g_sliderRight = slR * g_dpiScale;

    g_brTrack->SetOpacity(1.0f);
    g_ctx->FillRoundedRectangle(
        D2D1::RoundedRect(g_sliderTrackRect, SLIDER_H * 0.5f, SLIDER_H * 0.5f), g_brTrack.Get());

    const float fillR = slL + (slR - slL) * displayVol;
    if (fillR > slL)
    {
        g_brFill->SetOpacity(1.0f);
        g_ctx->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(slL, row3Mid - SLIDER_H * 0.5f, fillR, row3Mid + SLIDER_H * 0.5f),
                SLIDER_H * 0.5f, SLIDER_H * 0.5f),
            g_brFill.Get());
    }

    float thumbX = (std::clamp)(slL + (slR - slL) * displayVol,
                               slL + THUMB_R, slR - THUMB_R);
    g_brThumb->SetOpacity(1.0f);
    g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, row3Mid), THUMB_R, THUMB_R),
                        g_brThumb.Get());

        g_ctx->PopLayer(); // End Main Panel Layer
    }
}
