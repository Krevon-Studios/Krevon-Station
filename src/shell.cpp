#include "shell.h"
#include "window.h"
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

static HICON s_hTrayIcon = nullptr;

// Load a PNG from disk and produce an HICON scaled to the system small-icon size.
// Falls back to IDI_APPLICATION if anything fails.
static HICON LoadIconFromPng(const wchar_t* path)
{
    const int size = GetSystemMetrics(SM_CXSMICON);

    ComPtr<IWICImagingFactory> pFactory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory))))
        return nullptr;

    ComPtr<IWICBitmapDecoder> pDecoder;
    if (FAILED(pFactory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                   WICDecodeMetadataCacheOnLoad,
                                                   &pDecoder)))
        return nullptr;

    ComPtr<IWICBitmapFrameDecode> pFrame;
    if (FAILED(pDecoder->GetFrame(0, &pFrame)))
        return nullptr;

    ComPtr<IWICBitmapScaler> pScaler;
    pFactory->CreateBitmapScaler(&pScaler);
    pScaler->Initialize(pFrame.Get(), size, size, WICBitmapInterpolationModeFant);

    ComPtr<IWICFormatConverter> pConverter;
    pFactory->CreateFormatConverter(&pConverter);
    pConverter->Initialize(pScaler.Get(), GUID_WICPixelFormat32bppBGRA,
                           WICBitmapDitherTypeNone, nullptr, 0.0,
                           WICBitmapPaletteTypeCustom);

    const UINT stride  = size * 4;
    const UINT bufSize = stride * size;
    auto pixels = std::make_unique<BYTE[]>(bufSize);
    if (FAILED(pConverter->CopyPixels(nullptr, stride, bufSize, pixels.get())))
        return nullptr;

    // 32bpp DIB with alpha channel
    BITMAPV5HEADER bi    = {};
    bi.bV5Size           = sizeof(bi);
    bi.bV5Width          = size;
    bi.bV5Height         = -size;  // top-down
    bi.bV5Planes         = 1;
    bi.bV5BitCount       = 32;
    bi.bV5Compression    = BI_BITFIELDS;
    bi.bV5RedMask        = 0x00FF0000;
    bi.bV5GreenMask      = 0x0000FF00;
    bi.bV5BlueMask       = 0x000000FF;
    bi.bV5AlphaMask      = 0xFF000000;

    void*   pBits = nullptr;
    HDC     hdc   = GetDC(nullptr);
    HBITMAP hBmp  = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi),
                                     DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);

    if (!hBmp) return nullptr;
    memcpy(pBits, pixels.get(), bufSize);

    HBITMAP  hMask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii    = { TRUE, 0, 0, hMask, hBmp };
    HICON    hIcon = CreateIconIndirect(&ii);

    DeleteObject(hMask);
    DeleteObject(hBmp);
    return hIcon;
}

// Build path: <exe_dir>\assets\<filename>
static void GetAssetPath(const wchar_t* filename, wchar_t* out, DWORD outLen)
{
    GetModuleFileNameW(nullptr, out, outLen);
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcsncat_s(out, outLen, L"assets\\", 7);
    wcsncat_s(out, outLen, filename, _TRUNCATE);
}

// ── AppBar ────────────────────────────────────────────────────────────────────

void AppBar_SetPos(HWND hWnd)
{
    UINT dpi    = GetDpiForWindow(hWnd);
    int  height = MulDiv(NAVBAR_HEIGHT_DIP, dpi, 96);

    HMONITOR    hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi   = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    APPBARDATA abd = { sizeof(abd), hWnd };
    abd.uEdge     = ABE_TOP;
    abd.rc.left   = mi.rcMonitor.left;
    abd.rc.right  = mi.rcMonitor.right;
    abd.rc.top    = mi.rcMonitor.top;
    abd.rc.bottom = mi.rcMonitor.top + height;

    SHAppBarMessage(ABM_QUERYPOS, &abd);
    abd.rc.bottom = abd.rc.top + height;
    SHAppBarMessage(ABM_SETPOS,  &abd);

    SetWindowPos(
        hWnd, HWND_TOPMOST,
        abd.rc.left, abd.rc.top,
        abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top,
        SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void AppBar_Register(HWND hWnd)
{
    APPBARDATA abd       = { sizeof(abd), hWnd };
    abd.uCallbackMessage = WM_APPBAR_CALLBACK;
    SHAppBarMessage(ABM_NEW, &abd);
    AppBar_SetPos(hWnd);
}

void AppBar_Remove(HWND hWnd)
{
    APPBARDATA abd = { sizeof(abd), hWnd };
    SHAppBarMessage(ABM_REMOVE, &abd);
}

// ── Tray ──────────────────────────────────────────────────────────────────────

void Tray_Add(HWND hWnd)
{
    wchar_t iconPath[MAX_PATH];
    GetAssetPath(L"logo_transparent.png", iconPath, MAX_PATH);
    s_hTrayIcon = LoadIconFromPng(iconPath);

    NOTIFYICONDATAW nid  = { sizeof(nid) };
    nid.hWnd             = hWnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = s_hTrayIcon ? s_hTrayIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"KrevonStation");
    Shell_NotifyIconW(NIM_ADD, &nid);

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void Tray_Remove(HWND hWnd)
{
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd            = hWnd;
    nid.uID             = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    if (s_hTrayIcon)
    {
        DestroyIcon(s_hTrayIcon);
        s_hTrayIcon = nullptr;
    }
}
