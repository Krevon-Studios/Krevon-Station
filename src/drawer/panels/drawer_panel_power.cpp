// drawer_panel_power.cpp - Power sub-panel rendering.
#include "drawer_panel.h"
#include "../render/drawer_render.h"
#include <algorithm>

void PowerPanel_Render(float opacity)
{
    const D2D1_SIZE_F sz = g_ctx->GetSize();
    if (opacity > 0.01f)
    {
        D2D1_LAYER_PARAMETERS pwrLayerParams = D2D1::LayerParameters();
        pwrLayerParams.opacity = opacity;
        g_ctx->PushLayer(pwrLayerParams, nullptr);

        const float headerMidY = D_PAD + ROW1_H * 0.5f;

        // Back button
        const float backBtnR = 16.0f;
        const float backCX = D_PAD + backBtnR;
        g_backEllipse = D2D1::Ellipse(D2D1::Point2F(backCX, headerMidY), backBtnR, backBtnR);

        // Back button circle (draws base CLR_BTN and interpolates to CLR_BTN_HOV)
        D2D1_COLOR_F backCol = LerpColor(CLR_BTN, CLR_BTN_HOV, g_backHovT);
        ComPtr<ID2D1SolidColorBrush> brBackLerp;
        g_ctx->CreateSolidColorBrush(backCol, &brBackLerp);
        brBackLerp->SetOpacity(1.0f);
        g_ctx->FillEllipse(g_backEllipse, brBackLerp.Get());

        // Use DrawSvgColored to guarantee it renders in white, make it proportional (18.0f)
        // and nudge it slightly left (-1.0f) to optically center the chevron within the circle
        // (the visual mass of a '<' is on the right, so it needs a leftward nudge)
        DrawSvgColored(g_svgChevronLeft.Get(), backCX - 1.0f, headerMidY, 18.0f, CLR_WHITE);

        // "Power" title
        const wchar_t* pwrTitle = L"Power";
        const float pwrTitleX = backCX + backBtnR + 12.0f;
        g_ctx->DrawText(pwrTitle, static_cast<UINT32>(wcslen(pwrTitle)), g_tfName.Get(),
            D2D1::RectF(pwrTitleX, headerMidY - 10.0f, sz.width, headerMidY + 10.0f), g_brWhite.Get());

        // Separator
        const float sepY = D_PAD + ROW1_H + GAP_SEP_TOP;
        g_brSep->SetOpacity(1.0f);
        g_ctx->DrawLine(D2D1::Point2F(0.0f, sepY), D2D1::Point2F(sz.width, sepY), g_brSep.Get(), 1.0f);

        // 3 Power Options
        const float listTop = sepY + 1.0f + GAP_SEP_BOT;
        const wchar_t* pwrTitles[3] = { L"Sleep", L"Restart", L"Shut down" };
        const wchar_t* pwrSubs[3]   = { L"Low power state", L"Reboot Windows", L"Turn off this PC" };
        ID2D1SvgDocument* pwrIcons[3] = { g_svgMoon.Get(), g_svgRestart.Get(), g_svgPowerOff.Get() };

        for (int i = 0; i < 3; ++i)
        {
            const float itemY = listTop + i * PWR_ITEM_H;
            g_pwrItemRect[i] = D2D1::RectF(D_PAD, itemY, sz.width - D_PAD, itemY + PWR_ITEM_H);

            // Hover background
            if (g_pwrItemHovT[i] > 0.0f)
            {
                ComPtr<ID2D1SolidColorBrush> brItemHov;
                g_ctx->CreateSolidColorBrush(LerpColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f), CLR_BTN_HOV, g_pwrItemHovT[i]), &brItemHov);
                g_ctx->FillRoundedRectangle(
                    D2D1::RoundedRect(g_pwrItemRect[i], 8.0f, 8.0f), brItemHov.Get());
            }

            const float iconCX = D_PAD + 16.0f;
            const float itemMidY = itemY + PWR_ITEM_H * 0.5f;

            g_brWhite->SetOpacity(1.0f);
            DrawSvg(pwrIcons[i], iconCX, itemMidY, BTN_ICON_SZ);

            const float textX = iconCX + 16.0f + 12.0f;
            const float textMidY = itemY + PWR_ITEM_H * 0.5f + 1.0f; // slight downward nudge for optical centering
            
            g_ctx->DrawText(pwrTitles[i], static_cast<UINT32>(wcslen(pwrTitles[i])), g_tfName.Get(),
                D2D1::RectF(textX, textMidY - 16.0f, sz.width, textMidY + 1.0f), g_brWhite.Get());

            g_brGrey->SetOpacity(1.0f);
            g_ctx->DrawText(pwrSubs[i], static_cast<UINT32>(wcslen(pwrSubs[i])), g_tfSub.Get(),
                D2D1::RectF(textX, textMidY - 1.0f, sz.width, textMidY + 16.0f), g_brGrey.Get());
        }

        g_ctx->PopLayer(); // End Power Panel Layer
    }
}
