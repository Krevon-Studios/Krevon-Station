#include "renderer.h"
#include "accent_theme.h"
#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <objbase.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <ctime>

using Microsoft::WRL::ComPtr;

// ── Device objects ────────────────────────────────────────────────────────────
static ComPtr<ID3D11Device>        s_d3dDevice;
static ComPtr<IDXGISwapChain1>     s_swapChain;
static ComPtr<ID2D1Factory1>       s_d2dFactory;
static ComPtr<ID2D1Device>         s_d2dDevice;
static ComPtr<ID2D1DeviceContext5> s_ctx;
static ComPtr<ID2D1Bitmap1>        s_targetBitmap;

// ── Icon SVG documents ────────────────────────────────────────────────────────
static ComPtr<ID2D1SvgDocument> s_svgBluetooth;
static ComPtr<ID2D1SvgDocument> s_svgBluetoothConnected;
static ComPtr<ID2D1SvgDocument> s_svgBluetoothOff;
static ComPtr<ID2D1SvgDocument> s_svgWifi;
static ComPtr<ID2D1SvgDocument> s_svgWifiHigh;
static ComPtr<ID2D1SvgDocument> s_svgWifiLow;
static ComPtr<ID2D1SvgDocument> s_svgWifiZero;
static ComPtr<ID2D1SvgDocument> s_svgWifiDisconnected;
static ComPtr<ID2D1SvgDocument> s_svgVolume;
static ComPtr<ID2D1SvgDocument> s_svgVolumeMedium;
static ComPtr<ID2D1SvgDocument> s_svgVolumeHigh;
static ComPtr<ID2D1SvgDocument> s_svgVolumeOff;
static ComPtr<ID2D1SvgDocument> s_svgVolumeMuted;

// ── Brushes ───────────────────────────────────────────────────────────────────
static ComPtr<ID2D1SolidColorBrush> s_statusHoverBrush;  // hover / drawer-open pill
static ComPtr<ID2D1SolidColorBrush> s_desktopBrush;
static ComPtr<ID2D1SolidColorBrush> s_textBrush;
static ComPtr<ID2D1SolidColorBrush> s_visualizerBrush;
static ComPtr<IDWriteFactory>       s_dwriteFactory;
static ComPtr<IDWriteTextFormat>    s_islandTextFormat;
static ComPtr<IDWriteInlineObject>  s_islandEllipsis;

// ── State ─────────────────────────────────────────────────────────────────────
static bool      s_hovered        = false;
static bool      s_drawerOpen     = false;   // pill stays lit while drawer is open
static float     s_dpiScale       = 1.0f;   // physical pixels per DIP (DPI / 96)
static float     s_hoverProgress  = 0.0f;   // current display value [0,1], easing already applied
static float     s_animFrom       = 0.0f;   // progress value at animation start
static float     s_animTarget     = 0.0f;   // progress target (0 or 1)
static ULONGLONG s_animStartMs    = 0;
static float     s_animDurationMs = 0.0f;
static StatusSnapshot s_statusSnapshot = {};
static VirtualDesktopSnapshot s_desktopSnapshot = {};
static constexpr int DESKTOP_MAX_INDICATORS = 64;
static float     s_desktopT[DESKTOP_MAX_INDICATORS] = {};
static float     s_desktopAnimFrom[DESKTOP_MAX_INDICATORS] = {};
static ULONGLONG s_desktopAnimStartMs = 0;
static float     s_desktopAnimDurationMs = 0.0f;
static D2D1_RECT_F s_mediaIslandRect = {};
static float     s_visualizerPhase = 0.0f;
static bool      s_compactTextInitialized = false;
static bool      s_compactHasPlayingMedia = false;
static bool      s_compactPrevHasPlayingMedia = false;
static std::wstring s_compactText;
static std::wstring s_compactPrevText;
static ULONGLONG s_compactTextAnimStartMs = 0;
static float     s_compactTextAnimDurationMs = 0.0f;
static float     s_compactTextAnimT = 1.0f;

// ── Layout constants (DIPs) ───────────────────────────────────────────────────
static constexpr float ICON_SIZE          = 15.0f;
static constexpr float ICON_CELL_W        = 24.0f;
static constexpr float STATUS_RIGHT_PAD   = 10.0f;
static constexpr float STATUS_PAD_H       =  8.0f;
static constexpr float HOVER_PILL_INSET_H    =  3.0f;
static constexpr float HOVER_PILL_INSET_V    =  3.0f;
static constexpr float PILL_HOVER_ALPHA      =  0.14f; // pill opacity at full hover
static constexpr float HOVER_ANIM_IN_MS      = 150.0f; // hover enter
static constexpr float HOVER_ANIM_OUT_MS     = 220.0f; // hover leave
static constexpr float DESKTOP_LEFT_PAD      = 14.0f;
static constexpr float DESKTOP_DOT_SIZE      =  7.0f;
static constexpr float DESKTOP_ACTIVE_W      = 20.0f;
static constexpr float DESKTOP_GAP           =  7.0f;
static constexpr float DESKTOP_HIT_PAD       =  5.0f;
static constexpr float DESKTOP_MORPH_MS      = 180.0f;
static constexpr float ISLAND_TIME_W         = 214.0f;
static constexpr float ISLAND_MEDIA_W        = 312.0f;
static constexpr float ISLAND_HIT_PAD_H      = 12.0f;
static constexpr float VIS_BAR_W             = 3.0f;
static constexpr float VIS_BAR_GAP           = 3.0f;
static constexpr float COMPACT_TEXT_FADE_MS  = 180.0f;
static constexpr UINT  ANIM_TIMER_FAST_MS    = 16;
static constexpr UINT  COMPACT_VIS_TIMER_MS  = 50;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void IconPath(const wchar_t* filename, wchar_t* out, DWORD outLen)
{
    GetModuleFileNameW(nullptr, out, outLen);
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcsncat_s(out, outLen, L"assets\\icons\\", _TRUNCATE);
    wcsncat_s(out, outLen, filename, _TRUNCATE);
}

static HRESULT LoadSvg(const wchar_t* filename, ComPtr<ID2D1SvgDocument>& out)
{
    wchar_t path[MAX_PATH];
    IconPath(filename, path, MAX_PATH);

    IStream* rawStream = nullptr;
    HRESULT hr = SHCreateStreamOnFileW(path, STGM_READ | STGM_SHARE_DENY_WRITE, &rawStream);
    if (FAILED(hr)) return hr;
    ComPtr<IStream> stream(rawStream);

    hr = s_ctx->CreateSvgDocument(stream.Get(),
        D2D1::SizeF(ICON_SIZE, ICON_SIZE), &out);
    if (FAILED(hr)) return hr;

    ComPtr<ID2D1SvgElement> root;
    out->GetRoot(&root);
    out->SetViewportSize(D2D1::SizeF(ICON_SIZE, ICON_SIZE));
    root->SetAttributeValue(L"width",  ICON_SIZE);
    root->SetAttributeValue(L"height", ICON_SIZE);
    root->SetAttributeValue(L"color",  D2D1_SVG_ATTRIBUTE_STRING_TYPE_SVG, L"white");
    root->SetAttributeValue(L"stroke", D2D1_SVG_ATTRIBUTE_STRING_TYPE_SVG, L"white");
    return S_OK;
}

static HRESULT RebuildTarget()
{
    s_ctx->SetTarget(nullptr);
    s_targetBitmap.Reset();

    ComPtr<IDXGISurface> backBuffer;
    HRESULT hr = s_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return hr;

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

    hr = s_ctx->CreateBitmapFromDxgiSurface(backBuffer.Get(), &props, &s_targetBitmap);
    if (FAILED(hr)) return hr;

    s_ctx->SetTarget(s_targetBitmap.Get());
    return S_OK;
}

// Returns the status section rect in DIPs given the current render target size.
static D2D1_RECT_F GetStatusRect(D2D1_SIZE_F size)
{
    const float iconsW  = ICON_CELL_W * 3.0f;
    const float startX  = size.width - STATUS_RIGHT_PAD - iconsW;

    return D2D1::RectF(
        startX - STATUS_PAD_H,
        0.0f,
        startX + iconsW + STATUS_PAD_H,
        size.height);
}

static D2D1_ROUNDED_RECT GetHoverPillRect(D2D1_SIZE_F size)
{
    const D2D1_RECT_F statusRect = GetStatusRect(size);
    const D2D1_RECT_F pillRect = D2D1::RectF(
        statusRect.left + HOVER_PILL_INSET_H,
        statusRect.top + HOVER_PILL_INSET_V,
        statusRect.right - HOVER_PILL_INSET_H,
        statusRect.bottom - HOVER_PILL_INSET_V);
    const float radius = (pillRect.bottom - pillRect.top) * 0.5f;
    return D2D1::RoundedRect(pillRect, radius, radius);
}

static int GetDesktopRenderCount()
{
    if (!s_desktopSnapshot.available || s_desktopSnapshot.count <= 0)
        return 0;
    return min(s_desktopSnapshot.count, DESKTOP_MAX_INDICATORS);
}

static float GetDesktopTargetT(int index)
{
    return index == s_desktopSnapshot.currentIndex ? 1.0f : 0.0f;
}

static float GetDesktopIndicatorWidth(int index)
{
    return DESKTOP_DOT_SIZE + (DESKTOP_ACTIVE_W - DESKTOP_DOT_SIZE) * s_desktopT[index];
}

static D2D1_RECT_F GetDesktopIndicatorRect(D2D1_SIZE_F size, int index)
{
    float x = DESKTOP_LEFT_PAD;
    for (int i = 0; i < index; ++i)
        x += GetDesktopIndicatorWidth(i) + DESKTOP_GAP;

    const float width = GetDesktopIndicatorWidth(index);
    const float y = (size.height - DESKTOP_DOT_SIZE) * 0.5f;
    return D2D1::RectF(x, y, x + width, y + DESKTOP_DOT_SIZE);
}

static float EaseOutCubic(float t)
{
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static float Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static std::wstring FormatIslandTime()
{
    static const wchar_t* DAYS[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
    static const wchar_t* MONTHS[] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };

    std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);

    int hour = local.tm_hour % 12;
    if (hour == 0) hour = 12;

    wchar_t buf[64] = {};
    swprintf_s(buf, L"%s, %s %d  %d:%02d %s",
        DAYS[local.tm_wday],
        MONTHS[local.tm_mon],
        local.tm_mday,
        hour,
        local.tm_min,
        local.tm_hour >= 12 ? L"PM" : L"AM");
    return buf;
}

static int GetCompactPlayingMediaIndex(const StatusSnapshot& snapshot)
{
    for (size_t i = 0; i < snapshot.mediaSessions.size(); ++i)
    {
        if (snapshot.mediaSessions[i].playbackState == MediaPlaybackState::Playing)
            return static_cast<int>(i);
    }
    return -1;
}

static int GetCompactPlayingMediaIndex()
{
    return GetCompactPlayingMediaIndex(s_statusSnapshot);
}

static std::wstring CompactIslandText(const StatusSnapshot& snapshot)
{
    const int playingIndex = GetCompactPlayingMediaIndex(snapshot);
    if (playingIndex >= 0)
    {
        const auto& media = snapshot.mediaSessions[playingIndex];
        return media.title.empty() ? media.appName : media.title;
    }
    return FormatIslandTime();
}

static std::wstring CompactIslandText()
{
    return CompactIslandText(s_statusSnapshot);
}

static float MeasureIslandTextWidth(const std::wstring& text, float maxWidth)
{
    if (!s_dwriteFactory || !s_islandTextFormat || text.empty())
        return 0.0f;

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(s_dwriteFactory->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        s_islandTextFormat.Get(),
        maxWidth,
        24.0f,
        &layout)))
    {
        return 0.0f;
    }

    DWRITE_TEXT_METRICS metrics = {};
    if (FAILED(layout->GetMetrics(&metrics)))
        return 0.0f;
    return metrics.widthIncludingTrailingWhitespace;
}

static D2D1_RECT_F GetMediaIslandRect(D2D1_SIZE_F size)
{
    const bool hasPlayingMedia = GetCompactPlayingMediaIndex() >= 0;
    const float width = hasPlayingMedia ? ISLAND_MEDIA_W : ISLAND_TIME_W;
    const float left = (size.width - width) * 0.5f;
    return D2D1::RectF(
        left - ISLAND_HIT_PAD_H,
        0.0f,
        left + width + ISLAND_HIT_PAD_H,
        size.height);
}

static ID2D1SvgDocument* GetBluetoothSvg(BluetoothIconState state)
{
    switch (state)
    {
    case BluetoothIconState::Connected: return s_svgBluetoothConnected.Get();
    case BluetoothIconState::On:        return s_svgBluetooth.Get();
    case BluetoothIconState::Off:       return s_svgBluetoothOff.Get();
    default:                            return s_svgBluetoothOff.Get();
    }
}

static ID2D1SvgDocument* GetWifiSvg(WifiIconState state)
{
    switch (state)
    {
    case WifiIconState::Full:         return s_svgWifi.Get();
    case WifiIconState::High:         return s_svgWifiHigh.Get();
    case WifiIconState::Low:          return s_svgWifiLow.Get();
    case WifiIconState::Zero:         return s_svgWifiZero.Get();
    case WifiIconState::Disconnected: return s_svgWifiDisconnected.Get();
    default:                          return s_svgWifiDisconnected.Get();
    }
}

static ID2D1SvgDocument* GetVolumeSvg(VolumeIconState state)
{
    switch (state)
    {
    case VolumeIconState::Muted: return s_svgVolumeMuted.Get();
    case VolumeIconState::Off:   return s_svgVolumeOff.Get();
    case VolumeIconState::Low:   return s_svgVolume.Get();
    case VolumeIconState::Medium:return s_svgVolumeMedium.Get();
    case VolumeIconState::High:  return s_svgVolumeHigh.Get();
    default:                     return s_svgVolumeOff.Get();
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

HRESULT Renderer_Init(HWND hWnd)
{
    HRESULT hr;

    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
        D3D11_SDK_VERSION, &s_d3dDevice, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
            D3D11_SDK_VERSION, &s_d3dDevice, nullptr, nullptr);
    if (FAILED(hr)) return hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        reinterpret_cast<void**>(s_d2dFactory.GetAddressOf()));
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIDevice> dxgiDevice;
    s_d3dDevice.As(&dxgiDevice);
    hr = s_d2dFactory->CreateDevice(dxgiDevice.Get(), &s_d2dDevice);
    if (FAILED(hr)) return hr;

    ComPtr<ID2D1DeviceContext> baseCtx;
    hr = s_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &baseCtx);
    if (FAILED(hr)) return hr;
    hr = baseCtx.As(&s_ctx);
    if (FAILED(hr)) return hr;

    UINT dpi  = GetDpiForWindow(hWnd);
    s_dpiScale = static_cast<float>(dpi) / 96.0f;
    s_ctx->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    s_ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    ComPtr<IDXGIAdapter>  adapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    dxgiDevice->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));

    RECT rc;
    GetClientRect(hWnd, &rc);
    UINT w = static_cast<UINT>(max(rc.right  - rc.left, 1));
    UINT h = static_cast<UINT>(max(rc.bottom - rc.top,  1));

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width             = w;
    scd.Height            = h;
    scd.Format            = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count  = 1;
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount       = 2;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode         = DXGI_ALPHA_MODE_IGNORE;

    hr = dxgiFactory->CreateSwapChainForHwnd(
        s_d3dDevice.Get(), hWnd, &scd, nullptr, nullptr, &s_swapChain);
    if (FAILED(hr)) return hr;

    hr = RebuildTarget();
    if (FAILED(hr)) return hr;

    // Single brush — used for both hover highlight and drawer-open state
    hr = s_ctx->CreateSolidColorBrush(
        D2D1::ColorF(0.88f, 0.88f, 0.88f, 1.0f), &s_statusHoverBrush);
    if (FAILED(hr)) return hr;

    hr = s_ctx->CreateSolidColorBrush(
        D2D1::ColorF(0.54f, 0.54f, 0.54f, 1.0f), &s_desktopBrush);
    if (FAILED(hr)) return hr;

    hr = s_ctx->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f), &s_textBrush);
    if (FAILED(hr)) return hr;

    hr = s_ctx->CreateSolidColorBrush(AccentTheme_GetPalette().fill, &s_visualizerBrush);
    if (FAILED(hr)) return hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(s_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return hr;

    hr = s_dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_DEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"", &s_islandTextFormat);
    if (FAILED(hr)) return hr;
    s_islandTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    s_islandTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    s_islandTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trimming = {};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    s_dwriteFactory->CreateEllipsisTrimmingSign(s_islandTextFormat.Get(), &s_islandEllipsis);
    s_islandTextFormat->SetTrimming(&trimming, s_islandEllipsis.Get());

    LoadSvg(L"bluetooth.svg",           s_svgBluetooth);
    LoadSvg(L"bluetooth-connected.svg", s_svgBluetoothConnected);
    LoadSvg(L"bluetooth-off.svg",       s_svgBluetoothOff);
    LoadSvg(L"wifi.svg",                s_svgWifi);
    LoadSvg(L"wifi-high.svg",           s_svgWifiHigh);
    LoadSvg(L"wifi-low.svg",            s_svgWifiLow);
    LoadSvg(L"wifi-zero.svg",           s_svgWifiZero);
    LoadSvg(L"globe-off.svg",           s_svgWifiDisconnected);
    LoadSvg(L"volume.svg",              s_svgVolume);
    LoadSvg(L"volume-1.svg",            s_svgVolumeMedium);
    LoadSvg(L"volume-2.svg",            s_svgVolumeHigh);
    LoadSvg(L"volume-off.svg",          s_svgVolumeOff);
    LoadSvg(L"volume-x.svg",            s_svgVolumeMuted);

    return S_OK;
}

static void DrawIcon(ID2D1SvgDocument* svg, float cellLeft, float barH)
{
    if (!svg) return;
    const float x = cellLeft + (ICON_CELL_W - ICON_SIZE) * 0.5f;
    const float y = (barH    - ICON_SIZE)                * 0.5f;
    s_ctx->SetTransform(D2D1::Matrix3x2F::Translation(x, y));
    s_ctx->DrawSvgDocument(svg);
    s_ctx->SetTransform(D2D1::Matrix3x2F::Identity());
}

static void DrawDesktopPager(D2D1_SIZE_F size)
{
    const int count = GetDesktopRenderCount();
    if (!s_desktopBrush || count <= 0)
        return;

    for (int i = 0; i < count; ++i)
    {
        const float t = s_desktopT[i];
        const D2D1_RECT_F rect = GetDesktopIndicatorRect(size, i);

        const float channel = 0.54f + (1.0f - 0.54f) * t;
        s_desktopBrush->SetColor(D2D1::ColorF(channel, channel, channel, 1.0f));

        if (t <= 0.001f)
        {
            const D2D1_ELLIPSE dot = D2D1::Ellipse(
                D2D1::Point2F((rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f),
                DESKTOP_DOT_SIZE * 0.5f,
                DESKTOP_DOT_SIZE * 0.5f);
            s_ctx->FillEllipse(dot, s_desktopBrush.Get());
        }
        else
        {
            const float radius = (rect.bottom - rect.top) * 0.5f;
            s_ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), s_desktopBrush.Get());
        }
    }
}

static void DrawCompactVisualizer(float left, float centerY, float opacity)
{
    if (!s_visualizerBrush || opacity <= 0.001f)
        return;

    s_visualizerBrush->SetOpacity(opacity);
    for (int i = 0; i < 4; ++i)
    {
        const float wave = 0.5f + 0.5f * sinf(s_visualizerPhase + static_cast<float>(i) * 1.15f);
        const float h = 5.0f + wave * 8.0f;
        const float x = left + static_cast<float>(i) * (VIS_BAR_W + VIS_BAR_GAP);
        const D2D1_RECT_F rect = D2D1::RectF(x, centerY - h * 0.5f, x + VIS_BAR_W, centerY + h * 0.5f);
        s_ctx->FillRoundedRectangle(D2D1::RoundedRect(rect, VIS_BAR_W * 0.5f, VIS_BAR_W * 0.5f), s_visualizerBrush.Get());
    }
    s_visualizerBrush->SetOpacity(1.0f);
}

static void DrawCompactIslandContent(D2D1_RECT_F visualRect, D2D1_SIZE_F size, const std::wstring& text, bool hasPlayingMedia, float opacity)
{
    if (opacity <= 0.001f)
        return;

    s_textBrush->SetOpacity(opacity);
    if (hasPlayingMedia)
    {
        const float visW = VIS_BAR_W * 4.0f + VIS_BAR_GAP * 3.0f;
        const float gap = 12.0f;
        const float pad = 14.0f;
        const float maxTextW = (std::max)(0.0f, (visualRect.right - visualRect.left) - pad * 2.0f - visW - gap);
        const float measuredTextW = (std::min)(MeasureIslandTextWidth(text, maxTextW), maxTextW);
        const float groupW = visW + gap + measuredTextW;
        const float groupLeft = (visualRect.left + visualRect.right - groupW) * 0.5f;
        DrawCompactVisualizer(groupLeft, size.height * 0.5f, opacity);
        D2D1_RECT_F textRect = D2D1::RectF(
            groupLeft + visW + gap,
            visualRect.top,
            groupLeft + visW + gap + maxTextW,
            visualRect.bottom);
        s_islandTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        s_ctx->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), s_islandTextFormat.Get(), textRect, s_textBrush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    else
    {
        D2D1_RECT_F textRect = visualRect;
        textRect.left += 8.0f;
        textRect.right -= 8.0f;
        s_islandTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        s_ctx->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), s_islandTextFormat.Get(), textRect, s_textBrush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    s_textBrush->SetOpacity(1.0f);
}

static void DrawMediaIsland(D2D1_SIZE_F size)
{
    if (!s_textBrush || !s_islandTextFormat)
        return;

    s_mediaIslandRect = GetMediaIslandRect(size);
    const D2D1_RECT_F visualRect = D2D1::RectF(
        s_mediaIslandRect.left + ISLAND_HIT_PAD_H,
        s_mediaIslandRect.top,
        s_mediaIslandRect.right - ISLAND_HIT_PAD_H,
        s_mediaIslandRect.bottom);

    if (!s_compactTextInitialized)
    {
        s_compactText = CompactIslandText();
        s_compactHasPlayingMedia = GetCompactPlayingMediaIndex() >= 0;
        s_compactTextInitialized = true;
    }
    else if (!s_compactHasPlayingMedia && s_compactTextAnimDurationMs <= 0.0f)
    {
        s_compactText = FormatIslandTime();
    }

    if (s_compactTextAnimDurationMs > 0.0f)
    {
        const float eased = EaseOutCubic(s_compactTextAnimT);
        DrawCompactIslandContent(visualRect, size, s_compactPrevText, s_compactPrevHasPlayingMedia, 1.0f - eased);
        DrawCompactIslandContent(visualRect, size, s_compactText, s_compactHasPlayingMedia, eased);
    }
    else
    {
        DrawCompactIslandContent(visualRect, size, s_compactText, s_compactHasPlayingMedia, 1.0f);
    }
}

void Renderer_Render(HWND /*hWnd*/)
{
    if (!s_ctx || !s_swapChain) return;

    s_ctx->BeginDraw();
    s_ctx->Clear(D2D1::ColorF(0.f, 0.f, 0.f));

    const D2D1_SIZE_F size   = s_ctx->GetSize();
    const float       startX = size.width - STATUS_RIGHT_PAD - ICON_CELL_W * 3.0f;

    DrawDesktopPager(size);
    DrawMediaIsland(size);

    // ── Pill background (hover / drawer-open only) ────────────────────────────
    if (s_hoverProgress > 0.001f)
    {
        const D2D1_ROUNDED_RECT pillRR = GetHoverPillRect(size);
        s_statusHoverBrush->SetOpacity(s_hoverProgress * PILL_HOVER_ALPHA);
        s_ctx->FillRoundedRectangle(pillRR, s_statusHoverBrush.Get());
    }

    DrawIcon(GetBluetoothSvg(s_statusSnapshot.bluetooth), startX,                     size.height);
    DrawIcon(GetWifiSvg(s_statusSnapshot.wifi),           startX + ICON_CELL_W,       size.height);
    DrawIcon(GetVolumeSvg(s_statusSnapshot.volume),       startX + ICON_CELL_W * 2.f, size.height);

    HRESULT hr = s_ctx->EndDraw();
    if (SUCCEEDED(hr))
        s_swapChain->Present(1, 0);
}

void Renderer_Resize(UINT width, UINT height)
{
    if (!s_swapChain || !s_ctx || width == 0 || height == 0) return;
    s_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    RebuildTarget();
}

void Renderer_Destroy()
{
    s_statusHoverBrush.Reset();
    s_desktopBrush.Reset();
    s_textBrush.Reset();
    s_visualizerBrush.Reset();
    s_islandEllipsis.Reset();
    s_islandTextFormat.Reset();
    s_dwriteFactory.Reset();
    s_svgBluetooth.Reset();
    s_svgBluetoothConnected.Reset();
    s_svgBluetoothOff.Reset();
    s_svgWifi.Reset();
    s_svgWifiHigh.Reset();
    s_svgWifiLow.Reset();
    s_svgWifiZero.Reset();
    s_svgWifiDisconnected.Reset();
    s_svgVolume.Reset();
    s_svgVolumeMedium.Reset();
    s_svgVolumeHigh.Reset();
    s_svgVolumeOff.Reset();
    s_svgVolumeMuted.Reset();
    s_targetBitmap.Reset();
    s_ctx.Reset();
    s_d2dDevice.Reset();
    s_d2dFactory.Reset();
    s_swapChain.Reset();
    s_d3dDevice.Reset();
    memset(s_desktopT, 0, sizeof(s_desktopT));
    memset(s_desktopAnimFrom, 0, sizeof(s_desktopAnimFrom));
    s_desktopAnimDurationMs = 0.0f;
}

void Renderer_SetStatusSnapshot(const StatusSnapshot& snapshot)
{
    const std::wstring nextText = CompactIslandText(snapshot);
    const bool nextHasPlayingMedia = GetCompactPlayingMediaIndex(snapshot) >= 0;
    if (!s_compactTextInitialized)
    {
        s_compactText = nextText;
        s_compactHasPlayingMedia = nextHasPlayingMedia;
        s_compactTextInitialized = true;
    }
    else if (nextText != s_compactText || nextHasPlayingMedia != s_compactHasPlayingMedia)
    {
        s_compactPrevText = s_compactText;
        s_compactPrevHasPlayingMedia = s_compactHasPlayingMedia;
        s_compactText = nextText;
        s_compactHasPlayingMedia = nextHasPlayingMedia;
        s_compactTextAnimStartMs = GetTickCount64();
        s_compactTextAnimDurationMs = COMPACT_TEXT_FADE_MS;
        s_compactTextAnimT = 0.0f;
    }
    s_statusSnapshot = snapshot;
}

void Renderer_SetVirtualDesktopSnapshot(const VirtualDesktopSnapshot& snapshot)
{
    if (s_desktopSnapshot == snapshot)
        return;

    const bool animate = s_desktopSnapshot.available
        && snapshot.available
        && s_desktopSnapshot.count == snapshot.count
        && s_desktopSnapshot.currentIndex != snapshot.currentIndex;

    memcpy(s_desktopAnimFrom, s_desktopT, sizeof(s_desktopAnimFrom));
    s_desktopSnapshot = snapshot;

    const int count = GetDesktopRenderCount();
    if (!animate)
    {
        memset(s_desktopT, 0, sizeof(s_desktopT));
        for (int i = 0; i < count; ++i)
            s_desktopT[i] = GetDesktopTargetT(i);
        s_desktopAnimDurationMs = 0.0f;
        return;
    }

    s_desktopAnimStartMs = GetTickCount64();
    s_desktopAnimDurationMs = DESKTOP_MORPH_MS;
}

void Renderer_UpdateAccentTheme()
{
    if (s_visualizerBrush)
        s_visualizerBrush->SetColor(AccentTheme_GetPalette().fill);
}

bool Renderer_HitTestStatusBar(LONG xPx, LONG yPx)
{
    if (!s_ctx) return false;
    const D2D1_RECT_F statusRect = GetStatusRect(s_ctx->GetSize());

    // Convert hit rect from DIPs to physical pixels for comparison.
    const float left   = statusRect.left * s_dpiScale;
    const float right  = statusRect.right * s_dpiScale;
    const float top    = statusRect.top * s_dpiScale;
    const float bottom = statusRect.bottom * s_dpiScale;

    return xPx >= left && xPx <= right && yPx >= top && yPx <= bottom;
}

bool Renderer_HitTestMediaIsland(LONG xPx, LONG yPx)
{
    if (!s_ctx) return false;
    const D2D1_RECT_F rect = GetMediaIslandRect(s_ctx->GetSize());
    const float left = rect.left * s_dpiScale;
    const float right = rect.right * s_dpiScale;
    const float top = rect.top * s_dpiScale;
    const float bottom = rect.bottom * s_dpiScale;
    return xPx >= left && xPx <= right && yPx >= top && yPx <= bottom;
}

bool Renderer_GetMediaIslandRectPx(RECT* rect)
{
    if (!rect || !s_ctx)
        return false;

    const D2D1_RECT_F r = GetMediaIslandRect(s_ctx->GetSize());
    rect->left = static_cast<LONG>(r.left * s_dpiScale);
    rect->top = static_cast<LONG>(r.top * s_dpiScale);
    rect->right = static_cast<LONG>(r.right * s_dpiScale);
    rect->bottom = static_cast<LONG>(r.bottom * s_dpiScale);
    return true;
}

bool Renderer_HitTestDesktopPager(LONG xPx, LONG yPx, int* index)
{
    if (index)
        *index = -1;
    const int count = GetDesktopRenderCount();
    if (!s_ctx || count <= 0)
        return false;

    const D2D1_SIZE_F size = s_ctx->GetSize();
    const float xDip = static_cast<float>(xPx) / s_dpiScale;
    const float yDip = static_cast<float>(yPx) / s_dpiScale;
    if (yDip < 0.0f || yDip > size.height)
        return false;

    for (int i = 0; i < count; ++i)
    {
        const D2D1_RECT_F rect = GetDesktopIndicatorRect(size, i);
        if (xDip >= rect.left - DESKTOP_HIT_PAD && xDip <= rect.right + DESKTOP_HIT_PAD)
        {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

void Renderer_SetHover(bool hovered)
{
    s_hovered = hovered;
    // Only animate toward target if state actually changes the desired outcome
    const bool wantLit  = s_hovered || s_drawerOpen;
    const float target  = wantLit ? 1.0f : 0.0f;
    if (target == s_animTarget && s_animDurationMs > 0.0f) return; // already going there
    if (target == s_hoverProgress && s_animDurationMs <= 0.0f) return; // already there

    s_animFrom       = s_hoverProgress;
    s_animTarget     = target;
    s_animStartMs    = GetTickCount64();
    s_animDurationMs = wantLit ? HOVER_ANIM_IN_MS : HOVER_ANIM_OUT_MS;
}

void Renderer_SetDrawerOpen(bool open)
{
    if (s_drawerOpen == open) return;
    s_drawerOpen = open;

    const bool wantLit  = s_hovered || s_drawerOpen;
    const float target  = wantLit ? 1.0f : 0.0f;
    if (target == s_animTarget && s_animDurationMs > 0.0f) return;
    if (target == s_hoverProgress && s_animDurationMs <= 0.0f) return;

    s_animFrom       = s_hoverProgress;
    s_animTarget     = target;
    s_animStartMs    = GetTickCount64();
    s_animDurationMs = wantLit ? HOVER_ANIM_IN_MS : HOVER_ANIM_OUT_MS;
}

bool Renderer_TickAnimation()
{
    bool animating = false;

    if (GetCompactPlayingMediaIndex() >= 0)
    {
        s_visualizerPhase += 0.18f;
        if (s_visualizerPhase > 6.2831853f)
            s_visualizerPhase -= 6.2831853f;
        animating = true;
    }

    if (s_animDurationMs > 0.0f)
    {
        const float elapsed = static_cast<float>(GetTickCount64() - s_animStartMs);
        const float t       = Clamp01(elapsed / s_animDurationMs);

        s_hoverProgress = s_animFrom + (s_animTarget - s_animFrom) * EaseOutCubic(t);

        if (t >= 1.0f)
        {
            s_hoverProgress  = s_animTarget;
            s_animDurationMs = 0.0f;
        }
        else
        {
            animating = true;
        }
    }

    if (s_desktopAnimDurationMs > 0.0f)
    {
        const float elapsed = static_cast<float>(GetTickCount64() - s_desktopAnimStartMs);
        const float t = Clamp01(elapsed / s_desktopAnimDurationMs);
        const float eased = EaseOutCubic(t);
        const int count = GetDesktopRenderCount();

        for (int i = 0; i < DESKTOP_MAX_INDICATORS; ++i)
        {
            const float target = i < count ? GetDesktopTargetT(i) : 0.0f;
            s_desktopT[i] = s_desktopAnimFrom[i] + (target - s_desktopAnimFrom[i]) * eased;
        }

        if (t >= 1.0f)
        {
            for (int i = 0; i < DESKTOP_MAX_INDICATORS; ++i)
                s_desktopT[i] = i < count ? GetDesktopTargetT(i) : 0.0f;
            s_desktopAnimDurationMs = 0.0f;
        }
        else
        {
            animating = true;
        }
    }

    if (s_compactTextAnimDurationMs > 0.0f)
    {
        const float elapsed = static_cast<float>(GetTickCount64() - s_compactTextAnimStartMs);
        s_compactTextAnimT = Clamp01(elapsed / s_compactTextAnimDurationMs);
        if (s_compactTextAnimT >= 1.0f)
        {
            s_compactTextAnimT = 1.0f;
            s_compactTextAnimDurationMs = 0.0f;
        }
        else
        {
            animating = true;
        }
    }

    return animating;
}

UINT Renderer_GetAnimationTimerIntervalMs()
{
    if (s_animDurationMs > 0.0f
        || s_desktopAnimDurationMs > 0.0f
        || s_compactTextAnimDurationMs > 0.0f)
    {
        return ANIM_TIMER_FAST_MS;
    }

    if (GetCompactPlayingMediaIndex() >= 0)
        return COMPACT_VIS_TIMER_MS;

    return ANIM_TIMER_FAST_MS;
}
