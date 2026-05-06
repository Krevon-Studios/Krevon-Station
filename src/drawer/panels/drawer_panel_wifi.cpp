// drawer_panel_wifi.cpp - Wi-Fi network list panel rendering and metrics.
#include "drawer_panel.h"
#include "../render/drawer_render.h"
#include <algorithm>

static WifiIconState WifiStateFromSignal(int signalQuality)
{
    if (signalQuality >= 75) return WifiIconState::Full;
    if (signalQuality >= 50) return WifiIconState::High;
    if (signalQuality >= 25) return WifiIconState::Low;
    if (signalQuality > 0)   return WifiIconState::Zero;
    return WifiIconState::Disconnected;
}

void WifiPanel_UpdateMetrics()
{
    constexpr float WIFI_ITEM_H = 46.0f;
    constexpr float WIFI_GAP = 6.0f;
    constexpr float REFRESH_BTN_H = 32.0f;

    float listTotalH = 0.0f;
    for (const auto& a : g_animWifiNetworks) {
        const float extent = HeightMorphExtent(a.alpha, a.isRemoving);
        listTotalH += WIFI_ITEM_H * extent + (extent > 0.01f ? WIFI_GAP * extent : 0.0f);
    }
    if (listTotalH > 0.0f) listTotalH -= WIFI_GAP;

    const float listMaxViewH = WIFI_ITEM_H * 5.0f + WIFI_GAP * 4.0f;
    float listViewH = (std::min)(listTotalH, listMaxViewH);

    bool isWifiOn = g_wifiTogglePending ? g_wifiToggleTargetOn : g_snap.wifiRadioOn;

    float contentH = D_PAD + ROW1_H + GAP_SEP_TOP + 1.0f + GAP_SEP_BOT; // Header + sep
    
    if (isWifiOn)
    {
        contentH += (listTotalH > 0.0f) ? listViewH : 64.0f;
        contentH += 8.0f + 1.0f + 8.0f; // separator padding
        contentH += 26.0f; // REFRESH_BTN_H
        contentH += 8.0f;  // bottom padding
    }
    else
    {
        contentH += 80.0f; // Extra space for "Wi-Fi is turned off" centered message
    }

    g_wifiTargetH = (std::min)(contentH, 600.0f); // Max height limit
}

void WifiPanel_Render(float opacity)
{
    const D2D1_SIZE_F sz = g_ctx->GetSize();
    constexpr float WIFI_ITEM_H = 46.0f;
    constexpr float WIFI_GAP = 6.0f;
    constexpr float REFRESH_BTN_H = 32.0f;

    if (opacity > 0.01f)
    {
        D2D1_LAYER_PARAMETERS wifiLayerParams = D2D1::LayerParameters();
        wifiLayerParams.opacity = opacity;
        g_ctx->PushLayer(wifiLayerParams, nullptr);

        float listTotalH = 0.0f;
        for (const auto& a : g_animWifiNetworks) {
            const float extent = HeightMorphExtent(a.alpha, a.isRemoving);
            listTotalH += WIFI_ITEM_H * extent + (extent > 0.01f ? WIFI_GAP * extent : 0.0f);
        }
        if (listTotalH > 0.0f) listTotalH -= WIFI_GAP;

        const float listMaxViewH = WIFI_ITEM_H * 5.0f + WIFI_GAP * 4.0f;
        float listViewH = (std::min)(listTotalH, listMaxViewH);

        float maxScroll = (std::max)(0.0f, listTotalH - listViewH);
        g_wifiScrollTargetY = (std::clamp)(g_wifiScrollTargetY, 0.0f, maxScroll);
        g_wifiScrollY = (std::clamp)(g_wifiScrollY, 0.0f, maxScroll);

        const float headerMidY = D_PAD + ROW1_H * 0.5f;

        // Back button
        const float backBtnR = 16.0f;
        const float backCX = D_PAD + backBtnR;
        g_backEllipse = D2D1::Ellipse(D2D1::Point2F(backCX, headerMidY), backBtnR, backBtnR);

        D2D1_COLOR_F backCol = LerpColor(CLR_BTN, CLR_BTN_HOV, g_backHovT);
        ComPtr<ID2D1SolidColorBrush> brBackLerp;
        g_ctx->CreateSolidColorBrush(backCol, &brBackLerp);
        brBackLerp->SetOpacity(1.0f);
        g_ctx->FillEllipse(g_backEllipse, brBackLerp.Get());
        DrawSvgColored(g_svgChevronLeft.Get(), backCX - 1.0f, headerMidY, 18.0f, CLR_WHITE);

        // "Wi-Fi" title
        const wchar_t* wifiTitle = L"Wi-Fi";
        const float titleX = backCX + backBtnR + 12.0f;
        g_ctx->DrawText(wifiTitle, static_cast<UINT32>(wcslen(wifiTitle)), g_tfName.Get(),
            D2D1::RectF(titleX, headerMidY - 10.0f, sz.width, headerMidY + 10.0f), g_brWhite.Get());

        // Toggle Switch on the right
        const float toggleW = 44.0f;
        const float toggleH = 24.0f;
        const float toggleX = sz.width - D_PAD - toggleW;
        const float toggleY = headerMidY - toggleH * 0.5f;
        g_wifiToggleRect = D2D1::RectF(toggleX, toggleY, toggleX + toggleW, toggleY + toggleH);

        bool isWifiOn = g_snap.wifiRadioOn;
        if (g_wifiTogglePending)
            isWifiOn = g_wifiToggleTargetOn;

        ComPtr<ID2D1SolidColorBrush> toggleBg;
        D2D1_COLOR_F offCol = D2D1::ColorF(0.2f, 0.2f, 0.2f, 1.0f);
        D2D1_COLOR_F onCol = g_clrFill; // the pastel accent
        D2D1_COLOR_F curBg = LerpColor(isWifiOn ? offCol : onCol, isWifiOn ? onCol : offCol, 1.0f); // Fast toggle just use state
        g_ctx->CreateSolidColorBrush(curBg, &toggleBg);
        g_ctx->FillRoundedRectangle(D2D1::RoundedRect(g_wifiToggleRect, toggleH * 0.5f, toggleH * 0.5f), toggleBg.Get());

        // Thumb
        const float thumbR = 9.0f;
        float thumbXOffset = isWifiOn ? (toggleW - thumbR - 3.0f) : (thumbR + 3.0f);
        g_wifiToggleThumbRect = D2D1::RectF(toggleX + thumbXOffset - thumbR, toggleY + toggleH * 0.5f - thumbR, toggleX + thumbXOffset + thumbR, toggleY + toggleH * 0.5f + thumbR);
        
        ComPtr<ID2D1SolidColorBrush> thumbBr;
        g_ctx->CreateSolidColorBrush(isWifiOn ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.6f, 0.6f, 0.6f, 1.0f), &thumbBr);
        g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(toggleX + thumbXOffset, toggleY + toggleH * 0.5f), thumbR, thumbR), thumbBr.Get());

        if (g_wifiTogglePending)
        {
            D2D1_MATRIX_3X2_F iconTr;
            const float spinCX = toggleX + toggleW * 0.5f;
            const float spinCY = toggleY + toggleH * 0.5f;
            g_ctx->GetTransform(&iconTr);
            g_ctx->SetTransform(D2D1::Matrix3x2F::Rotation(g_wifiConnectingAngle, D2D1::Point2F(spinCX, spinCY)) * iconTr);
            DrawSvgColored(g_svgRefresh.Get(), spinCX, spinCY, 13.0f, isWifiOn ? g_clrPillIco : D2D1::ColorF(0.72f, 0.72f, 0.72f, 1.0f));
            g_ctx->SetTransform(iconTr);
        }

        // Separator
        const float sepY = D_PAD + ROW1_H + GAP_SEP_TOP;
        g_brSep->SetOpacity(1.0f);
        g_ctx->DrawLine(D2D1::Point2F(0.0f, sepY), D2D1::Point2F(sz.width, sepY), g_brSep.Get(), 1.0f);

        float curY = sepY + 1.0f + GAP_SEP_BOT;

        if (!isWifiOn)
        {
            const wchar_t* offMsg = g_wifiTogglePending ? L"Turning Wi-Fi off..." : L"Wi-Fi is turned off";
            g_brGrey->SetOpacity(1.0f);
            
            if (g_tfPct) g_tfPct->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            
            g_ctx->DrawText(offMsg, static_cast<UINT32>(wcslen(offMsg)), g_tfPct.Get(),
                D2D1::RectF(0.0f, curY, sz.width, curY + 80.0f), g_brGrey.Get());
                
            if (g_tfPct) g_tfPct->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        else
        {
            if (listTotalH <= 0.0f)
            {
                const wchar_t* scanMsg = g_wifiTogglePending ? L"Turning Wi-Fi on..." : L"Looking for networks...";
                g_brGrey->SetOpacity(1.0f);
                if (g_tfPct) g_tfPct->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                g_ctx->DrawText(scanMsg, static_cast<UINT32>(wcslen(scanMsg)), g_tfPct.Get(),
                    D2D1::RectF(0.0f, curY, sz.width, curY + 64.0f), g_brGrey.Get());
                if (g_tfPct) g_tfPct->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                curY += 64.0f;
            }
            else
            {
                g_wifiClipRect = D2D1::RectF(0.0f, curY, sz.width, curY + listViewH);
                g_ctx->PushAxisAlignedClip(g_wifiClipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                D2D1_MATRIX_3X2_F oldTr;
                g_ctx->GetTransform(&oldTr);
                g_ctx->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -g_wifiScrollY) * oldTr);

            float listY = curY;
            g_wifiNetRects.resize(g_animWifiNetworks.size());
            g_wifiNetHovT.resize(g_animWifiNetworks.size(), 0.0f);

            for (size_t i = 0; i < g_animWifiNetworks.size(); ++i)
            {
                const auto& a = g_animWifiNetworks[i];
                const auto& net = a.data;
                const float extent = HeightMorphExtent(a.alpha, a.isRemoving);
                float itemH = WIFI_ITEM_H * extent;
                float gapH = (extent > 0.01f && i + 1 < g_animWifiNetworks.size()) ? WIFI_GAP * extent : 0.0f;

                g_wifiNetRects[i] = D2D1::RectF(D_PAD, listY - g_wifiScrollY, sz.width - D_PAD, listY + itemH - g_wifiScrollY);

                if (a.alpha < 0.99f) {
                    g_ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::Matrix3x2F::Scale(a.scale, a.scale, D2D1::Point2F(sz.width * 0.5f, listY + itemH * 0.5f)), a.alpha), nullptr);
                }

                g_ctx->PushAxisAlignedClip(D2D1::RectF(D_PAD, listY, sz.width - D_PAD, listY + itemH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                D2D1_RECT_F itemRect = D2D1::RectF(D_PAD, listY, sz.width - D_PAD, listY + WIFI_ITEM_H);
                const float itemMidY = listY + WIFI_ITEM_H * 0.5f;

                bool isClicked = (g_wifiNetClickIdx == static_cast<int>(i));
                D2D1_MATRIX_3X2_F itemTr;
                if (isClicked) {
                    g_ctx->GetTransform(&itemTr);
                    g_ctx->SetTransform(D2D1::Matrix3x2F::Scale(0.98f, 0.98f, D2D1::Point2F(sz.width * 0.5f, itemMidY)) * itemTr);
                }

                if (net.isConnected)
                {
                    ComPtr<ID2D1SolidColorBrush> activeBg;
                    g_ctx->CreateSolidColorBrush(g_clrPill, &activeBg);
                    g_ctx->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 6.0f, 6.0f), activeBg.Get());
                }
                else
                {
                    ComPtr<ID2D1SolidColorBrush> brItemBg;
                    D2D1_COLOR_F baseCol = D2D1::ColorF(0.08f, 0.08f, 0.08f, 0.0f); // Transparent until hovered
                    D2D1_COLOR_F hovCol = D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f);
                    g_ctx->CreateSolidColorBrush(LerpColor(baseCol, hovCol, g_wifiNetHovT[i]), &brItemBg);
                    g_ctx->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 6.0f, 6.0f), brItemBg.Get());
                }

                const float iconR = 14.0f;
                const float iconCX = D_PAD + 12.0f + iconR;

                ComPtr<ID2D1SolidColorBrush> iconCircBr;
                g_ctx->CreateSolidColorBrush(net.isConnected ? g_clrPillIco : D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f), &iconCircBr);
                g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iconCX, itemMidY), iconR, iconR), iconCircBr.Get());

                DrawSvgColored(WifiSvg(WifiStateFromSignal(net.signalQuality)), iconCX, itemMidY, 14.0f, net.isConnected ? g_clrFill : CLR_WHITE);

                const float textX = iconCX + iconR + 12.0f;
                const float textW = sz.width - D_PAD - 40.0f - textX;
                
                g_brWhite->SetOpacity(1.0f);
                g_ctx->DrawText(net.ssid.c_str(), static_cast<UINT32>(net.ssid.size()), g_tfName.Get(),
                    D2D1::RectF(textX, itemMidY - 14.0f, textX + textW, itemMidY + 2.0f), g_brWhite.Get());

                std::wstring subText = net.isConnected ? (net.isSecure ? L"Connected, secured" : L"Connected, open") : (net.isSecure ? L"Secured" : L"Open");
                g_brGrey->SetOpacity(1.0f);
                g_ctx->DrawText(subText.c_str(), static_cast<UINT32>(subText.size()), g_tfSub.Get(),
                    D2D1::RectF(textX, itemMidY + 2.0f, textX + textW, itemMidY + 16.0f), g_brGrey.Get());

                const float rightIconX = sz.width - D_PAD - 20.0f;
                if (net.isConnected)
                {
                    DrawSvgColored(g_svgCheck.Get(), rightIconX, itemMidY, 16.0f, g_clrFill);
                }
                else if (net.isConnecting)
                {
                    D2D1_MATRIX_3X2_F iconTr;
                    g_ctx->GetTransform(&iconTr);
                    g_ctx->SetTransform(D2D1::Matrix3x2F::Rotation(g_wifiConnectingAngle, D2D1::Point2F(rightIconX, itemMidY)) * iconTr);
                    DrawSvgColored(g_svgRefresh.Get(), rightIconX, itemMidY, 14.0f, D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f));
                    g_ctx->SetTransform(iconTr);
                }
                else if (net.isSecure)
                {
                    DrawSvgColored(g_svgKey.Get(), rightIconX, itemMidY, 14.0f, D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f));
                }

                if (isClicked) {
                    g_ctx->SetTransform(itemTr);
                }

                g_ctx->PopAxisAlignedClip(); // end item clip
                if (a.alpha < 0.99f) g_ctx->PopLayer();

                listY += itemH + gapH;
            }

                g_ctx->SetTransform(oldTr);
                g_ctx->PopAxisAlignedClip();

                // Scrollbar
                if (maxScroll > 0.0f)
                {
                    float viewRatio = listViewH / listTotalH;
                    float sbH = (std::max)(16.0f, listViewH * viewRatio);
                    float sbY = curY + (g_wifiScrollY / maxScroll) * (listViewH - sbH);
                    ComPtr<ID2D1SolidColorBrush> sbBr;
                    g_ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f), &sbBr);
                    g_ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sz.width - SCROLLBAR_W - 4.0f, sbY, sz.width - 4.0f, sbY + sbH), SCROLLBAR_W * 0.5f, SCROLLBAR_W * 0.5f), sbBr.Get());
                }

                curY += listViewH;
            }
            
            // Separator
            curY += 8.0f;
            g_ctx->FillRectangle(D2D1::RectF(D_PAD, curY, sz.width - D_PAD, curY + 1.0f), g_brSep.Get());
            curY += 1.0f + 8.0f;

            // Refresh button (scaled down)
            const float refW = 68.0f;
            const float refH = 26.0f;
            const float refX = sz.width - D_PAD - refW;
            g_wifiRefreshRect = D2D1::RectF(refX, curY, refX + refW, curY + refH);

            D2D1_MATRIX_3X2_F btnTr;
            if (g_wifiRefreshClick) {
                g_ctx->GetTransform(&btnTr);
                g_ctx->SetTransform(D2D1::Matrix3x2F::Scale(0.95f, 0.95f, D2D1::Point2F(refX + refW * 0.5f, curY + refH * 0.5f)) * btnTr);
            }

            ComPtr<ID2D1SolidColorBrush> refBr;
            g_ctx->CreateSolidColorBrush(LerpColor(D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.0f), CLR_BTN_HOV, g_wifiRefreshHovT), &refBr);
            g_ctx->FillRoundedRectangle(D2D1::RoundedRect(g_wifiRefreshRect, 4.0f, 4.0f), refBr.Get());

            D2D1_COLOR_F iconHovCol = LerpColor(D2D1::ColorF(0.6f, 0.6f, 0.6f, 1.0f), CLR_WHITE, g_wifiRefreshHovT);
            
            float iconCx = refX + 12.0f;
            float iconCy = curY + refH * 0.5f;

            if (g_wifiRefreshSpinT > 0.0f) {
                D2D1_MATRIX_3X2_F iconTr;
                g_ctx->GetTransform(&iconTr);
                float angle = (1.0f - g_wifiRefreshSpinT) * 360.0f;
                g_ctx->SetTransform(D2D1::Matrix3x2F::Rotation(angle, D2D1::Point2F(iconCx, iconCy)) * iconTr);
                DrawSvgColored(g_svgRefresh.Get(), iconCx, iconCy, 12.0f, iconHovCol);
                g_ctx->SetTransform(iconTr);
            } else {
                DrawSvgColored(g_svgRefresh.Get(), iconCx, iconCy, 12.0f, iconHovCol);
            }
            
            const wchar_t* refText = L"Refresh";
            ComPtr<ID2D1SolidColorBrush> refTextBr;
            g_ctx->CreateSolidColorBrush(LerpColor(CLR_GREY, CLR_WHITE, g_wifiRefreshHovT), &refTextBr);
            g_ctx->DrawText(refText, static_cast<UINT32>(wcslen(refText)), g_tfSub.Get(), // Use smaller tfSub
                D2D1::RectF(refX + 24.0f, curY + refH * 0.5f - 8.0f, refX + refW, curY + refH * 0.5f + 8.0f), refTextBr.Get());

            if (g_wifiRefreshClick) {
                g_ctx->SetTransform(btnTr);
            }
        }

        g_ctx->PopLayer(); // End Wifi Panel Layer
    }
}
