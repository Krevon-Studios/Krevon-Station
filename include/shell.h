#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <memory>

// Application-defined message the system uses to notify us of AppBar events
#define WM_APPBAR_CALLBACK (WM_USER + 1)
// Tray icon notification message
#define WM_TRAYICON        (WM_USER + 2)

// Menu command IDs
#define IDM_TRAY_EXIT 1001
#define IDM_TRAY_CHECK_UPDATES 1002

void AppBar_Register(HWND hWnd);
void AppBar_Remove(HWND hWnd);
void AppBar_SetPos(HWND hWnd);

void Tray_Add(HWND hWnd);
void Tray_Remove(HWND hWnd);
