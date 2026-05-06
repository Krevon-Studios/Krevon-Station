#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_3.h>
#include <cmath>
#include "status/status_types.h"

enum class DrawerPanel { Main, Power, Sound, Wifi, Bluetooth };

// Layout constants (DIPs)
inline constexpr float D_W           = 358.0f;
inline constexpr float D_PAD         = 12.0f;
inline constexpr float ROW1_H        = 46.0f;
inline constexpr float ROW2_H        = 48.0f;
inline constexpr float ROW3_H        = 40.0f;
inline constexpr float GAP_SEP_TOP   = 10.0f;
inline constexpr float GAP_SEP_BOT   = 10.0f;
inline constexpr float GAP_R2_R3     = 8.0f;
inline constexpr float D_H_MAIN      = D_PAD + ROW1_H + GAP_SEP_TOP + 1.0f + GAP_SEP_BOT + ROW2_H + GAP_R2_R3 + ROW3_H + D_PAD;

inline constexpr float PWR_ITEM_H    = 46.0f;
inline constexpr float D_H_POWER     = D_PAD + ROW1_H + GAP_SEP_TOP + 1.0f + GAP_SEP_BOT + (PWR_ITEM_H * 3.0f) + D_PAD;

inline constexpr float SND_ITEM_H    = 46.0f;
inline constexpr float SND_MAX_H     = 480.0f;
inline constexpr float SCROLLBAR_W   = 2.0f;
inline constexpr float NOTIF_PANEL_GAP = 8.0f;
inline constexpr float NOTIF_ITEM_H    = 88.0f;
inline constexpr float NOTIF_ITEM_GAP  = 8.0f;
inline constexpr float NOTIF_MAX_VIEW_H = NOTIF_ITEM_H * 3.0f + NOTIF_ITEM_GAP * 2.0f;
inline constexpr float NOTIF_MAX_PANEL_H = 360.0f;
inline constexpr float DRAWER_WINDOW_MAX_H = 920.0f;

inline constexpr float AVATAR_R      = 20.0f;
inline constexpr float BTN_R         = 20.0f;
inline constexpr float BTN_ICON_SZ   = 16.0f;
inline constexpr float PILL_H        = ROW2_H;
inline constexpr float PILL_R        = 14.0f;
inline constexpr float VOL_ICON_SZ   = 16.0f;
inline constexpr float SLIDER_H      = 6.0f;
inline constexpr float THUMB_R       = 7.0f;
inline constexpr float ANIM_IN_MS    = 180.0f;
inline constexpr float ANIM_OUT_MS   = 140.0f;
inline constexpr UINT_PTR ANIM_TIMER       = 10;
inline constexpr UINT_PTR HOVER_TIMER      = 11;
inline constexpr UINT_PTR SCREENSHOT_TIMER = 12;
inline constexpr UINT_PTR PANEL_TIMER      = 13;
inline constexpr UINT_PTR SND_LIST_TIMER   = 14;
inline constexpr UINT_PTR SCROLL_TIMER     = 15;
inline constexpr float HOV_DUR_MS          = 120.0f;
inline constexpr float HEIGHT_MORPH_MIN_MS     = 150.0f;
inline constexpr float HEIGHT_MORPH_BASE_MS    = 120.0f;
inline constexpr float HEIGHT_MORPH_PER_DIP_MS = 0.35f;
inline constexpr float HEIGHT_MORPH_MAX_MS     = 240.0f;
inline constexpr float HEIGHT_MORPH_LIST_MS    = 190.0f;

enum { BTN_CAM=0, BTN_SET=1, BTN_LOCK=2, BTN_PWR=3, BTN_COUNT=4 };
enum { PILL_WIFI=0, PILL_BT=1, PILL_COUNT=2 };
enum { PWR_ITEM_SLEEP=0, PWR_ITEM_RESTART=1, PWR_ITEM_SHUTDOWN=2, PWR_ITEM_COUNT=3 };

inline constexpr D2D1_COLOR_F CLR_BG        = { 0.0f,  0.0f,  0.0f,  1.0f  };
inline constexpr D2D1_COLOR_F CLR_BTN       = { 1.0f,  1.0f,  1.0f,  0.08f };
inline constexpr D2D1_COLOR_F CLR_BTN_HOV   = { 1.0f,  1.0f,  1.0f,  0.14f };
inline constexpr D2D1_COLOR_F CLR_WHITE     = { 1.0f,  1.0f,  1.0f,  1.0f  };
inline constexpr D2D1_COLOR_F CLR_GREY      = { 0.6f,  0.6f,  0.6f,  1.0f  };
inline constexpr D2D1_COLOR_F CLR_TRACK     = { 1.0f,  1.0f,  1.0f,  0.15f };
inline constexpr D2D1_COLOR_F CLR_THUMB     = { 1.0f,  1.0f,  1.0f,  1.0f  };
inline constexpr D2D1_COLOR_F CLR_SEP       = { 1.0f,  1.0f,  1.0f,  0.06f };

struct SoundPanelMetrics
{
    float devicesTotalH = 0.0f;
    float appsTotalH = 0.0f;
    float devicesViewH = 0.0f;
    float appsViewH = 0.0f;
    float targetH = D_H_MAIN;
};

extern float g_dpiScale;

inline float EaseOutCubic(float t)
{
    float inv = 1.0f - t; return 1.0f - inv * inv * inv;
}
inline float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
inline float HeightMorphDurationMs(float fromHeight, float toHeight)
{
    const float dur = HEIGHT_MORPH_BASE_MS + fabsf(toHeight - fromHeight) * HEIGHT_MORPH_PER_DIP_MS;
    return dur < HEIGHT_MORPH_MIN_MS ? HEIGHT_MORPH_MIN_MS : (dur > HEIGHT_MORPH_MAX_MS ? HEIGHT_MORPH_MAX_MS : dur);
}
inline float HeightMorphExtent(float alpha, bool isRemoving)
{
    const float t = Clamp01(alpha);
    return isRemoving ? 1.0f - EaseOutCubic(1.0f - t) : EaseOutCubic(t);
}
inline D2D1_COLOR_F LerpColor(D2D1_COLOR_F a, D2D1_COLOR_F b, float t)
{
    return D2D1::ColorF(
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t);
}
inline D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a)
{
    c.a = a;
    return c;
}
inline D2D1_POINT_2F ToDip(LONG xPx, LONG yPx)
{
    return D2D1::Point2F(xPx / g_dpiScale, yPx / g_dpiScale);
}
inline bool PtInRectF(const D2D1_RECT_F& r, D2D1_POINT_2F p)
{
    return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
}
inline bool PtInEll(const D2D1_ELLIPSE& e, D2D1_POINT_2F p)
{
    float dx = (p.x - e.point.x) / e.radiusX;
    float dy = (p.y - e.point.y) / e.radiusY;
    return (dx*dx + dy*dy) <= 1.0f;
}
