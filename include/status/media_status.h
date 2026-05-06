#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include "status/status_types.h"

struct MediaStatusProviderImpl;

struct MediaStatusProvider
{
    MediaStatusProviderImpl* impl = nullptr;
};

bool MediaStatus_Init(MediaStatusProvider& provider, HWND hwnd);
void MediaStatus_Shutdown(MediaStatusProvider& provider);
void MediaStatus_RequestRefresh(MediaStatusProvider& provider);
void MediaStatus_GetSessions(const MediaStatusProvider& provider, std::vector<MediaSessionInfo>& outSessions, int& currentIndex, int& selectedIndex);

bool MediaStatus_SelectSession(MediaStatusProvider& provider, int index);
bool MediaStatus_TogglePlayPause(MediaStatusProvider& provider);
bool MediaStatus_SkipPrevious(MediaStatusProvider& provider);
bool MediaStatus_SkipNext(MediaStatusProvider& provider);
bool MediaStatus_SeekTo(MediaStatusProvider& provider, long long positionTicks);
