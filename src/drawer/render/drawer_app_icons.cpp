// drawer_app_icons.cpp - App icon extraction/cache for the sound mixer.
#include "drawer_render.h"

ComPtr<ID2D1Bitmap1> GetAppIcon(DWORD processId, const std::wstring& name)
{
    if (processId == 0) return nullptr;
    auto it = g_appIcons.find(processId);
    if (it != g_appIcons.end()) return it->second;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) {
        g_appIcons[processId] = nullptr;
        return nullptr;
    }

    WCHAR path[MAX_PATH];
    DWORD size = MAX_PATH;
    ComPtr<ID2D1Bitmap1> bmp;
    if (QueryFullProcessImageNameW(hProcess, 0, path, &size))
    {
        HICON hIcon = nullptr;
        ExtractIconExW(path, 0, nullptr, &hIcon, 1);
        if (!hIcon) {
            SHFILEINFOW sfi = {};
            if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
                hIcon = sfi.hIcon;
            }
        }
        if (hIcon)
        {
            ComPtr<IWICImagingFactory> wic;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
            {
                ComPtr<IWICBitmap> wicBmp;
                if (SUCCEEDED(wic->CreateBitmapFromHICON(hIcon, &wicBmp)))
                {
                    ComPtr<IWICFormatConverter> conv;
                    wic->CreateFormatConverter(&conv);
                    conv->Initialize(wicBmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

                    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                        D2D1_BITMAP_OPTIONS_NONE,
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                    g_ctx->CreateBitmapFromWicBitmap(conv.Get(), &props, &bmp);
                }
            }
            DestroyIcon(hIcon);
        }
    }
    CloseHandle(hProcess);

    g_appIcons[processId] = bmp;
    return bmp;
}
