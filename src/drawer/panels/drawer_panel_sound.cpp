// drawer_panel_sound.cpp - Sound output panel rendering and layout metrics.
#include "drawer_panel.h"
#include "../render/drawer_render.h"
#include <algorithm>

void SoundPanel_UpdateMetrics()
{
    constexpr float SND_DEVICE_GAP = 4.0f;

    float devicesTotalH = 0.0f;
    for (const auto& a : g_animEndpoints) {
        const float extent = HeightMorphExtent(a.alpha, a.isRemoving);
        devicesTotalH += SND_ITEM_H * extent + (extent > 0.01f ? SND_DEVICE_GAP * extent : 0.0f);
    }
    if (devicesTotalH > 0.0f) devicesTotalH -= SND_DEVICE_GAP;

    float appsTotalH = 0.0f;
    for (const auto& a : g_animSessions)
        appsTotalH += SND_ITEM_H * HeightMorphExtent(a.alpha, a.isRemoving);

    const float devicesMaxViewH = SND_ITEM_H * 3.0f + SND_DEVICE_GAP * 2.0f;
    const float appsMaxViewH = SND_ITEM_H * 4.0f;

    g_sndMetrics.devicesTotalH = devicesTotalH;
    g_sndMetrics.appsTotalH = appsTotalH;
    g_sndMetrics.devicesViewH = (std::min)(devicesTotalH, devicesMaxViewH);
    g_sndMetrics.appsViewH = (std::min)(appsTotalH, appsMaxViewH);

    float sndContentH = D_PAD + ROW1_H + GAP_SEP_TOP + 1.0f + GAP_SEP_BOT;
    sndContentH += 32.0f;
    sndContentH += g_sndMetrics.devicesViewH;
    sndContentH += 32.0f;
    sndContentH += g_sndMetrics.appsViewH;
    sndContentH += D_PAD;

    g_sndMetrics.targetH = (std::min)(sndContentH, SND_MAX_H);
    g_sndTargetH = g_sndMetrics.targetH;
}

void SoundPanel_Render(float opacity)
{
    const D2D1_SIZE_F sz = g_ctx->GetSize();
    constexpr float SND_DEVICE_GAP = 4.0f;
    const float devicesTotalH = g_sndMetrics.devicesTotalH;
    const float appsTotalH = g_sndMetrics.appsTotalH;
    const float devicesViewH = g_sndMetrics.devicesViewH;
    const float appsViewH = g_sndMetrics.appsViewH;
    if (opacity > 0.01f)
    {
        D2D1_LAYER_PARAMETERS sndLayerParams = D2D1::LayerParameters();
        sndLayerParams.opacity = opacity;
        g_ctx->PushLayer(sndLayerParams, nullptr);

        // Clamp scroll values
        float maxDeviceScroll = (std::max)(0.0f, devicesTotalH - devicesViewH);
        g_sndDeviceScrollTargetY = (std::clamp)(g_sndDeviceScrollTargetY, 0.0f, maxDeviceScroll);
        g_sndDeviceScrollY = (std::clamp)(g_sndDeviceScrollY, 0.0f, maxDeviceScroll);

        float maxAppScroll = (std::max)(0.0f, appsTotalH - appsViewH);
        g_sndAppScrollTargetY = (std::clamp)(g_sndAppScrollTargetY, 0.0f, maxAppScroll);
        g_sndAppScrollY = (std::clamp)(g_sndAppScrollY, 0.0f, maxAppScroll);

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

        // "Sound output" title
        const wchar_t* sndTitle = L"Sound output";
        const float sndTitleX = backCX + backBtnR + 12.0f;
        g_ctx->DrawText(sndTitle, static_cast<UINT32>(wcslen(sndTitle)), g_tfName.Get(),
            D2D1::RectF(sndTitleX, headerMidY - 10.0f, sz.width, headerMidY + 10.0f), g_brWhite.Get());

        // Separator
        const float sepY = D_PAD + ROW1_H + GAP_SEP_TOP;
        g_brSep->SetOpacity(1.0f);
        g_ctx->DrawLine(D2D1::Point2F(0.0f, sepY), D2D1::Point2F(sz.width, sepY), g_brSep.Get(), 1.0f);

        float curY = sepY + 1.0f + GAP_SEP_BOT;

        // ── Output Devices ──
        const wchar_t* hdgOut = L"OUTPUT DEVICES";
        g_brGrey->SetOpacity(1.0f);
        g_ctx->DrawText(hdgOut, static_cast<UINT32>(wcslen(hdgOut)), g_tfSndHdg.Get(),
            D2D1::RectF(D_PAD, curY, sz.width, curY + 16.0f), g_brGrey.Get());
        curY += 24.0f;

        g_sndDeviceClipRect = D2D1::RectF(0.0f, curY, sz.width, curY + devicesViewH);
        g_ctx->PushAxisAlignedClip(g_sndDeviceClipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        D2D1_MATRIX_3X2_F oldTr;
        g_ctx->GetTransform(&oldTr);
        g_ctx->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -g_sndDeviceScrollY) * oldTr);

        float listY = curY;
        for (size_t i = 0; i < g_animEndpoints.size(); ++i)
        {
            const auto& a = g_animEndpoints[i];
            const auto& ep = a.data;
            const float extent = HeightMorphExtent(a.alpha, a.isRemoving);
            float itemH = SND_ITEM_H * extent;
            float gapH = (extent > 0.01f && i + 1 < g_animEndpoints.size()) ? SND_DEVICE_GAP * extent : 0.0f;

            g_sndDeviceRects[i] = D2D1::RectF(D_PAD, listY - g_sndDeviceScrollY, sz.width - D_PAD, listY + itemH - g_sndDeviceScrollY);

            if (a.alpha < 0.99f) {
                g_ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::Matrix3x2F::Scale(a.scale, a.scale, D2D1::Point2F(sz.width * 0.5f, listY + itemH * 0.5f)), a.alpha), nullptr);
            }

            // To avoid squishing visuals, we clip the item to its animating height and draw at normal size
            g_ctx->PushAxisAlignedClip(D2D1::RectF(D_PAD, listY, sz.width - D_PAD, listY + itemH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            D2D1_RECT_F itemRect = D2D1::RectF(D_PAD, listY, sz.width - D_PAD, listY + SND_ITEM_H);

            if (ep.isActive)
            {
                ComPtr<ID2D1SolidColorBrush> activeBg;
                g_ctx->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f), &activeBg);
                g_ctx->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 8.0f, 8.0f), activeBg.Get());

                ComPtr<ID2D1SolidColorBrush> accentBr;
                g_ctx->CreateSolidColorBrush(g_accentColor, &accentBr);
                g_ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(D_PAD + 12.0f, listY + 14.0f, D_PAD + 14.0f, listY + SND_ITEM_H - 14.0f), 1.0f, 1.0f), accentBr.Get());
            }
            else if (g_sndDeviceHovT[i] > 0.0f)
            {
                ComPtr<ID2D1SolidColorBrush> brItemHov;
                g_ctx->CreateSolidColorBrush(LerpColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f), CLR_BTN_HOV, g_sndDeviceHovT[i]), &brItemHov);
                g_ctx->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 8.0f, 8.0f), brItemHov.Get());
            }

            const float iconCX = D_PAD + 16.0f + 16.0f;
            const float itemMidY = listY + SND_ITEM_H * 0.5f;

            g_brWhite->SetOpacity(ep.isActive ? 1.0f : 0.6f);
            DrawSvg(g_svgHeadphones.Get(), iconCX, itemMidY, BTN_ICON_SZ);

            const float textX = iconCX + 16.0f + 8.0f;
            g_brWhite->SetOpacity(1.0f);
            g_ctx->DrawText(ep.name.c_str(), static_cast<UINT32>(ep.name.size()), g_tfName.Get(),
                D2D1::RectF(textX, itemMidY - 10.0f, sz.width - D_PAD - 32.0f, itemMidY + 10.0f), g_brWhite.Get());

            if (ep.isActive)
            {
                ComPtr<ID2D1SolidColorBrush> accentBr;
                g_ctx->CreateSolidColorBrush(g_accentColor, &accentBr);
                const float chkX = sz.width - D_PAD - 20.0f;
                g_ctx->DrawLine(D2D1::Point2F(chkX, itemMidY), D2D1::Point2F(chkX + 4.0f, itemMidY + 4.0f), accentBr.Get(), 1.5f);
                g_ctx->DrawLine(D2D1::Point2F(chkX + 4.0f, itemMidY + 4.0f), D2D1::Point2F(chkX + 10.0f, itemMidY - 4.0f), accentBr.Get(), 1.5f);
            }

            g_ctx->PopAxisAlignedClip(); // end item clip

            if (a.alpha < 0.99f) g_ctx->PopLayer();

            listY += itemH + gapH;
        }

        g_ctx->SetTransform(oldTr);
        g_ctx->PopAxisAlignedClip();

        // Device Scrollbar
        if (maxDeviceScroll > 0.0f)
        {
            float viewRatio = devicesViewH / devicesTotalH;
            float sbH = (std::max)(16.0f, devicesViewH * viewRatio);
            float sbY = curY + (g_sndDeviceScrollY / maxDeviceScroll) * (devicesViewH - sbH);
            ComPtr<ID2D1SolidColorBrush> sbBr;
            g_ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f), &sbBr);
            g_ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sz.width - SCROLLBAR_W - 4.0f, sbY, sz.width - 4.0f, sbY + sbH), SCROLLBAR_W * 0.5f, SCROLLBAR_W * 0.5f), sbBr.Get());
        }

        curY += devicesViewH;

        // ── Volume Mixer ──
        const wchar_t* hdgMix = L"VOLUME MIXER";
        g_brGrey->SetOpacity(1.0f);
        g_ctx->DrawText(hdgMix, static_cast<UINT32>(wcslen(hdgMix)), g_tfSndHdg.Get(),
            D2D1::RectF(D_PAD, curY + 8.0f, sz.width, curY + 24.0f), g_brGrey.Get());
        curY += 32.0f;
        g_sndMixerY = curY - 12.0f; // threshold for scroll event routing

        g_sndAppClipRect = D2D1::RectF(0.0f, curY, sz.width, curY + appsViewH);
        g_ctx->PushAxisAlignedClip(g_sndAppClipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        g_ctx->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -g_sndAppScrollY) * oldTr);

        listY = curY;
        for (size_t i = 0; i < g_animSessions.size(); ++i)
        {
            const auto& a = g_animSessions[i];
            const auto& sess = a.data;
            float itemH = SND_ITEM_H * HeightMorphExtent(a.alpha, a.isRemoving);
            
            const float iconR = 16.0f;
            const float iconCX = D_PAD + iconR;
            const float itemMidY = listY + SND_ITEM_H * 0.5f;

            g_sndAppIconRects[i] = D2D1::RectF(iconCX - iconR, itemMidY - iconR - g_sndAppScrollY, iconCX + iconR, itemMidY + iconR - g_sndAppScrollY);

            const float contentX = iconCX + iconR + 12.0f;
            const float pctW = 36.0f;
            const float pctX = sz.width - D_PAD - pctW;
            const float slL  = contentX;
            const float slR  = pctX - 6.0f;
            const float slY  = listY + 28.0f;
            
            g_sndAppSliderRects[i] = D2D1::RectF(slL, slY - SLIDER_H * 0.5f - g_sndAppScrollY, slR, slY + SLIDER_H * 0.5f - g_sndAppScrollY);

            if (a.alpha < 0.99f) {
                g_ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::Matrix3x2F::Scale(a.scale, a.scale, D2D1::Point2F(sz.width * 0.5f, listY + itemH * 0.5f)), a.alpha), nullptr);
            }

            g_ctx->PushAxisAlignedClip(D2D1::RectF(D_PAD, listY, sz.width - D_PAD, listY + itemH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            if (g_sndAppIconHovT[i] > 0.0f)
            {
                ComPtr<ID2D1SolidColorBrush> iconHovBr;
                g_ctx->CreateSolidColorBrush(LerpColor(CLR_BTN, CLR_BTN_HOV, g_sndAppIconHovT[i]), &iconHovBr);
                g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iconCX, itemMidY), iconR, iconR), iconHovBr.Get());
            }

            if (sess.muted) g_ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 0.3f), nullptr);

            if (sess.isSystemSound) {
                DrawSvgColored(g_svgSpeaker.Get(), iconCX, itemMidY, 20.0f, g_accentColor);
            } else {
                ComPtr<ID2D1Bitmap1> appIcon = GetAppIcon(sess.processId, sess.name);
                if (appIcon) g_ctx->DrawBitmap(appIcon.Get(), D2D1::RectF(iconCX - 10.0f, itemMidY - 10.0f, iconCX + 10.0f, itemMidY + 10.0f));
                else DrawSvgColored(g_svgSpeaker.Get(), iconCX, itemMidY, 20.0f, CLR_WHITE);
            }
            
            if (sess.muted) g_ctx->PopLayer();

            g_brGrey->SetOpacity(1.0f);
            g_ctx->DrawText(sess.name.c_str(), static_cast<UINT32>(sess.name.size()), g_tfName.Get(),
                D2D1::RectF(contentX, listY + 4.0f, sz.width - D_PAD - 40.0f, listY + 20.0f), g_brGrey.Get());

            const float displayVol = sess.muted ? 0.0f : sess.volume;
            g_brTrack->SetOpacity(1.0f);
            g_ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(slL, slY - SLIDER_H * 0.5f, slR, slY + SLIDER_H * 0.5f), SLIDER_H * 0.5f, SLIDER_H * 0.5f), g_brTrack.Get());

            const float fillR = slL + (slR - slL) * displayVol;
            if (fillR > slL)
            {
                g_brFill->SetOpacity(1.0f);
                g_ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(slL, slY - SLIDER_H * 0.5f, fillR, slY + SLIDER_H * 0.5f), SLIDER_H * 0.5f, SLIDER_H * 0.5f), g_brFill.Get());
            }

            float thumbX = (std::clamp)(slL + (slR - slL) * displayVol, slL + THUMB_R, slR - THUMB_R);
            g_brThumb->SetOpacity(1.0f);
            g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, slY), THUMB_R, THUMB_R), g_brThumb.Get());

            wchar_t pctBuf[8];
            swprintf_s(pctBuf, L"%d%%", static_cast<int>(displayVol * 100.0f + 0.5f));
            g_brGrey->SetOpacity(1.0f);
            g_ctx->DrawText(pctBuf, static_cast<UINT32>(wcslen(pctBuf)), g_tfPct.Get(),
                D2D1::RectF(pctX, slY - 9.0f, pctX + pctW, slY + 9.0f), g_brGrey.Get());

            g_ctx->PopAxisAlignedClip(); // end item clip

            if (a.alpha < 0.99f) g_ctx->PopLayer();

            listY += itemH;
        }

        g_ctx->SetTransform(oldTr);
        g_ctx->PopAxisAlignedClip();

        // App Scrollbar
        if (maxAppScroll > 0.0f)
        {
            float viewRatio = appsViewH / appsTotalH;
            float sbH = (std::max)(16.0f, appsViewH * viewRatio);
            float sbY = curY + (g_sndAppScrollY / maxAppScroll) * (appsViewH - sbH);
            ComPtr<ID2D1SolidColorBrush> sbBr;
            g_ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f), &sbBr);
            g_ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sz.width - SCROLLBAR_W - 4.0f, sbY, sz.width - 4.0f, sbY + sbH), SCROLLBAR_W * 0.5f, SCROLLBAR_W * 0.5f), sbBr.Get());
        }

        g_ctx->PopLayer(); // End Sound Panel Layer
    }
}
