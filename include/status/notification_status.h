#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include "status_types.h"

#define WM_APP_NOTIFICATIONS_CHANGED (WM_APP + 4)

bool NotificationStatus_Init(HWND hwnd);
void NotificationStatus_Shutdown();
void NotificationStatus_RequestAccess();
void NotificationStatus_RefreshAsync();
std::vector<NotificationInfo> NotificationStatus_GetNotifications();
bool NotificationStatus_Remove(UINT32 notificationId);
bool NotificationStatus_ClearAll();
bool NotificationStatus_Activate(const NotificationInfo& notification);
