#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "status/status_types.h"

HWND MediaIsland_Create(HINSTANCE hInstance, HWND navbarHwnd);
void MediaIsland_Show(const StatusSnapshot& snapshot);
void MediaIsland_UpdateSnapshot(const StatusSnapshot& snapshot);
void MediaIsland_TickClock();
bool MediaIsland_IsOpen();
void MediaIsland_Destroy();
