#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Navbar height in device-independent pixels (96 DPI baseline)
constexpr int NAVBAR_HEIGHT_DIP = 32;

ATOM Window_RegisterClass(HINSTANCE hInstance);
HWND Window_Create(HINSTANCE hInstance);
