#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct VirtualDesktopSnapshot
{
    bool available = false;
    int  count = 0;
    int  currentIndex = -1;
};

inline bool operator==(const VirtualDesktopSnapshot& lhs, const VirtualDesktopSnapshot& rhs)
{
    return lhs.available == rhs.available
        && lhs.count == rhs.count
        && lhs.currentIndex == rhs.currentIndex;
}

inline bool operator!=(const VirtualDesktopSnapshot& lhs, const VirtualDesktopSnapshot& rhs)
{
    return !(lhs == rhs);
}

HRESULT VirtualDesktop_Init();
void VirtualDesktop_Shutdown();
bool VirtualDesktop_Refresh();
VirtualDesktopSnapshot VirtualDesktop_GetSnapshot();
bool VirtualDesktop_GetDesktopId(int index, GUID* id);
bool VirtualDesktop_GetWindowDesktopId(HWND hwnd, GUID* id);
bool VirtualDesktop_SwitchToIndex(int index);
