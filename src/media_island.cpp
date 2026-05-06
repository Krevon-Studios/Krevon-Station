#include "media_island.h"
#include "renderer.h"
#include "status/status_monitor.h"
#include "window.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <windowsx.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    static constexpr wchar_t ISLAND_CLASS[] = L"KrevonMediaIsland";
    static constexpr UINT_PTR ANIM_TIMER = 31;
    static constexpr UINT_PTR WATCH_TIMER = 32;
    static constexpr UINT_PTR MEDIA_SWITCH_TIMER = 33;
    static constexpr UINT_PTR HOVER_TIMER = 34;
    static constexpr float MEDIA_SWITCH_DURATION_MS = 300.0f;
    static constexpr float COMPACT_W = 214.0f;
    static constexpr float COMPACT_H = static_cast<float>(NAVBAR_HEIGHT_DIP);
    static constexpr float EXP_W = 448.0f;
    static constexpr float EXP_H = 130.0f;
    static constexpr float ANIM_IN_MS = 260.0f;
    static constexpr float ANIM_OUT_MS = 200.0f;
    static constexpr float DOT_SIZE = 6.0f;
    static constexpr float DOT_ACTIVE_W = 16.0f;
    static constexpr float DOT_GAP = 4.0f;
    static constexpr float DOT_MORPH_MS = 180.0f;
    static constexpr UINT WATCH_TIMER_MS = 33;

    HWND s_hwnd = nullptr;
    HWND s_navbarHwnd = nullptr;
    float s_dpiScale = 1.0f;
    bool s_open = false;
    float s_anim = 0.0f;
    float s_animFrom = 0.0f;
    float s_animTarget = 0.0f;
    float s_animDurMs = 0.0f;
    ULONGLONG s_animStart = 0;
    bool s_mediaSwitchActive = false;
    float s_mediaSwitchT = 1.0f;
    ULONGLONG s_mediaSwitchStart = 0;
    MediaSessionInfo s_switchFromMedia = {};
    MediaSessionInfo s_switchToMedia = {};
    ComPtr<ID2D1Bitmap1> s_switchFromArt;
    ComPtr<ID2D1Bitmap1> s_switchToArt;
    float s_playingPulsePhase = 0.0f;
    float s_sliderWavePhase = 0.0f;
    float s_playbackLightT = 0.0f;
    int s_hoverControl = -1;
    int s_hoverDot = -1;
    float s_controlHoverT[3] = {};
    float s_dotT[32] = {};
    float s_dotFrom[32] = {};
    ULONGLONG s_dotAnimStart = 0;
    float s_dotAnimDurMs = 0.0f;

    StatusSnapshot s_snapshot = {};
    ComPtr<ID3D11Device> s_d3d;
    ComPtr<IDXGISwapChain1> s_swap;
    ComPtr<ID2D1Factory1> s_fac;
    ComPtr<ID2D1Device> s_d2dDev;
    ComPtr<ID2D1DeviceContext5> s_ctx;
    ComPtr<ID2D1Bitmap1> s_target;
    ComPtr<ID2D1RoundedRectangleGeometry> s_coverGeometry;
    ComPtr<IDCompositionDevice> s_dcompDevice;
    ComPtr<IDCompositionTarget> s_dcompTarget;
    ComPtr<IDCompositionVisual> s_dcompVisual;
    ComPtr<IWICImagingFactory> s_wicFactory;
    ComPtr<IDWriteFactory> s_dw;
    ComPtr<IDWriteTextFormat> s_tfApp;
    ComPtr<IDWriteTextFormat> s_tfTitle;
    ComPtr<IDWriteTextFormat> s_tfSub;
    ComPtr<IDWriteTextFormat> s_tfTime;
    ComPtr<IDWriteInlineObject> s_appEllipsis;
    ComPtr<IDWriteInlineObject> s_titleEllipsis;
    ComPtr<IDWriteInlineObject> s_subEllipsis;
    ComPtr<IDWriteInlineObject> s_timeEllipsis;
    ComPtr<ID2D1SolidColorBrush> s_brBg;
    ComPtr<ID2D1SolidColorBrush> s_brWhite;
    ComPtr<ID2D1SolidColorBrush> s_brGrey;
    ComPtr<ID2D1SolidColorBrush> s_brDim;
    ComPtr<ID2D1SolidColorBrush> s_brAccent;
    ComPtr<ID2D1SvgDocument> s_svgPlay;
    ComPtr<ID2D1SvgDocument> s_svgPause;
    ComPtr<ID2D1SvgDocument> s_svgNext;
    ComPtr<ID2D1SvgDocument> s_svgPrev;
    ComPtr<ID2D1Bitmap1> s_coverArt;
    std::wstring s_coverSessionId;
    size_t s_coverKeyHash = 0;
    size_t s_coverKeySize = 0;
    D2D1_RECT_F s_prevRect = {};
    D2D1_RECT_F s_playRect = {};
    D2D1_RECT_F s_nextRect = {};
    D2D1_RECT_F s_progressRect = {};
    D2D1_RECT_F s_dotRects[32] = {};

    float Clamp01(float v)
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    float EaseOutCubic(float t)
    {
        const float inv = 1.0f - t;
        return 1.0f - inv * inv * inv;
    }

    float EaseInOutCubic(float t)
    {
        if (t < 0.5f)
            return 4.0f * t * t * t;
        const float f = -2.0f * t + 2.0f;
        return 1.0f - (f * f * f) * 0.5f;
    }

    bool PtInRectF(const D2D1_RECT_F& r, D2D1_POINT_2F p)
    {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    }

    size_t HashBytes(const std::vector<BYTE>& bytes)
    {
        size_t hash = 1469598103934665603ull;
        for (BYTE value : bytes)
        {
            hash ^= static_cast<size_t>(value);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    void ClearMediaCaches()
    {
        s_snapshot = {};
        s_switchFromMedia = {};
        s_switchToMedia = {};
        s_switchFromArt.Reset();
        s_switchToArt.Reset();
        s_coverArt.Reset();
        s_coverSessionId.clear();
        s_coverKeyHash = 0;
        s_coverKeySize = 0;
        s_mediaSwitchActive = false;
        s_mediaSwitchT = 1.0f;
    }

    int SelectedIndex()
    {
        if (s_snapshot.mediaSelectedIndex >= 0 && s_snapshot.mediaSelectedIndex < static_cast<int>(s_snapshot.mediaSessions.size()))
            return s_snapshot.mediaSelectedIndex;
        if (s_snapshot.mediaCurrentIndex >= 0 && s_snapshot.mediaCurrentIndex < static_cast<int>(s_snapshot.mediaSessions.size()))
            return s_snapshot.mediaCurrentIndex;
        return s_snapshot.mediaSessions.empty() ? -1 : 0;
    }

    const MediaSessionInfo* SelectedMedia()
    {
        const int index = SelectedIndex();
        if (index < 0 || index >= static_cast<int>(s_snapshot.mediaSessions.size()))
            return nullptr;
        return &s_snapshot.mediaSessions[index];
    }

    std::wstring FormatIslandTime()
    {
        static const wchar_t* DAYS[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
        static const wchar_t* MONTHS[] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
        std::time_t now = std::time(nullptr);
        std::tm local = {};
        localtime_s(&local, &now);
        int hour = local.tm_hour % 12;
        if (hour == 0) hour = 12;
        wchar_t buf[64] = {};
        swprintf_s(buf, L"%s, %s %d  %d:%02d %s", DAYS[local.tm_wday], MONTHS[local.tm_mon],
            local.tm_mday, hour, local.tm_min, local.tm_hour >= 12 ? L"PM" : L"AM");
        return buf;
    }

    void IconPath(const wchar_t* filename, wchar_t* out, DWORD outLen)
    {
        GetModuleFileNameW(nullptr, out, outLen);
        wchar_t* slash = wcsrchr(out, L'\\');
        if (slash) *(slash + 1) = L'\0';
        wcsncat_s(out, outLen, L"assets\\icons\\", _TRUNCATE);
        wcsncat_s(out, outLen, filename, _TRUNCATE);
    }

    HRESULT LoadSvg(const wchar_t* filename, float size, ComPtr<ID2D1SvgDocument>& out)
    {
        wchar_t path[MAX_PATH];
        IconPath(filename, path, MAX_PATH);
        IStream* raw = nullptr;
        HRESULT hr = SHCreateStreamOnFileW(path, STGM_READ | STGM_SHARE_DENY_WRITE, &raw);
        if (FAILED(hr)) return hr;
        ComPtr<IStream> stream(raw);
        hr = s_ctx->CreateSvgDocument(stream.Get(), D2D1::SizeF(size, size), &out);
        if (FAILED(hr)) return hr;
        ComPtr<ID2D1SvgElement> root;
        out->GetRoot(&root);
        out->SetViewportSize(D2D1::SizeF(size, size));
        root->SetAttributeValue(L"width", size);
        root->SetAttributeValue(L"height", size);
        root->SetAttributeValue(L"color", D2D1_SVG_ATTRIBUTE_STRING_TYPE_SVG, L"white");
        root->SetAttributeValue(L"stroke", D2D1_SVG_ATTRIBUTE_STRING_TYPE_SVG, L"white");
        root->SetAttributeValue(L"fill", D2D1_SVG_ATTRIBUTE_STRING_TYPE_SVG, L"white");
        return S_OK;
    }

    void DrawSvg(ID2D1SvgDocument* svg, float cx, float cy, float size, float opacity)
    {
        if (!svg || opacity <= 0.001f)
            return;
        D2D1_MATRIX_3X2_F old;
        s_ctx->GetTransform(&old);
        D2D1_LAYER_PARAMETERS layer = D2D1::LayerParameters();
        layer.opacity = opacity;
        s_ctx->PushLayer(layer, nullptr);
        const D2D1_SIZE_F vp = svg->GetViewportSize();
        const float scale = size / (vp.width > 0.0f ? vp.width : size);
        s_ctx->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
            D2D1::Matrix3x2F::Translation(cx - size * 0.5f, cy - size * 0.5f) *
            old);
        s_ctx->DrawSvgDocument(svg);
        s_ctx->SetTransform(old);
        s_ctx->PopLayer();
    }

    HRESULT RebuildTarget()
    {
        s_ctx->SetTarget(nullptr);
        s_target.Reset();
        ComPtr<IDXGISurface> buf;
        HRESULT hr = s_swap->GetBuffer(0, IID_PPV_ARGS(&buf));
        if (FAILED(hr)) return hr;
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        hr = s_ctx->CreateBitmapFromDxgiSurface(buf.Get(), &props, &s_target);
        if (FAILED(hr)) return hr;
        s_ctx->SetTarget(s_target.Get());
        return S_OK;
    }

    ComPtr<ID2D1Bitmap1> CreateBitmapFromBytes(const std::vector<BYTE>& bytes)
    {
        if (bytes.empty() || !s_ctx)
            return nullptr;

        if (!s_wicFactory)
            return nullptr;

        ComPtr<IWICStream> stream;
        if (FAILED(s_wicFactory->CreateStream(&stream)))
            return nullptr;
        if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))))
            return nullptr;

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(s_wicFactory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
            return nullptr;

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame)))
            return nullptr;

        ComPtr<IWICFormatConverter> conv;
        if (FAILED(s_wicFactory->CreateFormatConverter(&conv)))
            return nullptr;
        if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
            return nullptr;

        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap1> bmp;
        s_ctx->CreateBitmapFromWicBitmap(conv.Get(), &props, &bmp);
        return bmp;
    }

    void UpdateCoverArt()
    {
        const auto* media = SelectedMedia();
        const std::vector<BYTE>* nextBytes = media ? &media->thumbnailBytes : nullptr;
        const size_t nextSize = nextBytes ? nextBytes->size() : 0;
        const size_t nextHash = (nextBytes && !nextBytes->empty()) ? HashBytes(*nextBytes) : 0;
        const std::wstring nextSessionId = media ? media->sessionId : std::wstring{};
        if (nextSessionId == s_coverSessionId && nextSize == s_coverKeySize && nextHash == s_coverKeyHash)
            return;
        s_coverSessionId = nextSessionId;
        s_coverKeySize = nextSize;
        s_coverKeyHash = nextHash;
        s_coverArt = nextBytes ? CreateBitmapFromBytes(*nextBytes) : nullptr;
    }

    void PositionWindow()
    {
        if (!s_hwnd || !s_navbarHwnd)
            return;

        RECT nav;
        GetWindowRect(s_navbarHwnd, &nav);
        const int w = MulDiv(static_cast<int>(EXP_W), static_cast<int>(s_dpiScale * 96.0f), 96);
        const int h = MulDiv(static_cast<int>(EXP_H), static_cast<int>(s_dpiScale * 96.0f), 96);
        const int x = nav.left + ((nav.right - nav.left) - w) / 2;
        const int y = nav.top;

        SetWindowPos(s_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (s_swap)
        {
            s_swap->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0);
            RebuildTarget();
            if (s_dcompDevice)
            {
                s_dcompVisual->SetContent(s_swap.Get());
                s_dcompTarget->SetRoot(s_dcompVisual.Get());
                s_dcompDevice->Commit();
            }
        }
    }

    void StartAnim(float target, float duration)
    {
        const float distance = fabsf(target - s_anim);
        if (distance <= 0.001f)
        {
            s_anim = target;
            s_animTarget = target;
            s_animDurMs = 0.0f;
            return;
        }

        s_animFrom = s_anim;
        s_animTarget = target;
        s_animDurMs = (std::max)(80.0f, duration * distance);
        s_animStart = GetTickCount64();
        SetTimer(s_hwnd, ANIM_TIMER, 16, nullptr);
    }

    void StartDotAnim()
    {
        memcpy(s_dotFrom, s_dotT, sizeof(s_dotFrom));
        s_dotAnimStart = GetTickCount64();
        s_dotAnimDurMs = DOT_MORPH_MS;
        SetTimer(s_hwnd, ANIM_TIMER, 16, nullptr);
    }

    void StartMediaSwitch(const MediaSessionInfo& fromMedia, const MediaSessionInfo& toMedia)
    {
        if (fromMedia.sessionId == toMedia.sessionId)
            return;

        s_switchFromMedia = fromMedia;
        s_switchToMedia = toMedia;
        s_switchFromArt = CreateBitmapFromBytes(fromMedia.thumbnailBytes);
        s_switchToArt = CreateBitmapFromBytes(toMedia.thumbnailBytes);
        s_switchFromMedia.thumbnailBytes.clear();
        s_switchFromMedia.thumbnailBytes.shrink_to_fit();
        s_switchToMedia.thumbnailBytes.clear();
        s_switchToMedia.thumbnailBytes.shrink_to_fit();
        s_mediaSwitchActive = true;
        s_mediaSwitchT = 0.0f;
        s_mediaSwitchStart = GetTickCount64();
        SetTimer(s_hwnd, MEDIA_SWITCH_TIMER, 16, nullptr);
    }

    void DrawFallbackTime(float opacity)
    {
        std::wstring text = FormatIslandTime();
        s_brWhite->SetOpacity(opacity);
        D2D1_RECT_F r = D2D1::RectF(0.0f, 0.0f, EXP_W, EXP_H);
        s_ctx->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), s_tfTime.Get(), r, s_brWhite.Get());
    }

    D2D1_RECT_F ScaleRect(D2D1_RECT_F rect, float scale)
    {
        const float cx = (rect.left + rect.right) * 0.5f;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float hw = (rect.right - rect.left) * 0.5f * scale;
        const float hh = (rect.bottom - rect.top) * 0.5f * scale;
        return D2D1::RectF(cx - hw, cy - hh, cx + hw, cy + hh);
    }

    void DrawCoverArtBitmap(ID2D1Bitmap1* bitmap, D2D1_RECT_F artRect, float opacity, float scale)
    {
        if (!bitmap || opacity <= 0.001f || scale <= 0.001f)
            return;

        const D2D1_SIZE_F bitmapSize = bitmap->GetSize();
        const float srcW = bitmapSize.width;
        const float srcH = bitmapSize.height;
        const float dstW = artRect.right - artRect.left;
        const float dstH = artRect.bottom - artRect.top;
        D2D1_RECT_F srcRect = D2D1::RectF(0.0f, 0.0f, srcW, srcH);

        if (srcW > 0.0f && srcH > 0.0f && dstW > 0.0f && dstH > 0.0f)
        {
            const float srcAspect = srcW / srcH;
            const float dstAspect = dstW / dstH;
            if (srcAspect > dstAspect)
            {
                const float cropW = srcH * dstAspect;
                const float x = (srcW - cropW) * 0.5f;
                srcRect = D2D1::RectF(x, 0.0f, x + cropW, srcH);
            }
            else
            {
                const float cropH = srcW / dstAspect;
                const float y = (srcH - cropH) * 0.5f;
                srcRect = D2D1::RectF(0.0f, y, srcW, y + cropH);
            }
        }

        if (s_coverGeometry)
        {
            D2D1_LAYER_PARAMETERS layer = D2D1::LayerParameters();
            layer.contentBounds = artRect;
            layer.geometricMask = s_coverGeometry.Get();
            s_ctx->PushLayer(layer, nullptr);
            s_ctx->DrawBitmap(bitmap, ScaleRect(artRect, scale), opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, srcRect);
            s_ctx->PopLayer();
        }
        else
        {
            s_ctx->DrawBitmap(bitmap, ScaleRect(artRect, scale), opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, srcRect);
        }
    }

    void DrawCoverFallback(const MediaSessionInfo& media, D2D1_RECT_F artRect, float opacity, float scale)
    {
        if (opacity <= 0.001f)
            return;

        const D2D1_RECT_F scaledRect = ScaleRect(artRect, scale);
        s_brDim->SetOpacity(opacity);
        s_ctx->FillRoundedRectangle(D2D1::RoundedRect(scaledRect, 14.0f * scale, 14.0f * scale), s_brDim.Get());
        std::wstring fallback = media.appName.empty() ? L"M" : media.appName.substr(0, 1);
        s_brGrey->SetOpacity(opacity);
        s_ctx->DrawTextW(fallback.c_str(), static_cast<UINT32>(fallback.size()), s_tfTime.Get(), scaledRect, s_brGrey.Get());
    }

    void DrawCoverSlot(const MediaSessionInfo& media, ID2D1Bitmap1* bitmap, D2D1_RECT_F artRect, float opacity, float scale)
    {
        if (bitmap)
            DrawCoverArtBitmap(bitmap, artRect, opacity, scale);
        else
            DrawCoverFallback(media, artRect, opacity, scale);
    }

    void DrawControlIcon(D2D1_RECT_F rect, ID2D1SvgDocument* svg, bool enabled, int index, float opacity)
    {
        const float hoverT = (index >= 0 && index < 3) ? EaseOutCubic(s_controlHoverT[index]) : 0.0f;
        const float iconOpacity = opacity * (enabled ? (0.78f + 0.22f * hoverT) : 0.32f);
        const float baseSize = index == 1 ? 29.0f : 18.5f;
        DrawSvg(svg, (rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f, baseSize, iconOpacity);
    }

    void DrawMediaControls(const MediaSessionInfo& media, float opacity)
    {
        DrawControlIcon(s_prevRect, s_svgPrev.Get(), media.canSkipPrevious, 0, opacity);
        DrawControlIcon(s_playRect, media.playbackState == MediaPlaybackState::Playing ? s_svgPause.Get() : s_svgPlay.Get(), media.canPlayPause, 1, opacity);
        DrawControlIcon(s_nextRect, s_svgNext.Get(), media.canSkipNext, 2, opacity);
    }

    float MeasureTextWidth(const std::wstring& text, IDWriteTextFormat* format, float maxWidth)
    {
        if (!s_dw || !format || text.empty())
            return 0.0f;

        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(s_dw->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, maxWidth, 24.0f, &layout)))
            return 0.0f;

        DWRITE_TEXT_METRICS metrics = {};
        if (FAILED(layout->GetMetrics(&metrics)))
            return 0.0f;
        return metrics.widthIncludingTrailingWhitespace;
    }

    long long ClampTicks(long long value, long long low, long long high)
    {
        return (std::max)(low, (std::min)(high, value));
    }

    long long CurrentTimelinePositionTicks(const MediaSessionInfo& media)
    {
        if (!media.hasTimeline || media.timelineEndTicks <= media.timelineStartTicks)
            return media.timelinePositionTicks;

        long long position = media.timelinePositionTicks;
        if (media.playbackState == MediaPlaybackState::Playing && media.timelineSnapshotMs > 0)
        {
            const ULONGLONG elapsedMs = GetTickCount64() - media.timelineSnapshotMs;
            position += static_cast<long long>(elapsedMs) * 10000ll;
        }
        return ClampTicks(position, media.timelineStartTicks, media.timelineEndTicks);
    }

    void DrawMediaSlider(const MediaSessionInfo& media, float left, float right, float y, float opacity)
    {
        s_progressRect = {};
        if (!media.hasTimeline || media.timelineEndTicks <= media.timelineStartTicks)
            return;

        const long long start = media.timelineStartTicks;
        const long long end = media.timelineEndTicks;
        const long long position = CurrentTimelinePositionTicks(media);
        const long long duration = end - start;
        if (duration <= 0)
            return;

        const float trackLeft = left;
        const float trackRight = right;
        const float trackH = 5.5f;
        const float trackTop = y - trackH * 0.5f;
        const float trackBottom = y + trackH * 0.5f;
        const float progress = static_cast<float>(position - start) / static_cast<float>(duration);
        const float fillRight = trackLeft + (trackRight - trackLeft) * Clamp01(progress);

        constexpr float waveAmp = 3.2f;
        constexpr float waveRadius = 3.1f;
        constexpr float waveStep = 2.0f;
        constexpr float waveLength = 31.0f;
        constexpr float twoPi = 6.2831853f;
        s_progressRect = D2D1::RectF(trackLeft, y - waveAmp - waveRadius - 3.0f, trackRight, y + waveAmp + waveRadius + 3.0f);

        s_brGrey->SetOpacity(opacity * 0.30f);
        if (fillRight < trackRight - 0.1f)
        {
            const D2D1_RECT_F inactiveRect = D2D1::RectF(fillRight, trackTop, trackRight, trackBottom);
            s_ctx->FillRoundedRectangle(D2D1::RoundedRect(inactiveRect, trackH * 0.5f, trackH * 0.5f), s_brGrey.Get());
        }

        if (fillRight > trackLeft + 0.1f)
        {
            s_brAccent->SetOpacity(opacity * 0.95f);
            for (float x = trackLeft; x <= fillRight; x += waveStep)
            {
                const float phase = ((x - trackLeft) / waveLength) * twoPi + s_sliderWavePhase;
                const float cy = y + sinf(phase) * waveAmp;
                s_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), waveRadius, waveRadius), s_brAccent.Get());
            }
        }

        s_brAccent->SetOpacity(opacity);
        s_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillRight, y), 7.0f, 7.0f), s_brAccent.Get());

        s_brGrey->SetOpacity(1.0f);
        s_brWhite->SetOpacity(1.0f);
        s_brAccent->SetOpacity(1.0f);
    }

    void DrawDots(float x, float y, int count, int selected, float opacity)
    {
        UNREFERENCED_PARAMETER(selected);
        if (count <= 1)
            return;

        count = min(count, 32);
        s_brWhite->SetOpacity(opacity);
        for (int i = 0; i < count; ++i)
        {
            const float t = s_dotT[i];
            const float width = DOT_SIZE + (DOT_ACTIVE_W - DOT_SIZE) * s_dotT[i];
            s_dotRects[i] = D2D1::RectF(x, y, x + width, y + DOT_SIZE);
            const float channel = 0.54f + (1.0f - 0.54f) * t;
            s_brWhite->SetColor(D2D1::ColorF(channel, channel, channel, 1.0f));
            if (t <= 0.001f)
            {
                const D2D1_ELLIPSE dot = D2D1::Ellipse(
                    D2D1::Point2F((s_dotRects[i].left + s_dotRects[i].right) * 0.5f, (s_dotRects[i].top + s_dotRects[i].bottom) * 0.5f),
                    DOT_SIZE * 0.5f,
                    DOT_SIZE * 0.5f);
                s_ctx->FillEllipse(dot, s_brWhite.Get());
            }
            else
            {
                s_ctx->FillRoundedRectangle(D2D1::RoundedRect(s_dotRects[i], DOT_SIZE * 0.5f, DOT_SIZE * 0.5f), s_brWhite.Get());
            }
            x += width + DOT_GAP;
        }
        s_brWhite->SetColor(D2D1::ColorF(1, 1, 1, 1));
        s_brWhite->SetOpacity(1.0f);
    }

    void DrawPlaybackLight(const MediaSessionInfo& media, float opacity)
    {
        const D2D1_POINT_2F center = D2D1::Point2F(424.0f, 23.0f);
        const float onT = EaseOutCubic(s_playbackLightT);
        const float offT = 1.0f - onT;

        if (offT > 0.001f)
        {
            s_brGrey->SetOpacity(opacity * 0.45f * offT);
            s_ctx->FillEllipse(D2D1::Ellipse(center, 3.5f, 3.5f), s_brGrey.Get());
        }

        if (onT > 0.001f)
        {
            const float pulse = 0.5f + 0.5f * sinf(s_playingPulsePhase);
            const float glow = 0.75f + 0.25f * pulse;

            s_brAccent->SetOpacity(opacity * onT * 0.06f * glow);
            s_ctx->FillEllipse(D2D1::Ellipse(center, 18.0f, 18.0f), s_brAccent.Get());
            s_brAccent->SetOpacity(opacity * onT * 0.10f * glow);
            s_ctx->FillEllipse(D2D1::Ellipse(center, 13.0f, 13.0f), s_brAccent.Get());
            s_brAccent->SetOpacity(opacity * onT * 0.18f * glow);
            s_ctx->FillEllipse(D2D1::Ellipse(center, 8.5f, 8.5f), s_brAccent.Get());

            s_brAccent->SetOpacity(opacity * onT * 0.96f);
            s_ctx->FillEllipse(D2D1::Ellipse(center, 4.3f, 4.3f), s_brAccent.Get());
            s_brAccent->SetOpacity(opacity * onT);
            s_ctx->FillEllipse(D2D1::Ellipse(center, 2.0f, 2.0f), s_brAccent.Get());
        }
    }

    void DrawMediaContent(float opacity)
    {
        const auto* media = SelectedMedia();
        if (!media)
        {
            DrawFallbackTime(opacity);
            return;
        }

        const D2D1_RECT_F artRect = D2D1::RectF(20.0f, 24.0f, 108.0f, 112.0f);
        const float appRowTop = 14.0f;
        const float appRowH = 20.0f;
        const float appMaxW = 92.0f;
        const float appTextW = (std::min)(MeasureTextWidth(media->appName, s_tfApp.Get(), appMaxW), appMaxW);
        const float dotsX = 126.0f + appTextW + 8.0f;
        const float dotsY = appRowTop + (appRowH - DOT_SIZE) * 0.5f;
        D2D1_RECT_F appRect = D2D1::RectF(126.0f, appRowTop, 126.0f + appMaxW, appRowTop + appRowH);
        const float contentLeft = 126.0f;
        const float contentRight = 426.0f;
        D2D1_RECT_F titleRect = D2D1::RectF(contentLeft, 38.0f, contentRight, 60.0f);
        D2D1_RECT_F subRect = D2D1::RectF(contentLeft, 58.0f, contentRight, 77.0f);

        if (s_mediaSwitchActive)
        {
            const float t = EaseInOutCubic(s_mediaSwitchT);
            const float outA = opacity * (1.0f - t);
            const float inA = opacity * t;
            const float outY = -14.0f * t;
            const float inY = 14.0f * (1.0f - t);

            s_brGrey->SetOpacity(outA);
            s_ctx->DrawTextW(s_switchFromMedia.appName.c_str(), static_cast<UINT32>(s_switchFromMedia.appName.size()), s_tfApp.Get(), appRect, s_brGrey.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            s_brGrey->SetOpacity(inA);
            s_ctx->DrawTextW(s_switchToMedia.appName.c_str(), static_cast<UINT32>(s_switchToMedia.appName.size()), s_tfApp.Get(), appRect, s_brGrey.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            DrawCoverSlot(s_switchFromMedia, s_switchFromArt.Get(), artRect, outA, 1.0f - 0.12f * t);
            DrawCoverSlot(s_switchToMedia, s_switchToArt.Get(), artRect, inA, 0.88f + 0.12f * t);

            const std::wstring fromTitle = s_switchFromMedia.title.empty() ? s_switchFromMedia.appName : s_switchFromMedia.title;
            const std::wstring toTitle = s_switchToMedia.title.empty() ? s_switchToMedia.appName : s_switchToMedia.title;
            s_brWhite->SetOpacity(outA);
            s_ctx->DrawTextW(fromTitle.c_str(), static_cast<UINT32>(fromTitle.size()), s_tfTitle.Get(),
                D2D1::RectF(titleRect.left, titleRect.top + outY, titleRect.right, titleRect.bottom + outY), s_brWhite.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            s_brWhite->SetOpacity(inA);
            s_ctx->DrawTextW(toTitle.c_str(), static_cast<UINT32>(toTitle.size()), s_tfTitle.Get(),
                D2D1::RectF(titleRect.left, titleRect.top + inY, titleRect.right, titleRect.bottom + inY), s_brWhite.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            s_brGrey->SetOpacity(outA);
            s_ctx->DrawTextW(s_switchFromMedia.subtitle.c_str(), static_cast<UINT32>(s_switchFromMedia.subtitle.size()), s_tfSub.Get(),
                D2D1::RectF(subRect.left, subRect.top + outY, subRect.right, subRect.bottom + outY), s_brGrey.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            s_brGrey->SetOpacity(inA);
            s_ctx->DrawTextW(s_switchToMedia.subtitle.c_str(), static_cast<UINT32>(s_switchToMedia.subtitle.size()), s_tfSub.Get(),
                D2D1::RectF(subRect.left, subRect.top + inY, subRect.right, subRect.bottom + inY), s_brGrey.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        else
        {
            DrawCoverSlot(*media, s_coverArt.Get(), artRect, opacity, 1.0f);
            s_brGrey->SetOpacity(opacity);
            s_ctx->DrawTextW(media->appName.c_str(), static_cast<UINT32>(media->appName.size()), s_tfApp.Get(), appRect, s_brGrey.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            s_brWhite->SetOpacity(opacity);
            const std::wstring title = media->title.empty() ? media->appName : media->title;
            s_ctx->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), s_tfTitle.Get(), titleRect, s_brWhite.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            s_brGrey->SetOpacity(opacity);
            s_ctx->DrawTextW(media->subtitle.c_str(), static_cast<UINT32>(media->subtitle.size()), s_tfSub.Get(), subRect, s_brGrey.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        const float controlsCenterX = (contentLeft + contentRight) * 0.5f;
        const float controlsCenterY = 91.0f;
        s_prevRect = D2D1::RectF(controlsCenterX - 54.0f, controlsCenterY - 13.0f, controlsCenterX - 30.0f, controlsCenterY + 13.0f);
        s_playRect = D2D1::RectF(controlsCenterX - 16.0f, controlsCenterY - 18.0f, controlsCenterX + 16.0f, controlsCenterY + 18.0f);
        s_nextRect = D2D1::RectF(controlsCenterX + 30.0f, controlsCenterY - 13.0f, controlsCenterX + 54.0f, controlsCenterY + 13.0f);
        if (s_mediaSwitchActive)
        {
            const float t = EaseInOutCubic(s_mediaSwitchT);
            DrawMediaControls(s_switchFromMedia, opacity * (1.0f - t));
            DrawMediaControls(s_switchToMedia, opacity * t);
        }
        else
        {
            DrawMediaControls(*media, opacity);
        }

        const int count = static_cast<int>(s_snapshot.mediaSessions.size());
        DrawDots(dotsX, dotsY, count, SelectedIndex(), opacity);
        const float sliderInset = 18.0f;
        const float sliderLeft = contentLeft + sliderInset;
        const float sliderRight = contentRight - sliderInset;
        if (s_mediaSwitchActive)
        {
            const float t = EaseInOutCubic(s_mediaSwitchT);
            DrawMediaSlider(s_switchFromMedia, sliderLeft, sliderRight, 116.0f, opacity * (1.0f - t));
            DrawMediaSlider(s_switchToMedia, sliderLeft, sliderRight, 116.0f, opacity * t);
        }
        else
        {
            DrawMediaSlider(*media, sliderLeft, sliderRight, 116.0f, opacity);
        }

        DrawPlaybackLight(*media, opacity);
    }

    void Render()
    {
        if (!s_ctx || !s_swap)
            return;

        const float t = s_anim;
        const float w = COMPACT_W + (EXP_W - COMPACT_W) * t;
        const float h = COMPACT_H + (EXP_H - COMPACT_H) * t;
        const float left = (EXP_W - w) * 0.5f;
        const D2D1_RECT_F island = D2D1::RectF(left, 0.0f, left + w, h);
        const float radius = 14.0f + (28.0f - 14.0f) * t;
        const float expandedOpacity = Clamp01((t - 0.28f) / 0.72f);

        s_ctx->BeginDraw();
        s_ctx->Clear(D2D1::ColorF(0, 0, 0, 0));

        if (s_anim > 0.001f)
        {
            s_brBg->SetOpacity(1.0f);
            s_ctx->FillRoundedRectangle(D2D1::RoundedRect(island, radius, radius), s_brBg.Get());

            D2D1_LAYER_PARAMETERS layer = D2D1::LayerParameters();
            layer.contentBounds = island;
            layer.opacity = expandedOpacity;
            s_ctx->PushLayer(layer, nullptr);
            DrawMediaContent(1.0f);
            s_ctx->PopLayer();
        }

        HRESULT hr = s_ctx->EndDraw();
        if (SUCCEEDED(hr))
            s_swap->Present(0, 0);
    }

    bool PointInNavbarIsland(POINT pt)
    {
        RECT compact = {};
        if (!Renderer_GetMediaIslandRectPx(&compact))
            return false;
        POINT pts[2] = { { compact.left, compact.top }, { compact.right, compact.bottom } };
        MapWindowPoints(s_navbarHwnd, nullptr, pts, 2);
        compact.left = pts[0].x;
        compact.top = pts[0].y;
        compact.right = pts[1].x;
        compact.bottom = pts[1].y;
        return PtInRect(&compact, pt) != FALSE;
    }

    bool PointInPopup(POINT pt)
    {
        if (!s_hwnd || !IsWindowVisible(s_hwnd))
            return false;
        RECT rc = {};
        GetWindowRect(s_hwnd, &rc);
        return PtInRect(&rc, pt) != FALSE;
    }

    void WatchCursor()
    {
        POINT pt = {};
        GetCursorPos(&pt);
        if (!PointInPopup(pt) && !PointInNavbarIsland(pt) && s_animTarget > 0.0f)
            StartAnim(0.0f, ANIM_OUT_MS);
    }

    bool TickControlHover()
    {
        bool changing = false;
        constexpr float step = 16.0f / 120.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float target = s_hoverControl == i ? 1.0f : 0.0f;
            const float dir = target > s_controlHoverT[i] ? step : -step;
            float next = Clamp01(s_controlHoverT[i] + dir);
            if (fabsf(next - target) < step)
                next = target;
            if (next != s_controlHoverT[i])
            {
                s_controlHoverT[i] = next;
                changing = true;
            }
        }
        return changing;
    }

    bool TickPlaybackLight()
    {
        const auto* media = SelectedMedia();
        const float target = (media && media->playbackState == MediaPlaybackState::Playing) ? 1.0f : 0.0f;
        constexpr float step = 16.0f / 180.0f;
        float next = s_playbackLightT;
        if (target > s_playbackLightT)
            next = Clamp01(s_playbackLightT + step);
        else
            next = Clamp01(s_playbackLightT - step);
        if (fabsf(next - target) < step)
            next = target;
        const bool changing = next != s_playbackLightT;
        s_playbackLightT = next;
        return changing;
    }

    void ArmPlaybackLightTimer()
    {
        if (s_hwnd && s_open)
            SetTimer(s_hwnd, WATCH_TIMER, WATCH_TIMER_MS, nullptr);
    }

    void UpdateHover(LPARAM lParam)
    {
        D2D1_POINT_2F p = D2D1::Point2F(GET_X_LPARAM(lParam) / s_dpiScale, GET_Y_LPARAM(lParam) / s_dpiScale);
        int nextControl = -1;
        if (PtInRectF(s_prevRect, p)) nextControl = 0;
        else if (PtInRectF(s_playRect, p)) nextControl = 1;
        else if (PtInRectF(s_nextRect, p)) nextControl = 2;

        int nextDot = -1;
        const int count = min(static_cast<int>(s_snapshot.mediaSessions.size()), 32);
        for (int i = 0; i < count; ++i)
        {
            if (PtInRectF(s_dotRects[i], p))
            {
                nextDot = i;
                break;
            }
        }

        if (nextControl != s_hoverControl || nextDot != s_hoverDot)
        {
            s_hoverControl = nextControl;
            s_hoverDot = nextDot;
            SetTimer(s_hwnd, HOVER_TIMER, 16, nullptr);
        }
    }

    bool PointOverPagination(D2D1_POINT_2F p)
    {
        const int count = min(static_cast<int>(s_snapshot.mediaSessions.size()), 32);
        if (count <= 1)
            return false;

        D2D1_RECT_F bounds = s_dotRects[0];
        for (int i = 1; i < count; ++i)
        {
            bounds.left = (std::min)(bounds.left, s_dotRects[i].left);
            bounds.top = (std::min)(bounds.top, s_dotRects[i].top);
            bounds.right = (std::max)(bounds.right, s_dotRects[i].right);
            bounds.bottom = (std::max)(bounds.bottom, s_dotRects[i].bottom);
        }

        bounds.left -= 4.0f;
        bounds.top -= 5.0f;
        bounds.right += 4.0f;
        bounds.bottom += 5.0f;
        return PtInRectF(bounds, p);
    }

    void SelectMediaIndex(int index)
    {
        const int count = min(static_cast<int>(s_snapshot.mediaSessions.size()), 32);
        if (index < 0 || index >= count || index == SelectedIndex())
            return;

        const auto* media = SelectedMedia();
        if (!media)
            return;

        const MediaSessionInfo fromMedia = *media;
        const MediaSessionInfo toMedia = s_snapshot.mediaSessions[index];
        s_snapshot.mediaSelectedIndex = index;
        StartMediaSwitch(fromMedia, toMedia);
        StatusMonitor_SelectMediaSession(index);
        StartDotAnim();
        Render();
    }

    void HandleClick(LPARAM lParam)
    {
        D2D1_POINT_2F p = D2D1::Point2F(GET_X_LPARAM(lParam) / s_dpiScale, GET_Y_LPARAM(lParam) / s_dpiScale);
        const auto* media = SelectedMedia();
        if (!media)
            return;

        if (PtInRectF(s_prevRect, p) && media->canSkipPrevious)
        {
            StatusMonitor_MediaSkipPrevious();
            return;
        }
        if (PtInRectF(s_playRect, p) && media->canPlayPause)
        {
            StatusMonitor_MediaTogglePlayPause();
            return;
        }
        if (PtInRectF(s_nextRect, p) && media->canSkipNext)
        {
            StatusMonitor_MediaSkipNext();
            return;
        }
        if (PtInRectF(s_progressRect, p) && media->hasTimeline && media->timelineEndTicks > media->timelineStartTicks)
        {
            const float t = Clamp01((p.x - s_progressRect.left) / (s_progressRect.right - s_progressRect.left));
            const long long target = media->timelineStartTicks
                + static_cast<long long>(static_cast<double>(media->timelineEndTicks - media->timelineStartTicks) * t);
            const int selected = SelectedIndex();
            if (selected >= 0 && selected < static_cast<int>(s_snapshot.mediaSessions.size()))
            {
                s_snapshot.mediaSessions[selected].timelinePositionTicks = target;
                s_snapshot.mediaSessions[selected].timelineSnapshotMs = GetTickCount64();
            }
            StatusMonitor_MediaSeekTo(target);
            Render();
            return;
        }

        const int count = min(static_cast<int>(s_snapshot.mediaSessions.size()), 32);
        for (int i = 0; i < count; ++i)
        {
            if (PtInRectF(s_dotRects[i], p))
            {
                SelectMediaIndex(i);
                return;
            }
        }
    }

    void HandleMouseWheel(WPARAM wParam, LPARAM lParam)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(s_hwnd, &pt);
        D2D1_POINT_2F p = D2D1::Point2F(pt.x / s_dpiScale, pt.y / s_dpiScale);
        if (!PointOverPagination(p))
            return;

        const int count = min(static_cast<int>(s_snapshot.mediaSessions.size()), 32);
        if (count <= 1)
            return;

        const int selected = SelectedIndex();
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const int next = selected + (delta < 0 ? 1 : -1);
        SelectMediaIndex((std::max)(0, (std::min)(count - 1, next)));
    }

    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_TIMER:
            if (wParam == ANIM_TIMER)
            {
                bool still = false;
                if (s_animDurMs > 0.0f)
                {
                    const float elapsed = static_cast<float>(GetTickCount64() - s_animStart);
                    const float rawT = Clamp01(elapsed / s_animDurMs);
                    const float easedT = s_animTarget > s_animFrom
                        ? EaseOutCubic(rawT)
                        : EaseInOutCubic(rawT);
                    s_anim = s_animFrom + (s_animTarget - s_animFrom) * easedT;
                    still = rawT < 1.0f;
                    if (!still)
                    {
                        s_anim = s_animTarget;
                        s_animDurMs = 0.0f;
                    }
                }

                if (s_dotAnimDurMs > 0.0f)
                {
                    const float elapsed = static_cast<float>(GetTickCount64() - s_dotAnimStart);
                    const float t = Clamp01(elapsed / s_dotAnimDurMs);
                    const float eased = EaseOutCubic(t);
                    const int selected = SelectedIndex();
                    for (int i = 0; i < 32; ++i)
                    {
                        const float target = i == selected ? 1.0f : 0.0f;
                        s_dotT[i] = s_dotFrom[i] + (target - s_dotFrom[i]) * eased;
                    }
                    if (t >= 1.0f)
                        s_dotAnimDurMs = 0.0f;
                    else
                        still = true;
                }

                Render();
                if (!still)
                {
                    KillTimer(hWnd, ANIM_TIMER);
                    if (s_animTarget <= 0.0f)
                    {
                        s_open = false;
                        ShowWindow(hWnd, SW_HIDE);
                        KillTimer(hWnd, WATCH_TIMER);
                        ClearMediaCaches();
                    }
                }
                return 0;
            }
            if (wParam == WATCH_TIMER)
            {
                const auto* media = SelectedMedia();
                const bool lightChanging = TickPlaybackLight();
                if (s_open && media && media->playbackState == MediaPlaybackState::Playing)
                {
                    s_playingPulsePhase += 0.18f;
                    if (s_playingPulsePhase > 6.2831853f)
                        s_playingPulsePhase -= 6.2831853f;
                    s_sliderWavePhase += 0.09f;
                    if (s_sliderWavePhase > 6.2831853f)
                        s_sliderWavePhase -= 6.2831853f;
                    Render();
                }
                else if (lightChanging)
                {
                    Render();
                }
                WatchCursor();
                return 0;
            }
            if (wParam == MEDIA_SWITCH_TIMER)
            {
                const float elapsed = static_cast<float>(GetTickCount64() - s_mediaSwitchStart);
                s_mediaSwitchT = Clamp01(elapsed / MEDIA_SWITCH_DURATION_MS);
                Render();
                if (s_mediaSwitchT >= 1.0f)
                {
                    s_mediaSwitchActive = false;
                    s_switchFromArt.Reset();
                    s_switchToArt.Reset();
                    KillTimer(hWnd, MEDIA_SWITCH_TIMER);
                    Render();
                }
                return 0;
            }
            if (wParam == HOVER_TIMER)
            {
                const bool changing = TickControlHover();
                Render();
                if (!changing)
                    KillTimer(hWnd, HOVER_TIMER);
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            UpdateHover(lParam);
            return 0;
        }

        case WM_LBUTTONDOWN:
            HandleClick(lParam);
            return 0;

        case WM_MOUSEWHEEL:
            HandleMouseWheel(wParam, lParam);
            return 0;

        case WM_MOUSELEAVE:
            s_hoverControl = -1;
            s_hoverDot = -1;
            SetTimer(s_hwnd, HOVER_TIMER, 16, nullptr);
            return 0;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            Render();
            EndPaint(hWnd, &ps);
            return 0;
        }
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

HWND MediaIsland_Create(HINSTANCE hInstance, HWND navbarHwnd)
{
    s_navbarHwnd = navbarHwnd;

    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = ISLAND_CLASS;
    RegisterClassExW(&wcex);

    s_dpiScale = static_cast<float>(GetDpiForWindow(navbarHwnd)) / 96.0f;
    const int w = MulDiv(static_cast<int>(EXP_W), static_cast<int>(s_dpiScale * 96.0f), 96);
    const int h = MulDiv(static_cast<int>(EXP_H), static_cast<int>(s_dpiScale * 96.0f), 96);

    s_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        ISLAND_CLASS, L"KrevonMediaIsland", WS_POPUP,
        0, 0, w, h,
        navbarHwnd, nullptr, hInstance, nullptr);
    if (!s_hwnd)
        return nullptr;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &s_d3d, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &s_d3d, nullptr, nullptr);
    if (FAILED(hr)) return s_hwnd;

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), reinterpret_cast<void**>(s_fac.GetAddressOf()));
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_wicFactory));
    s_fac->CreateRoundedRectangleGeometry(
        D2D1::RoundedRect(D2D1::RectF(20.0f, 24.0f, 108.0f, 112.0f), 14.0f, 14.0f),
        &s_coverGeometry);
    ComPtr<IDXGIDevice> dxgiDev;
    s_d3d.As(&dxgiDev);
    s_fac->CreateDevice(dxgiDev.Get(), &s_d2dDev);
    ComPtr<ID2D1DeviceContext> baseCtx;
    s_d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &baseCtx);
    baseCtx.As(&s_ctx);
    s_ctx->SetDpi(s_dpiScale * 96.0f, s_dpiScale * 96.0f);
    s_ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    dxgiDev->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = static_cast<UINT>(w);
    scd.Height = static_cast<UINT>(h);
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    dxgiFactory->CreateSwapChainForComposition(s_d3d.Get(), &scd, nullptr, &s_swap);
    RebuildTarget();

    DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(&s_dcompDevice));
    s_dcompDevice->CreateTargetForHwnd(s_hwnd, TRUE, &s_dcompTarget);
    s_dcompDevice->CreateVisual(&s_dcompVisual);
    s_dcompVisual->SetContent(s_swap.Get());
    s_dcompTarget->SetRoot(s_dcompVisual.Get());
    s_dcompDevice->Commit();

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(s_dw.GetAddressOf()));
    auto createFormat = [&](float size, DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>& out, ComPtr<IDWriteInlineObject>& ellipsis) {
        s_dw->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &out);
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming = {};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        s_dw->CreateEllipsisTrimmingSign(out.Get(), &ellipsis);
        out->SetTrimming(&trimming, ellipsis.Get());
    };
    createFormat(10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, s_tfApp, s_appEllipsis);
    createFormat(16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, s_tfTitle, s_titleEllipsis);
    createFormat(12.0f, DWRITE_FONT_WEIGHT_NORMAL, s_tfSub, s_subEllipsis);
    createFormat(18.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, s_tfTime, s_timeEllipsis);
    s_tfApp->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    s_tfTime->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    s_tfTime->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    s_ctx->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &s_brBg);
    s_ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &s_brWhite);
    s_ctx->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.58f, 1), &s_brGrey);
    s_ctx->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.11f, 1), &s_brDim);
    s_ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.45f, 0.38f, 1), &s_brAccent);

    LoadSvg(L"play.svg", 16.0f, s_svgPlay);
    LoadSvg(L"pause.svg", 16.0f, s_svgPause);
    LoadSvg(L"fast-forward.svg", 16.0f, s_svgNext);
    LoadSvg(L"rewind.svg", 16.0f, s_svgPrev);
    return s_hwnd;
}

void MediaIsland_Show(const StatusSnapshot& snapshot)
{
    if (!s_hwnd)
        return;

    if (s_open)
    {
        if (s_animTarget < 1.0f || s_animDurMs > 0.0f)
            StartAnim(1.0f, ANIM_IN_MS);
        SetTimer(s_hwnd, WATCH_TIMER, WATCH_TIMER_MS, nullptr);
        return;
    }

    s_snapshot = snapshot;
    UpdateCoverArt();
    const int selected = SelectedIndex();
    for (int i = 0; i < 32; ++i)
        s_dotT[i] = i == selected ? 1.0f : 0.0f;

    if (!s_open)
    {
        PositionWindow();
        s_mediaSwitchActive = false;
        s_mediaSwitchT = 1.0f;
        s_switchFromArt.Reset();
        s_switchToArt.Reset();
        s_open = true;
        ShowWindow(s_hwnd, SW_SHOWNOACTIVATE);
    }

    if (s_animTarget < 1.0f || s_animDurMs <= 0.0f)
        StartAnim(1.0f, ANIM_IN_MS);
    SetTimer(s_hwnd, WATCH_TIMER, WATCH_TIMER_MS, nullptr);
    Render();
}

void MediaIsland_UpdateSnapshot(const StatusSnapshot& snapshot)
{
    if (!s_open && s_animTarget <= 0.0f)
        return;

    const int before = SelectedIndex();
    const auto* beforePtr = SelectedMedia();
    const MediaSessionInfo beforeMedia = beforePtr ? *beforePtr : MediaSessionInfo{};
    s_snapshot = snapshot;
    const int after = SelectedIndex();
    const auto* afterPtr = SelectedMedia();
    UpdateCoverArt();
    if (before != after)
    {
        if (beforePtr && afterPtr)
            StartMediaSwitch(beforeMedia, *afterPtr);
        StartDotAnim();
    }
    if (s_open)
    {
        ArmPlaybackLightTimer();
        Render();
    }
}

void MediaIsland_TickClock()
{
    if (s_open && s_snapshot.mediaSessions.empty())
        Render();
}

bool MediaIsland_IsOpen()
{
    return s_open;
}

void MediaIsland_Destroy()
{
    if (s_hwnd)
    {
        KillTimer(s_hwnd, ANIM_TIMER);
        KillTimer(s_hwnd, WATCH_TIMER);
        KillTimer(s_hwnd, MEDIA_SWITCH_TIMER);
        KillTimer(s_hwnd, HOVER_TIMER);
        DestroyWindow(s_hwnd);
        s_hwnd = nullptr;
    }
    ClearMediaCaches();
    s_svgPlay.Reset(); s_svgPause.Reset(); s_svgNext.Reset(); s_svgPrev.Reset();
    s_brBg.Reset(); s_brWhite.Reset(); s_brGrey.Reset(); s_brDim.Reset(); s_brAccent.Reset();
    s_appEllipsis.Reset(); s_titleEllipsis.Reset(); s_subEllipsis.Reset(); s_timeEllipsis.Reset();
    s_tfApp.Reset(); s_tfTitle.Reset(); s_tfSub.Reset(); s_tfTime.Reset(); s_dw.Reset();
    s_coverGeometry.Reset();
    s_target.Reset(); s_ctx.Reset(); s_d2dDev.Reset(); s_fac.Reset();
    s_wicFactory.Reset();
    s_dcompVisual.Reset(); s_dcompTarget.Reset(); s_dcompDevice.Reset();
    s_swap.Reset(); s_d3d.Reset();
}
