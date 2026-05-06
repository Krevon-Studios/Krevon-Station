#pragma once
#include "../state/drawer_state.h"

void Render();
float Drawer_GetCurrentPanelHeight();
HRESULT DrawerInit(HWND hWnd);
HRESULT RebuildTarget();
void DrawSvg(ID2D1SvgDocument* svg, float cx, float cy, float sz);
void DrawSvgColored(ID2D1SvgDocument* svg, float cx, float cy, float sz, D2D1_COLOR_F color);
ComPtr<ID2D1Bitmap1> GetAppIcon(DWORD processId, const std::wstring& name);
ComPtr<ID2D1Bitmap1> GetNotificationIcon(const NotificationInfo& notification);
void NotificationPanel_UpdateMetrics(float drawerHeight);
void NotificationPanel_Render(float drawerHeight, float opacity);
bool NotificationPanel_HandleWheel(HWND hWnd, D2D1_POINT_2F p, float delta);
bool NotificationPanel_HandleClick(HWND hWnd, D2D1_POINT_2F p);
void NotificationPanel_UpdateHover(D2D1_POINT_2F p);
void NotificationPanel_ResetHover();
