#include "desktop_preview.h"

#include "virtual_desktop.h"
#include "window.h"

#include <dwmapi.h>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

struct PreviewThumb
{
    HWND hwnd = nullptr;
    HTHUMBNAIL thumb = nullptr;
};

static constexpr wchar_t PREVIEW_CLASS_NAME[] = L"KrevonDesktopPreview";
static HINSTANCE s_hInstance = nullptr;
static HWND s_navbarHwnd = nullptr;
static HWND s_previewHwnd = nullptr;
static std::vector<PreviewThumb> s_thumbs;
static HWND s_sourceHwnd = nullptr;
static int s_desktopIndex = -1;
static bool s_visible = false;
static bool s_animating = false;
static bool s_hiding = false;
static int s_targetX = 0;
static int s_targetY = 0;
static int s_animFromY = 0;
static int s_animToY = 0;
static BYTE s_animFromAlpha = 0;
static BYTE s_animToAlpha = 255;
static ULONGLONG s_animStartMs = 0;
static float s_animDurationMs = 0.0f;

static constexpr int PREVIEW_W = 260;
static constexpr int PREVIEW_H = 150;
static constexpr int PREVIEW_PAD = 10;
static constexpr int PREVIEW_SLIDE_PX = 12;
static constexpr UINT_PTR PREVIEW_ANIM_TIMER = 1;
static constexpr UINT PREVIEW_ANIM_TIMER_MS = 16;
static constexpr float PREVIEW_SHOW_MS = 140.0f;
static constexpr float PREVIEW_HIDE_MS = 110.0f;

static void ClearThumbs()
{
    for (PreviewThumb& item : s_thumbs)
    {
        if (item.thumb)
            DwmUnregisterThumbnail(item.thumb);
    }
    s_thumbs.clear();
}

static float Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float EaseOutCubic(float t)
{
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static bool IsAltTabWindow(HWND hwnd)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd))
        return false;
    if (hwnd == s_navbarHwnd || hwnd == s_previewHwnd)
        return false;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd)
        return false;

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW)
        return false;

    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner && !(exStyle & WS_EX_APPWINDOW))
        return false;

    wchar_t title[2] = {};
    if (GetWindowTextW(hwnd, title, ARRAYSIZE(title)) <= 0)
        return false;

    return true;
}

static BOOL CALLBACK EnumWindowsForDesktop(HWND hwnd, LPARAM lParam)
{
    auto* source = reinterpret_cast<HWND*>(lParam);
    if (!IsAltTabWindow(hwnd))
        return TRUE;

    GUID targetId = {};
    if (!VirtualDesktop_GetDesktopId(s_desktopIndex, &targetId))
        return FALSE;

    GUID windowId = {};
    if (!VirtualDesktop_GetWindowDesktopId(hwnd, &windowId))
        return TRUE;

    if (IsEqualGUID(targetId, windowId))
    {
        *source = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND CollectDesktopSource(int desktopIndex)
{
    s_desktopIndex = desktopIndex;
    HWND source = nullptr;
    EnumWindows(EnumWindowsForDesktop, reinterpret_cast<LPARAM>(&source));
    if (!source)
        source = FindWindowW(L"Progman", nullptr);
    if (!source)
        source = GetShellWindow();
    return source;
}

static void RegisterThumb()
{
    ClearThumbs();
    if (!s_previewHwnd || !s_sourceHwnd)
        return;

    HTHUMBNAIL thumb = nullptr;
    HRESULT hr = DwmRegisterThumbnail(s_previewHwnd, s_sourceHwnd, &thumb);
    if (FAILED(hr) || !thumb)
        return;

    DWM_THUMBNAIL_PROPERTIES props = {};
    props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    props.fVisible = TRUE;
    props.opacity = 255;
    props.fSourceClientAreaOnly = FALSE;
    props.rcDestination = { PREVIEW_PAD, PREVIEW_PAD, PREVIEW_W - PREVIEW_PAD, PREVIEW_H - PREVIEW_PAD };
    DwmUpdateThumbnailProperties(thumb, &props);
    s_thumbs.push_back({ s_sourceHwnd, thumb });
}

static void BeginPreviewAnimation(bool show)
{
    if (!s_previewHwnd)
        return;

    s_animating = true;
    s_hiding = !show;
    s_animStartMs = GetTickCount64();
    s_animDurationMs = show ? PREVIEW_SHOW_MS : PREVIEW_HIDE_MS;
    s_animFromY = show ? s_targetY - PREVIEW_SLIDE_PX : s_targetY;
    s_animToY = show ? s_targetY : s_targetY - PREVIEW_SLIDE_PX;
    s_animFromAlpha = show ? 0 : 255;
    s_animToAlpha = show ? 255 : 0;

    if (show)
    {
        SetLayeredWindowAttributes(s_previewHwnd, 0, 0, LWA_ALPHA);
        SetWindowPos(s_previewHwnd, HWND_TOPMOST, s_targetX, s_animFromY, PREVIEW_W, PREVIEW_H,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    SetTimer(s_previewHwnd, PREVIEW_ANIM_TIMER, PREVIEW_ANIM_TIMER_MS, nullptr);
}

static void TickPreviewAnimation()
{
    if (!s_animating || !s_previewHwnd)
        return;

    const float elapsed = static_cast<float>(GetTickCount64() - s_animStartMs);
    const float t = Clamp01(elapsed / s_animDurationMs);
    const float eased = EaseOutCubic(t);
    const int y = s_animFromY + static_cast<int>((s_animToY - s_animFromY) * eased + 0.5f);
    const BYTE alpha = static_cast<BYTE>(s_animFromAlpha + (s_animToAlpha - s_animFromAlpha) * eased);

    SetLayeredWindowAttributes(s_previewHwnd, 0, alpha, LWA_ALPHA);
    SetWindowPos(s_previewHwnd, HWND_TOPMOST, s_targetX, y, PREVIEW_W, PREVIEW_H,
        SWP_NOACTIVATE | SWP_NOZORDER);

    if (t >= 1.0f)
    {
        KillTimer(s_previewHwnd, PREVIEW_ANIM_TIMER);
        s_animating = false;
        SetLayeredWindowAttributes(s_previewHwnd, 0, s_animToAlpha, LWA_ALPHA);
        SetWindowPos(s_previewHwnd, HWND_TOPMOST, s_targetX, s_animToY, PREVIEW_W, PREVIEW_H,
            SWP_NOACTIVATE | SWP_NOZORDER);

        if (s_hiding)
        {
            ClearThumbs();
            ShowWindow(s_previewHwnd, SW_HIDE);
            s_visible = false;
            s_desktopIndex = -1;
            s_sourceHwnd = nullptr;
        }
    }
}

static LRESULT CALLBACK PreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TIMER:
        if (wParam == PREVIEW_ANIM_TIMER)
        {
            TickPreviewAnimation();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(10, 10, 10));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        if (s_thumbs.empty())
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(150, 150, 150));
            DrawTextW(hdc, L"No preview", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

HRESULT DesktopPreview_Init(HINSTANCE hInstance, HWND navbarHwnd)
{
    s_hInstance = hInstance;
    s_navbarHwnd = navbarHwnd;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = PreviewWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = PREVIEW_CLASS_NAME;
    RegisterClassExW(&wc);

    s_previewHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        PREVIEW_CLASS_NAME,
        L"Krevon Desktop Preview",
        WS_POPUP,
        0, 0, PREVIEW_W, PREVIEW_H,
        navbarHwnd, nullptr, hInstance, nullptr);

    if (!s_previewHwnd)
        return HRESULT_FROM_WIN32(GetLastError());

    HRGN rgn = CreateRoundRectRgn(0, 0, PREVIEW_W + 1, PREVIEW_H + 1, 14, 14);
    SetWindowRgn(s_previewHwnd, rgn, TRUE);
    return S_OK;
}

void DesktopPreview_Shutdown()
{
    ClearThumbs();
    if (s_previewHwnd)
    {
        DestroyWindow(s_previewHwnd);
        s_previewHwnd = nullptr;
    }
    if (s_hInstance)
        UnregisterClassW(PREVIEW_CLASS_NAME, s_hInstance);
    s_hInstance = nullptr;
    s_navbarHwnd = nullptr;
    s_sourceHwnd = nullptr;
    s_desktopIndex = -1;
    s_visible = false;
    s_animating = false;
}

void DesktopPreview_Show(int desktopIndex, LONG dotXpx, LONG dotYpx)
{
    if (!s_previewHwnd)
        return;

    const VirtualDesktopSnapshot snapshot = VirtualDesktop_GetSnapshot();
    if (snapshot.available && desktopIndex == snapshot.currentIndex)
    {
        DesktopPreview_Hide();
        return;
    }

    const bool alreadyVisible = s_visible && s_desktopIndex == desktopIndex;
    if (alreadyVisible)
        return;

    s_sourceHwnd = CollectDesktopSource(desktopIndex);

    RECT navRect;
    GetWindowRect(s_navbarHwnd, &navRect);
    const int dpi = GetDpiForWindow(s_navbarHwnd);
    const int navH = MulDiv(NAVBAR_HEIGHT_DIP, dpi, 96);
    s_targetX = max(4, navRect.left + dotXpx - PREVIEW_W / 2);
    s_targetY = navRect.top + navH + 6;

    SetWindowPos(s_previewHwnd, HWND_TOPMOST, s_targetX, s_targetY, PREVIEW_W, PREVIEW_H,
        SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOZORDER);
    RegisterThumb();
    InvalidateRect(s_previewHwnd, nullptr, FALSE);

    if (!s_visible || s_hiding)
        BeginPreviewAnimation(true);
    else
        ShowWindow(s_previewHwnd, SW_SHOWNOACTIVATE);

    s_visible = true;
    s_hiding = false;
}

void DesktopPreview_Hide()
{
    if (!s_visible || s_hiding)
        return;

    BeginPreviewAnimation(false);
}
