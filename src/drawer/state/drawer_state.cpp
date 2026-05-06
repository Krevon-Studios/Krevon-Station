// drawer_state.cpp - Internal state definitions for the drawer module.
#include "drawer_state.h"

HWND   g_drawerHwnd   = nullptr;
HWND   g_navbarHwnd   = nullptr;
bool   g_open         = false;
float  g_dpiScale     = 1.0f;

ComPtr<ID3D11Device>        g_d3d;
ComPtr<IDXGISwapChain1>     g_swap;
ComPtr<ID2D1Factory1>       g_fac;
ComPtr<ID2D1Device>         g_d2dDev;
ComPtr<ID2D1DeviceContext5> g_ctx;
ComPtr<ID2D1Bitmap1>        g_target;

ComPtr<IDCompositionDevice> g_dcompDevice;
ComPtr<IDCompositionTarget> g_dcompTarget;
ComPtr<IDCompositionVisual> g_dcompVisual;

ComPtr<IDWriteFactory>    g_dw;
ComPtr<IDWriteTextFormat> g_tfName;
ComPtr<IDWriteTextFormat> g_tfSub;
ComPtr<IDWriteTextFormat> g_tfPct;
ComPtr<IDWriteTextFormat> g_tfSndHdg;
ComPtr<IDWriteTextFormat> g_tfNotifHeader;
ComPtr<IDWriteTextFormat> g_tfNotifApp;
ComPtr<IDWriteTextFormat> g_tfNotifTitle;
ComPtr<IDWriteTextFormat> g_tfNotifBody;
ComPtr<IDWriteTextFormat> g_tfNotifButton;

ComPtr<ID2D1SolidColorBrush> g_brWhite, g_brGrey;
ComPtr<ID2D1SolidColorBrush> g_brBtn,   g_brBtnHov;
ComPtr<ID2D1SolidColorBrush> g_brPill,  g_brPillHov, g_brPillIco;
ComPtr<ID2D1SolidColorBrush> g_brTrack, g_brFill,    g_brThumb, g_brBg;
ComPtr<ID2D1SolidColorBrush> g_brSep;

ComPtr<ID2D1SvgDocument> g_svgUserRound, g_svgCamera, g_svgSettings;
ComPtr<ID2D1SvgDocument> g_svgLock,      g_svgPower,  g_svgSettings2, g_svgChevron;
ComPtr<ID2D1SvgDocument> g_svgWifi[5];
ComPtr<ID2D1SvgDocument> g_svgBt[3];
ComPtr<ID2D1SvgDocument> g_svgVol[5];
ComPtr<ID2D1SvgDocument> g_svgMoon, g_svgRestart, g_svgPowerOff, g_svgChevronLeft;
ComPtr<ID2D1SvgDocument> g_svgSpeaker, g_svgHeadphones;
ComPtr<ID2D1SvgDocument> g_svgCheck, g_svgRefresh, g_svgKey;
ComPtr<ID2D1SvgDocument> g_svgX, g_svgTrash;
ComPtr<ID2D1Bitmap>      g_avatarBmp;

std::unordered_map<DWORD, ComPtr<ID2D1Bitmap1>> g_appIcons;
std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap1>> g_notificationIcons;

StatusSnapshot g_snap;
std::wstring   g_username;

bool      g_animOpen     = false;
float     g_animProgress = 0.0f;
float     g_animFrom     = 0.0f;
float     g_animTarget   = 0.0f;
float     g_animDurMs    = 0.0f;
ULONGLONG g_animStart    = 0;

DrawerPanel g_activePanel       = DrawerPanel::Main;
DrawerPanel g_targetPanel       = DrawerPanel::Main;
float       g_panelAnimProgress = 1.0f;
float       g_panelAnimFrom     = 0.0f;
float       g_panelAnimTarget   = 0.0f;
float       g_panelAnimDurMs    = 0.0f;
ULONGLONG   g_panelAnimStart    = 0;

int  g_btnHov    = -1;
int  g_pillHov   = -1;
int  g_pwrItemHov= -1;
bool g_volIconHov = false;
bool g_volSetHov  = false;
bool g_avHov      = false;
bool g_backHov    = false;
bool g_dragging   = false;
float g_sliderLeft  = 0.0f;
float g_sliderRight = 0.0f;

// Smooth hover progress values [0,1]
float g_btnHovT[BTN_COUNT]   = {};
float g_pillHovT[PILL_COUNT] = {};
float g_pwrItemHovT[PWR_ITEM_COUNT] = {};
float g_avHovT               = 0.0f;
float g_volIconHovT          = 0.0f;
float g_volSetHovT           = 0.0f;
float g_backHovT             = 0.0f;

D2D1_ELLIPSE g_avEllipse            = {};
D2D1_ELLIPSE g_btnEllipse[BTN_COUNT]  = {};
D2D1_RECT_F  g_pillRect[PILL_COUNT]   = {};
D2D1_RECT_F  g_volIconRect            = {};
D2D1_RECT_F  g_volSetRect             = {};
D2D1_RECT_F  g_sliderTrackRect        = {};
D2D1_ELLIPSE g_backEllipse          = {};
D2D1_RECT_F  g_pwrItemRect[PWR_ITEM_COUNT] = {};

float g_sndDeviceScrollY = 0.0f;
float g_sndAppScrollY = 0.0f;
float g_sndDeviceScrollTargetY = 0.0f;
float g_sndAppScrollTargetY = 0.0f;
float g_sndDeviceScrollVelY = 0.0f;
float g_sndAppScrollVelY = 0.0f;
float g_sndTargetH = 0.0f;
D2D1_RECT_F g_sndDeviceClipRect = {};
D2D1_RECT_F g_sndAppClipRect = {};
float g_sndMixerY = 0.0f;

std::vector<AnimEndpoint> g_animEndpoints;
std::vector<AnimSession> g_animSessions;

std::vector<D2D1_RECT_F> g_sndDeviceRects;
std::vector<float>       g_sndDeviceHovT;
std::vector<D2D1_RECT_F> g_sndAppIconRects;
std::vector<float>       g_sndAppIconHovT;
std::vector<D2D1_RECT_F> g_sndAppSliderRects;
std::vector<D2D1_RECT_F> g_sndAppSliderThumbRects;
std::vector<float>       g_sndAppSliderHovT;
int g_sndDeviceHovIdx = -1;
int g_sndAppIconHovIdx = -1;
int g_sndAppSliderHovIdx = -1;
int g_sndDraggingAppIdx = -1;

std::vector<AnimWifiNetwork> g_animWifiNetworks;
std::vector<D2D1_RECT_F> g_wifiNetRects;
std::vector<float>       g_wifiNetHovT;
int g_wifiNetHovIdx = -1;
int g_wifiNetClickIdx = -1;
float g_wifiConnectingAngle = 0.0f;
D2D1_RECT_F g_wifiClipRect = {};
float g_wifiTargetH = 0.0f;
float g_wifiScrollY = 0.0f;
float g_wifiScrollTargetY = 0.0f;
float g_wifiScrollVelY = 0.0f;
float g_wifiRefreshHovT = 0.0f;
bool  g_wifiRefreshHov = false;
bool  g_wifiRefreshClick = false;
float g_wifiRefreshSpinT = 0.0f;
D2D1_RECT_F g_wifiRefreshRect = {};
float g_wifiToggleHovT = 0.0f;
bool  g_wifiToggleHov = false;
bool  g_wifiTogglePending = false;
bool  g_wifiToggleTargetOn = false;
D2D1_RECT_F g_wifiToggleRect = {};
D2D1_RECT_F g_wifiToggleThumbRect = {};

std::vector<AnimBluetoothDevice> g_animBtDevices;
std::vector<D2D1_RECT_F> g_btDevRects;
std::vector<float>       g_btDevHovT;
int g_btDevHovIdx = -1;
int g_btDevClickIdx = -1;
float g_btConnectingAngle = 0.0f;
D2D1_RECT_F g_btClipRect = {};
float g_btTargetH = 0.0f;
float g_btScrollY = 0.0f;
float g_btScrollTargetY = 0.0f;
float g_btScrollVelY = 0.0f;
float g_btRefreshHovT = 0.0f;
bool  g_btRefreshHov = false;
bool  g_btRefreshClick = false;
float g_btRefreshSpinT = 0.0f;
D2D1_RECT_F g_btRefreshRect = {};
float g_btToggleHovT = 0.0f;
bool  g_btToggleHov = false;
bool  g_btTogglePending = false;
bool  g_btToggleTargetOn = false;
D2D1_RECT_F g_btToggleRect = {};
D2D1_RECT_F g_btToggleThumbRect = {};

std::vector<AnimNotification> g_animNotifications;
std::vector<D2D1_RECT_F> g_notifRowRects;
std::vector<D2D1_RECT_F> g_notifCloseRects;
std::vector<float>       g_notifRowHovT;
std::vector<float>       g_notifCloseHovT;
int   g_notifRowHovIdx = -1;
int   g_notifCloseHovIdx = -1;
int   g_notifClickIdx = -1;
bool  g_notifClearHov = false;
bool  g_notifClearClick = false;
float g_notifClearHovT = 0.0f;
D2D1_RECT_F g_notifPanelRect = {};
D2D1_RECT_F g_notifClipRect = {};
D2D1_RECT_F g_notifClearRect = {};
float g_notifPanelY = 0.0f;
float g_notifPanelH = 0.0f;
float g_notifListTotalH = 0.0f;
float g_notifListViewH = 0.0f;
float g_notifScrollY = 0.0f;
float g_notifScrollTargetY = 0.0f;
float g_notifScrollVelY = 0.0f;

float g_prevVolumeLevel = 0.0f;
D2D1_COLOR_F g_clrPill       = { 0.0f, 0.0f, 0.0f, 1.0f };
D2D1_COLOR_F g_clrPillHov    = { 0.0f, 0.0f, 0.0f, 1.0f };
D2D1_COLOR_F g_clrPillIco    = { 0.0f, 0.0f, 0.0f, 1.0f };
D2D1_COLOR_F g_clrFill       = { 0.0f, 0.0f, 0.0f, 1.0f };
D2D1_COLOR_F g_accentColor = { 0.0f, 0.47f, 0.83f, 1.0f };
HHOOK g_mouseHook = nullptr;
SoundPanelMetrics g_sndMetrics;
