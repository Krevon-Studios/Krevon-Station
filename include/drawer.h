#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "status/status_types.h"

// Posted to g_navbarHwnd whenever the drawer closes itself (e.g. a button click).
// window.cpp handles this to clear Renderer_SetDrawerOpen.
#define WM_APP_DRAWER_CLOSED (WM_APP + 3)

// ── Drawer API ────────────────────────────────────────────────────────────────
//
// The drawer is a popup panel that slides in below the navbar's status cluster
// when the user clicks on it. It shows user info, action buttons, Wi-Fi and
// Bluetooth pills, and a volume slider.
//
// Lifecycle:
//   1. Call Drawer_Create once after the navbar HWND exists.
//   2. Call Drawer_Toggle(snapshot) from WM_LBUTTONDOWN in the navbar.
//   3. Call Drawer_UpdateSnapshot(snapshot) from WM_APP_STATUS_CHANGED.
//   4. Call Drawer_Destroy on WM_DESTROY.

HWND Drawer_Create(HINSTANCE hInstance, HWND navbarHwnd);
void Drawer_Toggle(const StatusSnapshot& snapshot);
void Drawer_UpdateSnapshot(const StatusSnapshot& snapshot);
void Drawer_UpdateNotifications(const std::vector<NotificationInfo>& notifications);
void Drawer_UpdateAccentTheme();
bool Drawer_IsOpen();
void Drawer_Destroy();
