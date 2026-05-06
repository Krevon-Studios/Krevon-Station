// drawer_notifications.cpp - Notification panel rendering and interaction.
#include "drawer_render.h"
#include "drawer.h"
#include "status/notification_status.h"

#include <algorithm>

namespace
{
    constexpr float PANEL_PAD = 14.0f;
    constexpr float HEADER_H = 48.0f;
    constexpr float BOTTOM_PAD = 14.0f;
    constexpr float ROW_PAD_X = 14.0f;
    constexpr float ROW_PAD_Y = 12.0f;
    constexpr float ICON_R = 20.0f;
    constexpr float CLOSE_R = 11.0f;
    constexpr float CLEAR_H = 26.0f;
    constexpr float CLEAR_W = 92.0f;

    std::wstring NotificationIconKey(const NotificationInfo& notification)
    {
        if (!notification.appUserModelId.empty())
            return notification.appUserModelId;
        return notification.appName + L"#" + std::to_wstring(notification.id);
    }

    bool SameNotification(const NotificationInfo& a, const NotificationInfo& b)
    {
        return a.id == b.id && a.appUserModelId == b.appUserModelId;
    }

    D2D1_RECT_F FitRect(D2D1_RECT_F box, D2D1_SIZE_F content)
    {
        if (content.width <= 0.0f || content.height <= 0.0f)
            return box;

        const float boxW = box.right - box.left;
        const float boxH = box.bottom - box.top;
        const float scale = (std::min)(boxW / content.width, boxH / content.height);
        const float w = content.width * scale;
        const float h = content.height * scale;
        const float cx = (box.left + box.right) * 0.5f;
        const float cy = (box.top + box.bottom) * 0.5f;
        return D2D1::RectF(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
    }

    void DrawTextF(const std::wstring& text, IDWriteTextFormat* format, D2D1_RECT_F rect,
        ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_CLIP)
    {
        if (text.empty() || !format || !brush)
            return;
        g_ctx->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush, options);
    }

    bool FindAlphaBounds(IWICBitmapSource* source, WICRect& bounds)
    {
        UINT w = 0;
        UINT h = 0;
        if (FAILED(source->GetSize(&w, &h)) || w == 0 || h == 0)
            return false;

        const UINT stride = w * 4;
        std::vector<BYTE> pixels(static_cast<size_t>(stride) * h);
        if (FAILED(source->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data())))
            return false;

        UINT minX = w;
        UINT minY = h;
        UINT maxX = 0;
        UINT maxY = 0;
        bool found = false;

        for (UINT y = 0; y < h; ++y)
        {
            const BYTE* row = pixels.data() + static_cast<size_t>(y) * stride;
            for (UINT x = 0; x < w; ++x)
            {
                const BYTE alpha = row[x * 4 + 3];
                if (alpha <= 8)
                    continue;

                minX = (std::min)(minX, x);
                minY = (std::min)(minY, y);
                maxX = (std::max)(maxX, x);
                maxY = (std::max)(maxY, y);
                found = true;
            }
        }

        if (!found)
            return false;

        const UINT pad = 1;
        minX = (minX > pad) ? (minX - pad) : 0;
        minY = (minY > pad) ? (minY - pad) : 0;
        maxX = (std::min)(w - 1, maxX + pad);
        maxY = (std::min)(h - 1, maxY + pad);

        bounds.X = static_cast<INT>(minX);
        bounds.Y = static_cast<INT>(minY);
        bounds.Width = static_cast<INT>(maxX - minX + 1);
        bounds.Height = static_cast<INT>(maxY - minY + 1);
        return bounds.Width > 0 && bounds.Height > 0;
    }

    float NotificationListTotalHeight()
    {
        float total = 0.0f;
        for (const auto& n : g_animNotifications) {
            const float extent = HeightMorphExtent(n.alpha, n.isRemoving);
            total += NOTIF_ITEM_H * extent + (extent > 0.01f ? NOTIF_ITEM_GAP * extent : 0.0f);
        }
        if (total > 0.0f)
            total -= NOTIF_ITEM_GAP;
        return total;
    }
}

ComPtr<ID2D1Bitmap1> GetNotificationIcon(const NotificationInfo& notification)
{
    const std::wstring key = NotificationIconKey(notification);
    auto it = g_notificationIcons.find(key);
    if (it != g_notificationIcons.end())
        return it->second;

    ComPtr<ID2D1Bitmap1> bmp;
    if (!notification.iconBytes.empty())
    {
        ComPtr<IWICImagingFactory> wic;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        {
            ComPtr<IWICStream> stream;
            if (SUCCEEDED(wic->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(notification.iconBytes.data()), static_cast<DWORD>(notification.iconBytes.size()))))
            {
                ComPtr<IWICBitmapDecoder> dec;
                if (SUCCEEDED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec)))
                {
                    ComPtr<IWICBitmapFrameDecode> frame;
                    if (SUCCEEDED(dec->GetFrame(0, &frame)))
                    {
                        ComPtr<IWICFormatConverter> conv;
                        if (SUCCEEDED(wic->CreateFormatConverter(&conv)))
                        {
                            conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

                            ComPtr<IWICBitmapSource> source = conv;
                            WICRect bounds{};
                            if (FindAlphaBounds(conv.Get(), bounds))
                            {
                                ComPtr<IWICBitmapClipper> clipper;
                                if (SUCCEEDED(wic->CreateBitmapClipper(&clipper)) &&
                                    SUCCEEDED(clipper->Initialize(conv.Get(), &bounds)))
                                {
                                    source = clipper;
                                }
                            }

                            D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                                D2D1_BITMAP_OPTIONS_NONE,
                                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                            g_ctx->CreateBitmapFromWicBitmap(source.Get(), &props, &bmp);
                        }
                    }
                }
            }
        }
    }

    g_notificationIcons[key] = bmp;
    return bmp;
}

void NotificationPanel_UpdateMetrics(float drawerHeight)
{
    g_notifListTotalH = NotificationListTotalHeight();
    if (g_notifListTotalH <= 0.01f)
    {
        g_notifPanelY = drawerHeight;
        g_notifPanelH = 0.0f;
        g_notifListViewH = 0.0f;
        g_notifPanelRect = {};
        g_notifClipRect = {};
        return;
    }

    g_notifListViewH = (std::min)(g_notifListTotalH, NOTIF_MAX_VIEW_H);
    g_notifPanelH = (std::min)(HEADER_H + g_notifListViewH + BOTTOM_PAD, NOTIF_MAX_PANEL_H);
    g_notifPanelY = drawerHeight + NOTIF_PANEL_GAP;
}

void NotificationPanel_Render(float drawerHeight, float opacity)
{
    NotificationPanel_UpdateMetrics(drawerHeight);
    if (opacity <= 0.01f || g_notifPanelH <= 0.01f)
        return;

    const D2D1_SIZE_F sz = g_ctx->GetSize();
    g_notifPanelRect = D2D1::RectF(0.0f, g_notifPanelY, sz.width, g_notifPanelY + g_notifPanelH);

    D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters();
    layerParams.opacity = opacity;
    g_ctx->PushLayer(layerParams, nullptr);

    g_brBg->SetOpacity(1.0f);
    g_ctx->FillRoundedRectangle(
        D2D1::RoundedRect(g_notifPanelRect, 16.0f, 16.0f),
        g_brBg.Get());

    const float headerMidY = g_notifPanelY + HEADER_H * 0.5f;
    g_brGrey->SetOpacity(1.0f);
    DrawTextF(L"Notifications", g_tfNotifHeader.Get(),
        D2D1::RectF(PANEL_PAD + 4.0f, headerMidY - 10.0f, sz.width * 0.5f, headerMidY + 11.0f), g_brGrey.Get());

    g_notifClearRect = D2D1::RectF(sz.width - PANEL_PAD - CLEAR_W, headerMidY - CLEAR_H * 0.5f,
        sz.width - PANEL_PAD, headerMidY + CLEAR_H * 0.5f);

    D2D1_MATRIX_3X2_F clearOld;
    if (g_notifClearClick)
    {
        g_ctx->GetTransform(&clearOld);
        g_ctx->SetTransform(D2D1::Matrix3x2F::Scale(0.97f, 0.97f,
            D2D1::Point2F((g_notifClearRect.left + g_notifClearRect.right) * 0.5f, headerMidY)) * clearOld);
    }

    ComPtr<ID2D1SolidColorBrush> clearBg;
    g_ctx->CreateSolidColorBrush(LerpColor(WithAlpha(CLR_WHITE, 0.055f), WithAlpha(CLR_WHITE, 0.105f), g_notifClearHovT), &clearBg);
    g_ctx->FillRoundedRectangle(D2D1::RoundedRect(g_notifClearRect, CLEAR_H * 0.5f, CLEAR_H * 0.5f), clearBg.Get());
    const D2D1_COLOR_F clearCol = LerpColor(CLR_GREY, CLR_WHITE, g_notifClearHovT);
    DrawSvgColored(g_svgTrash.Get(), g_notifClearRect.left + 19.0f, headerMidY, 12.0f, clearCol);
    ComPtr<ID2D1SolidColorBrush> clearTextBr;
    g_ctx->CreateSolidColorBrush(clearCol, &clearTextBr);
    DrawTextF(L"Clear all", g_tfNotifButton.Get(),
        D2D1::RectF(g_notifClearRect.left + 30.0f, g_notifClearRect.top, g_notifClearRect.right - 6.0f, g_notifClearRect.bottom),
        clearTextBr.Get());

    if (g_notifClearClick)
        g_ctx->SetTransform(clearOld);

    float maxScroll = (std::max)(0.0f, g_notifListTotalH - g_notifListViewH);
    g_notifScrollTargetY = (std::clamp)(g_notifScrollTargetY, 0.0f, maxScroll);
    g_notifScrollY = (std::clamp)(g_notifScrollY, 0.0f, maxScroll);

    const float listTop = g_notifPanelY + HEADER_H;
    g_notifClipRect = D2D1::RectF(0.0f, listTop, sz.width, listTop + g_notifListViewH);
    g_ctx->PushAxisAlignedClip(g_notifClipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    D2D1_MATRIX_3X2_F oldTr;
    g_ctx->GetTransform(&oldTr);
    g_ctx->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -g_notifScrollY) * oldTr);

    g_notifRowRects.resize(g_animNotifications.size());
    g_notifCloseRects.resize(g_animNotifications.size());
    g_notifRowHovT.resize(g_animNotifications.size(), 0.0f);
    g_notifCloseHovT.resize(g_animNotifications.size(), 0.0f);

    float y = listTop;
    for (size_t i = 0; i < g_animNotifications.size(); ++i)
    {
        const auto& anim = g_animNotifications[i];
        const auto& n = anim.data;
        const float extent = HeightMorphExtent(anim.alpha, anim.isRemoving);
        const float itemH = NOTIF_ITEM_H * extent;
        const float gapH = (extent > 0.01f && i + 1 < g_animNotifications.size()) ? NOTIF_ITEM_GAP * extent : 0.0f;

        g_notifRowRects[i] = D2D1::RectF(PANEL_PAD, y - g_notifScrollY, sz.width - PANEL_PAD, y + itemH - g_notifScrollY);

        if (anim.alpha < 0.99f)
        {
            g_ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::Matrix3x2F::Scale(anim.scale, anim.scale, D2D1::Point2F(sz.width * 0.5f, y + itemH * 0.5f)),
                anim.alpha), nullptr);
        }

        g_ctx->PushAxisAlignedClip(D2D1::RectF(PANEL_PAD, y, sz.width - PANEL_PAD, y + itemH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        const D2D1_RECT_F rowRect = D2D1::RectF(PANEL_PAD, y, sz.width - PANEL_PAD, y + NOTIF_ITEM_H);
        const D2D1_RECT_F contentRect = D2D1::RectF(rowRect.left + ROW_PAD_X, rowRect.top + ROW_PAD_Y,
            rowRect.right - ROW_PAD_X, rowRect.bottom - ROW_PAD_Y);
        const float rowMidY = y + NOTIF_ITEM_H * 0.5f;

        D2D1_MATRIX_3X2_F itemOld;
        if (g_notifClickIdx == static_cast<int>(i))
        {
            g_ctx->GetTransform(&itemOld);
            g_ctx->SetTransform(D2D1::Matrix3x2F::Scale(0.985f, 0.985f, D2D1::Point2F(sz.width * 0.5f, rowMidY)) * itemOld);
        }

        if (g_notifRowHovT[i] > 0.0f)
        {
            ComPtr<ID2D1SolidColorBrush> rowBg;
            g_ctx->CreateSolidColorBrush(LerpColor(WithAlpha(CLR_WHITE, 0.0f), WithAlpha(CLR_WHITE, 0.055f), g_notifRowHovT[i]), &rowBg);
            g_ctx->FillRoundedRectangle(D2D1::RoundedRect(rowRect, 8.0f, 8.0f), rowBg.Get());
        }

        const float iconCX = contentRect.left + ICON_R;
        const float iconCY = contentRect.top + 32.0f;
        ComPtr<ID2D1Bitmap1> icon = GetNotificationIcon(n);
        if (icon)
        {
            const D2D1_RECT_F iconBox = D2D1::RectF(iconCX - ICON_R, iconCY - ICON_R, iconCX + ICON_R, iconCY + ICON_R);
            g_ctx->DrawBitmap(icon.Get(), FitRect(iconBox, icon->GetSize()), 1.0f,
                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        }
        else
        {
            ComPtr<ID2D1SolidColorBrush> iconBg;
            g_ctx->CreateSolidColorBrush(g_clrPillIco, &iconBg);
            g_ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(iconCX - ICON_R, iconCY - ICON_R, iconCX + ICON_R, iconCY + ICON_R), 7.0f, 7.0f), iconBg.Get());
            DrawSvgColored(g_svgUserRound.Get(), iconCX, iconCY, 18.0f, g_clrFill);
        }

        const float textX = iconCX + ICON_R + 14.0f;
        const float timeW = 62.0f;
        const float closeCX = contentRect.right - CLOSE_R + 2.0f;
        const float closeCY = contentRect.top + 11.0f;
        g_notifCloseRects[i] = D2D1::RectF(closeCX - CLOSE_R, closeCY - CLOSE_R - g_notifScrollY,
            closeCX + CLOSE_R, closeCY + CLOSE_R - g_notifScrollY);

        const float timeRight = closeCX - CLOSE_R - 8.0f;
        const float timeLeft = timeRight - timeW;
        const float textRight = timeLeft - 10.0f;

        ComPtr<ID2D1SolidColorBrush> appBr;
        g_ctx->CreateSolidColorBrush(g_clrFill, &appBr);
        DrawTextF(n.appName, g_tfNotifApp.Get(), D2D1::RectF(textX, contentRect.top - 1.0f, textRight, contentRect.top + 16.0f), appBr.Get());

        g_brGrey->SetOpacity(0.58f);
        DrawTextF(n.timeText, g_tfNotifButton.Get(), D2D1::RectF(timeLeft, contentRect.top - 1.0f, timeRight, contentRect.top + 16.0f), g_brGrey.Get());

        g_brWhite->SetOpacity(0.92f);
        DrawTextF(n.title, g_tfNotifTitle.Get(), D2D1::RectF(textX, contentRect.top + 24.0f, contentRect.right - 22.0f, contentRect.top + 43.0f), g_brWhite.Get());

        g_brGrey->SetOpacity(0.7f);
        DrawTextF(n.subtext, g_tfNotifBody.Get(), D2D1::RectF(textX, contentRect.top + 48.0f, contentRect.right - 22.0f, contentRect.bottom + 1.0f), g_brGrey.Get());

        const float closeAlpha = (std::max)(g_notifRowHovT[i], g_notifCloseHovT[i]);
        if (closeAlpha > 0.01f)
        {
            if (g_notifCloseHovT[i] > 0.0f)
            {
                ComPtr<ID2D1SolidColorBrush> closeBg;
                g_ctx->CreateSolidColorBrush(LerpColor(WithAlpha(CLR_WHITE, 0.04f), WithAlpha(CLR_WHITE, 0.12f), g_notifCloseHovT[i]), &closeBg);
                g_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(closeCX, closeCY), CLOSE_R, CLOSE_R), closeBg.Get());
            }

            D2D1_COLOR_F closeColor = LerpColor(CLR_GREY, CLR_WHITE, g_notifCloseHovT[i]);
            closeColor.a *= closeAlpha;
            DrawSvgColored(g_svgX.Get(), closeCX, closeCY, 12.0f, closeColor);
        }

        if (g_notifClickIdx == static_cast<int>(i))
            g_ctx->SetTransform(itemOld);

        g_ctx->PopAxisAlignedClip();
        if (anim.alpha < 0.99f)
            g_ctx->PopLayer();

        y += itemH + gapH;
    }

    g_ctx->SetTransform(oldTr);
    g_ctx->PopAxisAlignedClip();

    if (maxScroll > 0.0f)
    {
        float viewRatio = g_notifListViewH / g_notifListTotalH;
        float sbH = (std::max)(16.0f, g_notifListViewH * viewRatio);
        float sbY = listTop + (g_notifScrollY / maxScroll) * (g_notifListViewH - sbH);
        ComPtr<ID2D1SolidColorBrush> sbBr;
        g_ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.24f), &sbBr);
        g_ctx->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(sz.width - SCROLLBAR_W - 4.0f, sbY, sz.width - 4.0f, sbY + sbH),
            SCROLLBAR_W * 0.5f, SCROLLBAR_W * 0.5f), sbBr.Get());
    }

    g_ctx->PopLayer();
}

bool NotificationPanel_HandleWheel(HWND hWnd, D2D1_POINT_2F p, float delta)
{
    if (g_notifPanelH <= 0.01f || !PtInRectF(g_notifClipRect, p))
        return false;

    g_notifScrollTargetY -= delta * 60.0f;
    SetTimer(hWnd, SCROLL_TIMER, 16, nullptr);
    return true;
}

bool NotificationPanel_HandleClick(HWND hWnd, D2D1_POINT_2F p)
{
    if (g_notifPanelH <= 0.01f || !PtInRectF(g_notifPanelRect, p))
        return false;

    if (PtInRectF(g_notifClearRect, p))
    {
        g_notifClearClick = true;
        SetTimer(hWnd, HOVER_TIMER, 16, nullptr);
        NotificationStatus_ClearAll();
        Render();
        return true;
    }

    for (size_t i = 0; i < g_animNotifications.size(); ++i)
    {
        if (i < g_notifCloseRects.size() && PtInRectF(g_notifCloseRects[i], p) && PtInRectF(g_notifClipRect, p))
        {
            NotificationStatus_Remove(g_animNotifications[i].data.id);
            return true;
        }
    }

    for (size_t i = 0; i < g_animNotifications.size(); ++i)
    {
        if (i < g_notifRowRects.size() && PtInRectF(g_notifRowRects[i], p) && PtInRectF(g_notifClipRect, p))
        {
            g_notifClickIdx = static_cast<int>(i);
            SetTimer(hWnd, HOVER_TIMER, 16, nullptr);

            const NotificationInfo notification = g_animNotifications[i].data;
            Drawer_Toggle(g_snap);
            PostMessageW(g_navbarHwnd, WM_APP_DRAWER_CLOSED, 0, 0);

            if (NotificationStatus_Activate(notification))
                NotificationStatus_Remove(notification.id);

            return true;
        }
    }

    return true;
}

void NotificationPanel_UpdateHover(D2D1_POINT_2F p)
{
    int newRow = -1;
    int newClose = -1;
    bool newClear = false;

    if (g_notifPanelH > 0.01f)
    {
        newClear = PtInRectF(g_notifClearRect, p);
        if (PtInRectF(g_notifClipRect, p))
        {
            for (size_t i = 0; i < g_notifRowRects.size(); ++i)
            {
                if (PtInRectF(g_notifRowRects[i], p))
                {
                    newRow = static_cast<int>(i);
                    break;
                }
            }

            for (size_t i = 0; i < g_notifCloseRects.size(); ++i)
            {
                if (PtInRectF(g_notifCloseRects[i], p))
                {
                    newClose = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    if (newRow != g_notifRowHovIdx || newClose != g_notifCloseHovIdx || newClear != g_notifClearHov)
    {
        g_notifRowHovIdx = newRow;
        g_notifCloseHovIdx = newClose;
        g_notifClearHov = newClear;
        SetTimer(g_drawerHwnd, HOVER_TIMER, 16, nullptr);
    }
}

void NotificationPanel_ResetHover()
{
    g_notifRowHovIdx = -1;
    g_notifCloseHovIdx = -1;
    g_notifClearHov = false;
    SetTimer(g_drawerHwnd, HOVER_TIMER, 16, nullptr);
}
