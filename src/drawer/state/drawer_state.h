#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <lmcons.h>
#include <dwmapi.h>
#include <dcomp.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <sddl.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include "window.h"
#include "status/status_types.h"
#include "status/status_monitor.h"
#include "drawer_layout.h"

using Microsoft::WRL::ComPtr;

struct AnimEndpoint {
    AudioEndpoint data;
    float alpha = 0.0f;
    float scale = 0.9f;
    bool isRemoving = false;
};

struct AnimSession {
    AudioSession data;
    float alpha = 0.0f;
    float scale = 0.9f;
    bool isRemoving = false;
};

struct AnimNotification {
    NotificationInfo data;
    float alpha = 0.0f;
    float scale = 0.9f;
    bool isRemoving = false;
};

extern HWND   g_drawerHwnd;
extern HWND   g_navbarHwnd;
extern bool   g_open;

extern ComPtr<ID3D11Device>        g_d3d;
extern ComPtr<IDXGISwapChain1>     g_swap;
extern ComPtr<ID2D1Factory1>       g_fac;
extern ComPtr<ID2D1Device>         g_d2dDev;
extern ComPtr<ID2D1DeviceContext5> g_ctx;
extern ComPtr<ID2D1Bitmap1>        g_target;

extern ComPtr<IDCompositionDevice> g_dcompDevice;
extern ComPtr<IDCompositionTarget> g_dcompTarget;
extern ComPtr<IDCompositionVisual> g_dcompVisual;

extern ComPtr<IDWriteFactory>    g_dw;
extern ComPtr<IDWriteTextFormat> g_tfName;
extern ComPtr<IDWriteTextFormat> g_tfSub;
extern ComPtr<IDWriteTextFormat> g_tfPct;
extern ComPtr<IDWriteTextFormat> g_tfSndHdg;
extern ComPtr<IDWriteTextFormat> g_tfNotifHeader;
extern ComPtr<IDWriteTextFormat> g_tfNotifApp;
extern ComPtr<IDWriteTextFormat> g_tfNotifTitle;
extern ComPtr<IDWriteTextFormat> g_tfNotifBody;
extern ComPtr<IDWriteTextFormat> g_tfNotifButton;

extern ComPtr<ID2D1SolidColorBrush> g_brWhite, g_brGrey;
extern ComPtr<ID2D1SolidColorBrush> g_brBtn,   g_brBtnHov;
extern ComPtr<ID2D1SolidColorBrush> g_brPill,  g_brPillHov, g_brPillIco;
extern ComPtr<ID2D1SolidColorBrush> g_brTrack, g_brFill,    g_brThumb, g_brBg;
extern ComPtr<ID2D1SolidColorBrush> g_brSep;

extern ComPtr<ID2D1SvgDocument> g_svgUserRound, g_svgCamera, g_svgSettings;
extern ComPtr<ID2D1SvgDocument> g_svgLock,      g_svgPower,  g_svgSettings2, g_svgChevron;
extern ComPtr<ID2D1SvgDocument> g_svgWifi[5];
extern ComPtr<ID2D1SvgDocument> g_svgBt[3];
extern ComPtr<ID2D1SvgDocument> g_svgVol[5];
extern ComPtr<ID2D1SvgDocument> g_svgMoon, g_svgRestart, g_svgPowerOff, g_svgChevronLeft;
extern ComPtr<ID2D1SvgDocument> g_svgMoon, g_svgRestart, g_svgPowerOff, g_svgChevronLeft;
extern ComPtr<ID2D1SvgDocument> g_svgSpeaker, g_svgHeadphones;
extern ComPtr<ID2D1SvgDocument> g_svgCheck, g_svgRefresh, g_svgKey;
extern ComPtr<ID2D1SvgDocument> g_svgX, g_svgTrash;
extern ComPtr<ID2D1Bitmap>      g_avatarBmp;
extern std::unordered_map<DWORD, ComPtr<ID2D1Bitmap1>> g_appIcons;
extern std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap1>> g_notificationIcons;

extern StatusSnapshot g_snap;
extern std::wstring   g_username;

extern bool      g_animOpen;
extern float     g_animProgress, g_animFrom, g_animTarget, g_animDurMs;
extern ULONGLONG g_animStart;

extern DrawerPanel g_activePanel;
extern DrawerPanel g_targetPanel;
extern float       g_panelAnimProgress;
extern float       g_panelAnimFrom;
extern float       g_panelAnimTarget;
extern float       g_panelAnimDurMs;
extern ULONGLONG   g_panelAnimStart;

extern int   g_btnHov, g_pillHov, g_pwrItemHov;
extern bool  g_volIconHov;
extern bool  g_volSetHov;
extern bool  g_avHov, g_backHov;
extern bool  g_dragging;
extern float g_sliderLeft, g_sliderRight;

extern float g_btnHovT[BTN_COUNT];
extern float g_pillHovT[PILL_COUNT];
extern float g_pwrItemHovT[PWR_ITEM_COUNT];
extern float g_avHovT;
extern float g_volIconHovT;
extern float g_volSetHovT;
extern float g_backHovT;

extern D2D1_ELLIPSE g_avEllipse;
extern D2D1_ELLIPSE g_btnEllipse[BTN_COUNT];
extern D2D1_RECT_F  g_pillRect[PILL_COUNT];
extern D2D1_RECT_F  g_volIconRect;
extern D2D1_RECT_F  g_volSetRect;
extern D2D1_RECT_F  g_sliderTrackRect;
extern D2D1_ELLIPSE g_backEllipse;
extern D2D1_RECT_F  g_pwrItemRect[PWR_ITEM_COUNT];

extern float g_sndDeviceScrollY;
extern float g_sndAppScrollY;
extern float g_sndDeviceScrollTargetY;
extern float g_sndAppScrollTargetY;
extern float g_sndDeviceScrollVelY;
extern float g_sndAppScrollVelY;
extern float g_sndTargetH;
extern D2D1_RECT_F g_sndDeviceClipRect;
extern D2D1_RECT_F g_sndAppClipRect;
extern float g_sndMixerY;
extern SoundPanelMetrics g_sndMetrics;

extern std::vector<AnimEndpoint> g_animEndpoints;
extern std::vector<AnimSession>  g_animSessions;

extern std::vector<D2D1_RECT_F> g_sndDeviceRects;
extern std::vector<float>       g_sndDeviceHovT;
extern std::vector<D2D1_RECT_F> g_sndAppIconRects;
extern std::vector<float>       g_sndAppIconHovT;
extern std::vector<D2D1_RECT_F> g_sndAppSliderRects;
extern std::vector<D2D1_RECT_F> g_sndAppSliderThumbRects;
extern std::vector<float>       g_sndAppSliderHovT;
extern int g_sndDeviceHovIdx;
extern int g_sndAppIconHovIdx;
extern int g_sndAppSliderHovIdx;
extern int g_sndDraggingAppIdx;

// Wifi Panel State
struct AnimWifiNetwork {
    WifiNetwork data;
    float alpha = 0.0f;
    float scale = 0.9f;
    bool isRemoving = false;
};
extern std::vector<AnimWifiNetwork> g_animWifiNetworks;
extern std::vector<D2D1_RECT_F> g_wifiNetRects;
extern std::vector<float>       g_wifiNetHovT;
extern int g_wifiNetHovIdx;
extern int g_wifiNetClickIdx;
extern float g_wifiConnectingAngle;
extern D2D1_RECT_F g_wifiClipRect;
extern float g_wifiTargetH;
extern float g_wifiScrollY;
extern float g_wifiScrollTargetY;
extern float g_wifiScrollVelY;
extern float g_wifiRefreshHovT;
extern bool  g_wifiRefreshHov;
extern bool  g_wifiRefreshClick;
extern float g_wifiRefreshSpinT;
extern D2D1_RECT_F g_wifiRefreshRect;
extern float g_wifiToggleHovT;
extern bool  g_wifiToggleHov;
extern bool  g_wifiTogglePending;
extern bool  g_wifiToggleTargetOn;
extern D2D1_RECT_F g_wifiToggleRect;
extern D2D1_RECT_F g_wifiToggleThumbRect;

// Bluetooth Panel State
struct AnimBluetoothDevice {
    BluetoothDevice data;
    float alpha = 0.0f;
    float scale = 0.9f;
    bool isRemoving = false;
};
extern std::vector<AnimBluetoothDevice> g_animBtDevices;
extern std::vector<D2D1_RECT_F> g_btDevRects;
extern std::vector<float>       g_btDevHovT;
extern int g_btDevHovIdx;
extern int g_btDevClickIdx;
extern float g_btConnectingAngle;
extern D2D1_RECT_F g_btClipRect;
extern float g_btTargetH;
extern float g_btScrollY;
extern float g_btScrollTargetY;
extern float g_btScrollVelY;
extern float g_btRefreshHovT;
extern bool  g_btRefreshHov;
extern bool  g_btRefreshClick;
extern float g_btRefreshSpinT;
extern D2D1_RECT_F g_btRefreshRect;
extern float g_btToggleHovT;
extern bool  g_btToggleHov;
extern bool  g_btTogglePending;
extern bool  g_btToggleTargetOn;
extern D2D1_RECT_F g_btToggleRect;
extern D2D1_RECT_F g_btToggleThumbRect;

extern std::vector<AnimNotification> g_animNotifications;
extern std::vector<D2D1_RECT_F> g_notifRowRects;
extern std::vector<D2D1_RECT_F> g_notifCloseRects;
extern std::vector<float>       g_notifRowHovT;
extern std::vector<float>       g_notifCloseHovT;
extern int   g_notifRowHovIdx;
extern int   g_notifCloseHovIdx;
extern int   g_notifClickIdx;
extern bool  g_notifClearHov;
extern bool  g_notifClearClick;
extern float g_notifClearHovT;
extern D2D1_RECT_F g_notifPanelRect;
extern D2D1_RECT_F g_notifClipRect;
extern D2D1_RECT_F g_notifClearRect;
extern float g_notifPanelY;
extern float g_notifPanelH;
extern float g_notifListTotalH;
extern float g_notifListViewH;
extern float g_notifScrollY;
extern float g_notifScrollTargetY;
extern float g_notifScrollVelY;

extern float g_prevVolumeLevel;
extern D2D1_COLOR_F g_accentColor;
extern D2D1_COLOR_F g_clrPill;
extern D2D1_COLOR_F g_clrPillHov;
extern D2D1_COLOR_F g_clrPillIco;
extern D2D1_COLOR_F g_clrFill;
extern HHOOK g_mouseHook;

void UpdateAccentColors(D2D1_COLOR_F accent);

inline ID2D1SvgDocument* WifiSvg(WifiIconState s)
{
    switch (s) {
    case WifiIconState::Full:  return g_svgWifi[0].Get();
    case WifiIconState::High:  return g_svgWifi[1].Get();
    case WifiIconState::Low:   return g_svgWifi[2].Get();
    case WifiIconState::Zero:  return g_svgWifi[3].Get();
    default:                   return g_svgWifi[4].Get();
    }
}
inline ID2D1SvgDocument* BtSvg(BluetoothIconState s)
{
    switch (s) {
    case BluetoothIconState::Connected: return g_svgBt[0].Get();
    case BluetoothIconState::On:        return g_svgBt[1].Get();
    default:                            return g_svgBt[2].Get();
    }
}
inline ID2D1SvgDocument* VolSvg(VolumeIconState s)
{
    switch (s) {
    case VolumeIconState::Muted:  return g_svgVol[0].Get();
    case VolumeIconState::Off:    return g_svgVol[1].Get();
    case VolumeIconState::Low:    return g_svgVol[2].Get();
    case VolumeIconState::Medium: return g_svgVol[3].Get();
    default:                      return g_svgVol[4].Get();
    }
}
