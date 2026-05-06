#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "status/status_types.h"

struct AudioStatusProviderImpl;

struct AudioStatusProvider
{
    AudioStatusProviderImpl* impl = nullptr;
};

bool AudioStatus_Init(AudioStatusProvider& provider, HWND hwnd);
void AudioStatus_Shutdown(AudioStatusProvider& provider);
void AudioStatus_Refresh(AudioStatusProvider& provider);
VolumeIconState AudioStatus_GetIconState(const AudioStatusProvider& provider);

// Extended queries for the drawer panel
float AudioStatus_GetVolumeLevel(const AudioStatusProvider& provider); // [0.0, 1.0]
bool  AudioStatus_GetMuted(const AudioStatusProvider& provider);

// Endpoints & Sessions
void AudioStatus_GetEndpoints(const AudioStatusProvider& provider, std::vector<AudioEndpoint>& outEndpoints);
void AudioStatus_GetSessions(const AudioStatusProvider& provider, std::vector<AudioSession>& outSessions);

bool AudioStatus_SetDefaultEndpoint(AudioStatusProvider& provider, const std::wstring& endpointId);
bool AudioStatus_SetSessionVolume(AudioStatusProvider& provider, DWORD processId, float level);
bool AudioStatus_SetSessionMute(AudioStatusProvider& provider, DWORD processId, bool mute);

// Volume control (fires COM call on the default endpoint)
bool  AudioStatus_SetMasterVolume(AudioStatusProvider& provider, float level); // [0.0, 1.0]
bool  AudioStatus_SetMute(AudioStatusProvider& provider, bool mute);
