// drawer_init.cpp — D3D/D2D/DWrite device setup, SVG loading, avatar, username.

#include "../state/drawer_state.h"
#include "../render/drawer_render.h"
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Foundation.h>

static winrt::Windows::UI::ViewManagement::UISettings g_uiSettings{ nullptr };

// ── Path helpers ──────────────────────────────────────────────────────────────

static void IconPath(const wchar_t* fn, wchar_t* out, DWORD len)
{
    GetModuleFileNameW(nullptr, out, len);
    wchar_t* sl = wcsrchr(out, L'\\');
    if (sl) *(sl + 1) = L'\0';
    wcsncat_s(out, len, L"assets\\icons\\", _TRUNCATE);
    wcsncat_s(out, len, fn, _TRUNCATE);
}

// ── SVG loading ───────────────────────────────────────────────────────────────

static HRESULT LoadSvgAt(const wchar_t* fn, float sz, ComPtr<ID2D1SvgDocument>& out, const wchar_t* color = L"white")
{
    wchar_t path[MAX_PATH];
    IconPath(fn, path, MAX_PATH);

    IStream* raw = nullptr;
    HRESULT hr = SHCreateStreamOnFileW(path, STGM_READ | STGM_SHARE_DENY_WRITE, &raw);
    if (FAILED(hr)) return hr;
    ComPtr<IStream> stream(raw);

    hr = g_ctx->CreateSvgDocument(stream.Get(), D2D1::SizeF(sz, sz), &out);
    if (FAILED(hr)) return hr;

    ComPtr<ID2D1SvgElement> root;
    out->GetRoot(&root);
    out->SetViewportSize(D2D1::SizeF(sz, sz));
    root->SetAttributeValue(L"width",  sz);
    root->SetAttributeValue(L"height", sz);
    root->SetAttributeValue(L"color",  D2D1_SVG_ATTRIBUTE_STRING_TYPE_SVG, color);
    root->SetAttributeValue(L"stroke", D2D1_SVG_ATTRIBUTE_STRING_TYPE_SVG, color);
    return S_OK;
}

// ── Swap chain target rebuild (called from drawer.cpp on resize too) ──────────

HRESULT RebuildTarget()
{
    g_ctx->SetTarget(nullptr);
    g_target.Reset();

    ComPtr<IDXGISurface> buf;
    HRESULT hr = g_swap->GetBuffer(0, IID_PPV_ARGS(&buf));
    if (FAILED(hr)) return hr;

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr = g_ctx->CreateBitmapFromDxgiSurface(buf.Get(), &props, &g_target);
    if (FAILED(hr)) return hr;

    g_ctx->SetTarget(g_target.Get());
    return S_OK;
}

// ── DrawSvg helper (also used by render) ─────────────────────────────────────

void DrawSvg(ID2D1SvgDocument* svg, float cx, float cy, float sz)
{
    if (!svg) return;
    D2D1_SIZE_F vp = svg->GetViewportSize();
    float sc = sz / (vp.width > 0.0f ? vp.width : 15.0f);
    
    D2D1_MATRIX_3X2_F oldTransform;
    g_ctx->GetTransform(&oldTransform);
    
    g_ctx->SetTransform(
        D2D1::Matrix3x2F::Scale(sc, sc, D2D1::Point2F(0, 0)) *
        D2D1::Matrix3x2F::Translation(cx - sz * 0.5f, cy - sz * 0.5f) *
        oldTransform
    );
    g_ctx->DrawSvgDocument(svg);
    g_ctx->SetTransform(oldTransform);
}

void DrawSvgColored(ID2D1SvgDocument* svg, float cx, float cy, float sz, D2D1_COLOR_F color)
{
    if (!svg) return;
    ComPtr<ID2D1SvgElement> root;
    svg->GetRoot(&root);
    if (root)
    {
        root->SetAttributeValue(L"stroke", D2D1_SVG_ATTRIBUTE_POD_TYPE_COLOR, &color, sizeof(color));
    }
    DrawSvg(svg, cx, cy, sz);
}

// ── User avatar loading ───────────────────────────────────────────────────────
// Tries FOLDERID_AccountPictures for a plain .png (local accounts sometimes have these).
// Falls back gracefully to null → user-round.svg icon is used in Render().

static void TryLoadAvatar()
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return;

    DWORD len = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &len);
    std::vector<BYTE> buf(len);
    if (!GetTokenInformation(hToken, TokenUser, buf.data(), len, &len))
    {
        CloseHandle(hToken);
        return;
    }

    PTOKEN_USER pUser = reinterpret_cast<PTOKEN_USER>(buf.data());
    LPWSTR sidStr = nullptr;
    if (!ConvertSidToStringSidW(pUser->User.Sid, &sidStr))
    {
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);

    std::wstring regPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\";
    regPath += sidStr;
    LocalFree(sidStr);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS)
        return;

    const wchar_t* sizes[] = { L"Image1080", L"Image448", L"Image240", L"Image192", L"Image96" };
    wchar_t fullPath[MAX_PATH] = {};
    for (const auto& szName : sizes)
    {
        DWORD type = 0;
        DWORD dataSize = sizeof(fullPath);
        if (RegQueryValueExW(hKey, szName, nullptr, &type, reinterpret_cast<LPBYTE>(fullPath), &dataSize) == ERROR_SUCCESS)
        {
            if (type == REG_SZ && fullPath[0] != L'\0')
                break;
        }
        fullPath[0] = L'\0';
    }
    RegCloseKey(hKey);

    if (fullPath[0] == L'\0') return;

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return;

    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(fullPath, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &dec))) return;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return;

    ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(&conv);
    conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    const UINT avatarPx = static_cast<UINT>(AVATAR_R * 2.0f * g_dpiScale + 0.5f);
    ComPtr<IWICBitmapScaler> scaler;
    wic->CreateBitmapScaler(&scaler);
    scaler->Initialize(conv.Get(), avatarPx, avatarPx, WICBitmapInterpolationModeFant);

    ComPtr<IWICFormatConverter> scaledConv;
    wic->CreateFormatConverter(&scaledConv);
    scaledConv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_ctx->CreateBitmapFromWicBitmap(scaledConv.Get(), &bmpProps, &g_avatarBmp);
}

// ── Username query ────────────────────────────────────────────────────────────

static void QueryUsername()
{
    wchar_t buf[UNLEN + 1] = {};
    DWORD len = UNLEN + 1;
    g_username = (GetUserNameW(buf, &len) && len > 1) ? buf : L"User";
}

// ── Main init ─────────────────────────────────────────────────────────────────

void UpdateAccentColors(D2D1_COLOR_F accent)
{
    g_accentColor = accent;
    // Generate variants based on user's example:
    // Base: #D33F26
    // Slider & Icons (Light Pastel): #ED9370 (~35% white mix)
    // Pill Icon Circle (Mid Dark): #50251F (~38% accent mix)
    // Pill Background (Very Dark): #1F0E0C (~15% accent mix)
    
    D2D1_COLOR_F lightVariant = LerpColor(accent, CLR_WHITE, 0.35f);
    
    g_clrPill    = LerpColor(CLR_BG, accent, 0.15f);
    g_clrPillHov = LerpColor(CLR_BG, accent, 0.22f); // Between pill and icon circle
    g_clrPillIco = LerpColor(CLR_BG, accent, 0.38f);
    g_clrFill    = lightVariant;

    if (g_brPill)    g_brPill->SetColor(g_clrPill);
    if (g_brPillHov) g_brPillHov->SetColor(g_clrPillHov);
    if (g_brPillIco) g_brPillIco->SetColor(g_clrPillIco);
    if (g_brFill)    g_brFill->SetColor(g_clrFill);
}

HRESULT DrawerInit(HWND hWnd)
{
    g_dpiScale = static_cast<float>(GetDpiForWindow(hWnd)) / 96.0f;

    // Fetch System Accent Color
    try
    {
        g_uiSettings = winrt::Windows::UI::ViewManagement::UISettings();
        auto color = g_uiSettings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
        UpdateAccentColors(D2D1::ColorF(color.R / 255.0f, color.G / 255.0f, color.B / 255.0f, color.A / 255.0f));

        g_uiSettings.ColorValuesChanged([](const winrt::Windows::UI::ViewManagement::UISettings& sender, const winrt::Windows::Foundation::IInspectable&) {
            auto newColor = sender.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
            UpdateAccentColors(D2D1::ColorF(newColor.R / 255.0f, newColor.G / 255.0f, newColor.B / 255.0f, newColor.A / 255.0f));
            if (g_drawerHwnd && g_open) {
                InvalidateRect(g_drawerHwnd, nullptr, FALSE);
            }
        });
    }
    catch (...)
    {
        // Fallback to default Windows blue if WinRT is unavailable
        UpdateAccentColors(D2D1::ColorF(0.0f, 0.47f, 0.83f, 1.0f));
    }

    // D3D11 device (hardware with BGRA support; WARP fallback)
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &g_d3d, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &g_d3d, nullptr, nullptr);
    if (FAILED(hr)) return hr;

    // D2D factory + device
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        reinterpret_cast<void**>(g_fac.GetAddressOf()));
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIDevice> dxgiDev;
    g_d3d.As(&dxgiDev);
    hr = g_fac->CreateDevice(dxgiDev.Get(), &g_d2dDev);
    if (FAILED(hr)) return hr;

    ComPtr<ID2D1DeviceContext> baseCtx;
    g_d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &baseCtx);
    baseCtx.As(&g_ctx);

    const float dpi = g_dpiScale * 96.0f;
    g_ctx->SetDpi(dpi, dpi);
    g_ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Swap chain (PREMULTIPLIED alpha for DWM composition)
    ComPtr<IDXGIAdapter>  adapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    dxgiDev->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));

    RECT rc; GetClientRect(hWnd, &rc);
    const UINT w = static_cast<UINT>(max(rc.right - rc.left, 1));
    const UINT h = static_cast<UINT>(max(rc.bottom - rc.top,  1));

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width        = w;   scd.Height       = h;
    scd.Format       = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount  = 2;
    scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode    = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = dxgiFactory->CreateSwapChainForComposition(g_d3d.Get(), &scd, nullptr, &g_swap);
    if (FAILED(hr)) return hr;

    hr = RebuildTarget();
    if (FAILED(hr)) return hr;

    // Bind swap chain to the HWND via DirectComposition.
    // Without this the Present calls go to an unconnected composition surface and
    // nothing is rendered to the window.
    hr = DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(&g_dcompDevice));
    if (FAILED(hr)) return hr;

    // TRUE = topmost visual layer (above DWM chrome)
    hr = g_dcompDevice->CreateTargetForHwnd(hWnd, TRUE, &g_dcompTarget);
    if (FAILED(hr)) return hr;

    hr = g_dcompDevice->CreateVisual(&g_dcompVisual);
    if (FAILED(hr)) return hr;

    g_dcompVisual->SetContent(g_swap.Get());
    g_dcompTarget->SetRoot(g_dcompVisual.Get());
    g_dcompDevice->Commit();

    // DWrite
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_dw.GetAddressOf()));
    g_dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"", &g_tfName);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 9.5f, L"", &g_tfSub);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"", &g_tfPct);
    if (g_tfPct) g_tfPct->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 9.5f, L"", &g_tfSndHdg);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"", &g_tfNotifHeader);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"", &g_tfNotifApp);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"", &g_tfNotifTitle);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"", &g_tfNotifBody);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"", &g_tfNotifButton);
    if (g_tfNotifButton)
    {
        g_tfNotifButton->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_tfNotifButton->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Brushes
    auto mkBr = [&](D2D1_COLOR_F c, ComPtr<ID2D1SolidColorBrush>& br)
        { g_ctx->CreateSolidColorBrush(c, &br); };
    mkBr(CLR_WHITE,    g_brWhite);   mkBr(CLR_GREY,     g_brGrey);
    mkBr(CLR_BTN,      g_brBtn);     mkBr(CLR_BTN_HOV,  g_brBtnHov);
    mkBr(g_clrPill,    g_brPill);    mkBr(g_clrPillHov, g_brPillHov);
    mkBr(g_clrPillIco, g_brPillIco); mkBr(CLR_TRACK,    g_brTrack);
    mkBr(g_clrFill,    g_brFill);    mkBr(CLR_THUMB,    g_brThumb);
    mkBr(CLR_BG,       g_brBg);      mkBr(CLR_SEP,      g_brSep);

    // Action button / drawer-specific icons
    LoadSvgAt(L"user-round.svg",  AVATAR_R * 1.1f, g_svgUserRound);
    LoadSvgAt(L"camera.svg",      BTN_ICON_SZ,     g_svgCamera);
    LoadSvgAt(L"settings.svg",    BTN_ICON_SZ,     g_svgSettings);
    LoadSvgAt(L"lock.svg",        BTN_ICON_SZ,     g_svgLock);
    LoadSvgAt(L"power.svg",       BTN_ICON_SZ,     g_svgPower);
    LoadSvgAt(L"settings-2.svg",  VOL_ICON_SZ,     g_svgSettings2);
    LoadSvgAt(L"chevron-right.svg", 12.0f,          g_svgChevron);
    LoadSvgAt(L"chevron-left.svg",  12.0f,          g_svgChevronLeft);
    LoadSvgAt(L"moon.svg",        BTN_ICON_SZ,     g_svgMoon);
    LoadSvgAt(L"rotate-ccw.svg",  BTN_ICON_SZ,     g_svgRestart);
    LoadSvgAt(L"power-off.svg",   BTN_ICON_SZ,     g_svgPowerOff);
    LoadSvgAt(L"check.svg",       12.0f,           g_svgCheck);
    LoadSvgAt(L"refresh-cw.svg",  16.0f,           g_svgRefresh);
    LoadSvgAt(L"key.svg",         16.0f,           g_svgKey);
    LoadSvgAt(L"x.svg",           14.0f,           g_svgX);
    LoadSvgAt(L"trash-2.svg",     14.0f,           g_svgTrash);

    // Status icons (re-loaded at pill/volume icon size)
    LoadSvgAt(L"wifi.svg",              16.0f, g_svgWifi[0]);
    LoadSvgAt(L"wifi-high.svg",         16.0f, g_svgWifi[1]);
    LoadSvgAt(L"wifi-low.svg",          16.0f, g_svgWifi[2]);
    LoadSvgAt(L"wifi-zero.svg",         16.0f, g_svgWifi[3]);
    LoadSvgAt(L"globe-off.svg",         16.0f, g_svgWifi[4]);
    LoadSvgAt(L"bluetooth-connected.svg", 16.0f, g_svgBt[0]);
    LoadSvgAt(L"bluetooth.svg",         16.0f, g_svgBt[1]);
    LoadSvgAt(L"bluetooth-off.svg",     16.0f, g_svgBt[2]);
    LoadSvgAt(L"volume-x.svg",   VOL_ICON_SZ, g_svgVol[0]);
    LoadSvgAt(L"volume-off.svg", VOL_ICON_SZ, g_svgVol[1]);
    LoadSvgAt(L"volume.svg",     VOL_ICON_SZ, g_svgVol[2]);
    LoadSvgAt(L"volume-1.svg",   VOL_ICON_SZ, g_svgVol[3]);
    LoadSvgAt(L"volume-2.svg",   VOL_ICON_SZ, g_svgVol[4]);
    LoadSvgAt(L"speaker.svg",    16.0f,       g_svgSpeaker);
    LoadSvgAt(L"headphones.svg", 16.0f,       g_svgHeadphones);

    QueryUsername();
    TryLoadAvatar();
    return S_OK;
}
