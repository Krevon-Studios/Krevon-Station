// drawer_panel_bluetooth.cpp - Bluetooth device list panel rendering and metrics.
#include "drawer_panel.h"
#include "../render/drawer_render.h"
#include <algorithm>

void BluetoothPanel_UpdateMetrics()
{
    constexpr float BT_ITEM_H = 46.0f;
    constexpr float BT_GAP = 6.0f;
    constexpr float REFRESH_BTN_H = 32.0f;

    float listTotalH = 0.0f;
    for (const auto& a : g_animBtDevices) {
        const float extent = HeightMorphExtent(a.alpha, a.isRemoving);
        listTotalH += BT_ITEM_H * extent + (extent > 0.01f ? BT_GAP * extent : 0.0f);
    }
    if (listTotalH > 0.0f) listTotalH -= BT_GAP;

    const float listMaxViewH = BT_ITEM_H * 5.0f + BT_GAP * 4.0f;
    float listViewH = (std::min)(listTotalH, listMaxViewH);

    bool isBtOn = g_btTogglePending ? g_btToggleTargetOn : (g_snap.bluetooth != BluetoothIconState::Off);

    float contentH = D_PAD + ROW1_H + GAP_SEP_TOP + 1.0f + GAP_SEP_BOT; // Header + sep
    
    if (isBtOn)
    {
        contentH += (listTotalH > 0.0f) ? listViewH : 64.0f;
        contentH += 8.0f + 1.0f + 8.0f; // separator padding
        contentH += 26.0f; // REFRESH_BTN_H
        contentH += 8.0f;  // bottom padding
    }
    else
    {
        contentH += 80.0f; // Extra space for "Bluetooth is turned off" centered message
    }

    g_btTargetH = (std::min)(contentH, 600.0f); // Max height limit
}

void BluetoothPanel_Render(float opacity)
{
    const D2D1_SIZE_F sz = g_ctx->GetSize();
    constexpr float BT_ITEM_H = 46.0f;
    constexpr float BT_GAP = 6.0f;
    constexpr float REFRESH_BTN_H = 32.0f;

    if (opacity > 0.01f)
    {
        D2D1_LAYER_PARAMETERS btLayerParams = D2D1::LayerParameters();
        btLayerParams.opacity = opacity;
        g_ctx->PushLayer(btLayerParams, nullptr);

        float listTotalH = 0.0f;
        for (const auto& a : g_animBtDevices) {
            const float extent = HeightMorphExtent(a.alpha, a.isRemoving);
            listTotalH += BT_ITEM_H * extent + (extent > 0.01f ? BT_GAP * extent : 0.0f);
        }
        if (listTotalH > 0.0f) listTotalH -= BT_GAP;

        const float listMaxViewH = BT_ITEM_H * 5.0f + BT_GAP * 4.0f;
        float listViewH = (std::min)(listTotalH, listMaxViewH);

        float maxScroll = (std::max)(0.0f, listTotalH - listViewH);
        g_btScrollTargetY = (std::clamp)(g_btScrollTargetY, 0.0f, maxScroll);
        g_btScrollY = (std::clamp)(g_btScrollY, 0.0f, maxScroll);

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

        // "Bluetooth" title
        const wchar_t* btTitle = L"Bluetooth";
        const float titleX = backCX + backBtnR + 12.0f;
        g_ctx->DrawText(btTitle, static_cast<UINT32>(wcslen(btTitle)), g_tfName.Get(),
            D2D1::RectF(titleX, headerMidY - 10.0f, sz.width, headerMidY + 10.0f), g_brWhite.Get());

        // Toggle Switch on the right
        const float toggleW = 44.0f;
        const float toggleH = 24.0f;
        const float toggleX = sz.width - D_PAD - toggleW;
        const float toggleY = headerMidY - toggleH * 0.5f;
        g_btToggleRect = D2D1::RectF(toggleX, toggleY, toggleX + toggleW, toggleY + toggleH);

        bool isBtOn = (g_snap.bluetooth != BluetoothIconState::Off);
        if (g_btTogglePending)
            isBtOn = g_btToggleTargetOn;

        ComPtr<ID2D1SolidColorBrush> toggleBg;
        D2D1_COLOR_F offCol = D2D1::ColorF(0.2f, 0.2f, 0.2f, 1.0f);
        D2D1_COLOR_F onCol = g_clrFill; // the pastel accent
        D2D1_COLOR_F curBg = LerpColor(isBtOn ? offCol : onCol, isBtOn ? onCol : offCol, 1.0f); // Fast toggle just use state
        g_ctx->CreateSolidColorBrush(curBg, &toggleBg);
        g_ctx->FillRoundedRectangle(D2D1::RoundedRect(g_btToggleRect, toggleH * 0.5f, toggleH * 0.5f), toggleBg.Get());

        // Thumb
        const float thumbR = 9.0f;
        float thumbXOffset = isBtOn ? (toggleW - thumbR - 3.0f) : (thumbR + 3.0f);
        g_btToggleThumbRect = D2D1::RectF(toggleX + thumbXOffset - thumbR, toggleY + toggleH * 0.5f - thumbR, toggleX + thumbXOffset + thumbR, toggleY + toggleH * 0.5f + thumbR);
        
        ComPtr<ID2D1SolidColorBrush> thumbBr;
        g_ctx->CreateSolidColorBrush(isBtOn ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.6f, 0.6f, 0.6f, 1.0f), &thumbBr);
        g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(toggleX + thumbXOffset, toggleY + toggleH * 0.5f), thumbR, thumbR), thumbBr.Get());

        if (g_btTogglePending)
        {
            D2D1_MATRIX_3X2_F iconTr;
            const float spinCX = toggleX + toggleW * 0.5f;
            const float spinCY = toggleY + toggleH * 0.5f;
            g_ctx->GetTransform(&iconTr);
            g_ctx->SetTransform(D2D1::Matrix3x2F::Rotation(g_btConnectingAngle, D2D1::Point2F(spinCX, spinCY)) * iconTr);
            DrawSvgColored(g_svgRefresh.Get(), spinCX, spinCY, 13.0f, isBtOn ? g_clrPillIco : D2D1::ColorF(0.72f, 0.72f, 0.72f, 1.0f));
            g_ctx->SetTransform(iconTr);
        }

        // Separator
        const float sepY = D_PAD + ROW1_H + GAP_SEP_TOP;
        g_brSep->SetOpacity(1.0f);
        g_ctx->DrawLine(D2D1::Point2F(0.0f, sepY), D2D1::Point2F(sz.width, sepY), g_brSep.Get(), 1.0f);

        float curY = sepY + 1.0f + GAP_SEP_BOT;

        if (!isBtOn)
        {
            const wchar_t* offMsg = g_btTogglePending ? L"Turning Bluetooth off..." : L"Bluetooth is turned off";
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
                const wchar_t* scanMsg = g_btTogglePending ? L"Turning Bluetooth on..." : L"No devices paired";
                g_brGrey->SetOpacity(1.0f);
                if (g_tfPct) g_tfPct->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                g_ctx->DrawText(scanMsg, static_cast<UINT32>(wcslen(scanMsg)), g_tfPct.Get(),
                    D2D1::RectF(0.0f, curY, sz.width, curY + 64.0f), g_brGrey.Get());
                if (g_tfPct) g_tfPct->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                curY += 64.0f;
            }
            else
            {
                g_btClipRect = D2D1::RectF(0.0f, curY, sz.width, curY + listViewH);
                g_ctx->PushAxisAlignedClip(g_btClipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                D2D1_MATRIX_3X2_F oldTr;
                g_ctx->GetTransform(&oldTr);
                g_ctx->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -g_btScrollY) * oldTr);

            float listY = curY;
            g_btDevRects.resize(g_animBtDevices.size());
            g_btDevHovT.resize(g_animBtDevices.size(), 0.0f);

            for (size_t i = 0; i < g_animBtDevices.size(); ++i)
            {
                const auto& a = g_animBtDevices[i];
                const auto& dev = a.data;
                const bool isBusy = dev.isConnecting || dev.isDisconnecting;
                const float extent = HeightMorphExtent(a.alpha, a.isRemoving);
                float itemH = BT_ITEM_H * extent;
                float gapH = (extent > 0.01f && i + 1 < g_animBtDevices.size()) ? BT_GAP * extent : 0.0f;

                g_btDevRects[i] = D2D1::RectF(D_PAD, listY - g_btScrollY, sz.width - D_PAD, listY + itemH - g_btScrollY);

                if (a.alpha < 0.99f) {
                    g_ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::Matrix3x2F::Scale(a.scale, a.scale, D2D1::Point2F(sz.width * 0.5f, listY + itemH * 0.5f)), a.alpha), nullptr);
                }

                g_ctx->PushAxisAlignedClip(D2D1::RectF(D_PAD, listY, sz.width - D_PAD, listY + itemH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                D2D1_RECT_F itemRect = D2D1::RectF(D_PAD, listY, sz.width - D_PAD, listY + BT_ITEM_H);
                const float itemMidY = listY + BT_ITEM_H * 0.5f;

                bool isClicked = (g_btDevClickIdx == static_cast<int>(i));
                D2D1_MATRIX_3X2_F itemTr;
                if (isClicked) {
                    g_ctx->GetTransform(&itemTr);
                    g_ctx->SetTransform(D2D1::Matrix3x2F::Scale(0.98f, 0.98f, D2D1::Point2F(sz.width * 0.5f, itemMidY)) * itemTr);
                }

                if (dev.isConnected)
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
                    g_ctx->CreateSolidColorBrush(LerpColor(baseCol, hovCol, g_btDevHovT[i]), &brItemBg);
                    g_ctx->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 6.0f, 6.0f), brItemBg.Get());
                }

                const float iconR = 14.0f;
                const float iconCX = D_PAD + 12.0f + iconR;

                ComPtr<ID2D1SolidColorBrush> iconCircBr;
                g_ctx->CreateSolidColorBrush(dev.isConnected ? g_clrPillIco : D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f), &iconCircBr);
                g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iconCX, itemMidY), iconR, iconR), iconCircBr.Get());

                DrawSvgColored(BtSvg(BluetoothIconState::Connected), iconCX, itemMidY, 14.0f, dev.isConnected ? g_clrFill : CLR_WHITE);

                // Reserve space for right-side label/icon
                const float rightAreaW = (dev.isConnected && !isBusy) ? 68.0f : 20.0f;
                const float textX = iconCX + iconR + 12.0f;
                const float textW = sz.width - D_PAD - rightAreaW - 8.0f - textX;
                
                g_brWhite->SetOpacity(1.0f);
                g_ctx->DrawText(dev.name.c_str(), static_cast<UINT32>(dev.name.size()), g_tfName.Get(),
                    D2D1::RectF(textX, itemMidY - 14.0f, textX + textW, itemMidY + 2.0f), g_brWhite.Get());

                std::wstring subText = dev.isConnecting ? L"Connecting..." : (dev.isDisconnecting ? L"Disconnecting..." : (dev.isConnected ? L"Connected" : L"Paired"));
                g_brGrey->SetOpacity(1.0f);
                g_ctx->DrawText(subText.c_str(), static_cast<UINT32>(subText.size()), g_tfSub.Get(),
                    D2D1::RectF(textX, itemMidY + 2.0f, textX + textW, itemMidY + 16.0f), g_brGrey.Get());

                // Right-side action area
                const float rightEdge = sz.width - D_PAD - 8.0f;
                if (isBusy)
                {
                    const float spinX = rightEdge - 7.0f;
                    D2D1_MATRIX_3X2_F iconTr;
                    g_ctx->GetTransform(&iconTr);
                    g_ctx->SetTransform(D2D1::Matrix3x2F::Rotation(g_btConnectingAngle, D2D1::Point2F(spinX, itemMidY)) * iconTr);
                    DrawSvgColored(g_svgRefresh.Get(), spinX, itemMidY, 14.0f, D2D1::ColorF(0.55f, 0.55f, 0.55f, 1.0f));
                    g_ctx->SetTransform(iconTr);
                }
                else if (dev.isConnected)
                {
                    const float discW = 62.0f;
                    const float discX = rightEdge - discW;
                    const wchar_t* discLabel = L"Disconnect";
                    ComPtr<ID2D1SolidColorBrush> discTextBr;
                    D2D1_COLOR_F discTextCol = D2D1::ColorF(0.85f, 0.36f, 0.36f, 1.0f);
                    g_ctx->CreateSolidColorBrush(discTextCol, &discTextBr);
                    g_ctx->DrawText(discLabel, static_cast<UINT32>(wcslen(discLabel)), g_tfSub.Get(),
                        D2D1::RectF(discX, itemMidY - 8.0f, discX + discW, itemMidY + 8.0f), discTextBr.Get());
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
                    float sbY = curY + (g_btScrollY / maxScroll) * (listViewH - sbH);
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
            g_btRefreshRect = D2D1::RectF(refX, curY, refX + refW, curY + refH);

            D2D1_MATRIX_3X2_F btnTr;
            if (g_btRefreshClick) {
                g_ctx->GetTransform(&btnTr);
                g_ctx->SetTransform(D2D1::Matrix3x2F::Scale(0.95f, 0.95f, D2D1::Point2F(refX + refW * 0.5f, curY + refH * 0.5f)) * btnTr);
            }

            ComPtr<ID2D1SolidColorBrush> refBr;
            g_ctx->CreateSolidColorBrush(LerpColor(D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.0f), CLR_BTN_HOV, g_btRefreshHovT), &refBr);
            g_ctx->FillRoundedRectangle(D2D1::RoundedRect(g_btRefreshRect, 4.0f, 4.0f), refBr.Get());

            D2D1_COLOR_F iconHovCol = LerpColor(D2D1::ColorF(0.6f, 0.6f, 0.6f, 1.0f), CLR_WHITE, g_btRefreshHovT);
            
            float iconCx = refX + 12.0f;
            float iconCy = curY + refH * 0.5f;

            if (g_btRefreshSpinT > 0.0f) {
                D2D1_MATRIX_3X2_F iconTr;
                g_ctx->GetTransform(&iconTr);
                float angle = (1.0f - g_btRefreshSpinT) * 360.0f;
                g_ctx->SetTransform(D2D1::Matrix3x2F::Rotation(angle, D2D1::Point2F(iconCx, iconCy)) * iconTr);
                DrawSvgColored(g_svgRefresh.Get(), iconCx, iconCy, 12.0f, iconHovCol);
                g_ctx->SetTransform(iconTr);
            } else {
                DrawSvgColored(g_svgRefresh.Get(), iconCx, iconCy, 12.0f, iconHovCol);
            }
            
            const wchar_t* refText = L"Refresh";
            ComPtr<ID2D1SolidColorBrush> refTextBr;
            g_ctx->CreateSolidColorBrush(LerpColor(CLR_GREY, CLR_WHITE, g_btRefreshHovT), &refTextBr);
            g_ctx->DrawText(refText, static_cast<UINT32>(wcslen(refText)), g_tfSub.Get(), // Use smaller tfSub
                D2D1::RectF(refX + 24.0f, curY + refH * 0.5f - 8.0f, refX + refW, curY + refH * 0.5f + 8.0f), refTextBr.Get());

            if (g_btRefreshClick) {
                g_ctx->SetTransform(btnTr);
            }
        }

        g_ctx->PopLayer(); // End BT Panel Layer
    }
}
