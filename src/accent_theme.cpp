#include "accent_theme.h"

#include <mutex>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
    std::mutex s_paletteMutex;
    HWND s_notifyHwnd = nullptr;
    winrt::Windows::UI::ViewManagement::UISettings s_uiSettings{ nullptr };
    winrt::event_token s_colorToken{};
    bool s_hasColorToken = false;

    D2D1_COLOR_F LerpColor(D2D1_COLOR_F a, D2D1_COLOR_F b, float t)
    {
        return D2D1::ColorF(
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t);
    }

    AccentThemePalette BuildPalette(D2D1_COLOR_F accent)
    {
        const D2D1_COLOR_F black = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
        const D2D1_COLOR_F white = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);

        AccentThemePalette palette = {};
        palette.accent = accent;
        palette.fill = LerpColor(accent, white, 0.35f);
        palette.pill = LerpColor(black, accent, 0.15f);
        palette.pillHover = LerpColor(black, accent, 0.22f);
        palette.pillIcon = LerpColor(black, accent, 0.38f);
        return palette;
    }

    AccentThemePalette s_palette = BuildPalette(D2D1::ColorF(0.0f, 0.47f, 0.83f, 1.0f));

    void SetPalette(AccentThemePalette palette)
    {
        std::lock_guard<std::mutex> lock(s_paletteMutex);
        s_palette = palette;
    }

    D2D1_COLOR_F ToD2DColor(winrt::Windows::UI::Color color)
    {
        return D2D1::ColorF(
            color.R / 255.0f,
            color.G / 255.0f,
            color.B / 255.0f,
            color.A / 255.0f);
    }
}

void AccentTheme_Init(HWND notifyHwnd)
{
    s_notifyHwnd = notifyHwnd;

    try
    {
        s_uiSettings = winrt::Windows::UI::ViewManagement::UISettings();
        auto color = s_uiSettings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
        SetPalette(BuildPalette(ToD2DColor(color)));

        s_colorToken = s_uiSettings.ColorValuesChanged(
            [](const winrt::Windows::UI::ViewManagement::UISettings& sender,
               const winrt::Windows::Foundation::IInspectable&) {
                auto color = sender.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
                SetPalette(BuildPalette(ToD2DColor(color)));
                if (s_notifyHwnd)
                    PostMessageW(s_notifyHwnd, WM_APP_ACCENT_CHANGED, 0, 0);
            });
        s_hasColorToken = true;
    }
    catch (...)
    {
        SetPalette(BuildPalette(D2D1::ColorF(0.0f, 0.47f, 0.83f, 1.0f)));
    }
}

void AccentTheme_Shutdown()
{
    try
    {
        if (s_uiSettings && s_hasColorToken)
            s_uiSettings.ColorValuesChanged(s_colorToken);
    }
    catch (...)
    {
    }

    s_hasColorToken = false;
    s_uiSettings = nullptr;
    s_notifyHwnd = nullptr;
}

AccentThemePalette AccentTheme_GetPalette()
{
    std::lock_guard<std::mutex> lock(s_paletteMutex);
    return s_palette;
}
