#pragma once
#include <string>
#include <vector>
#include <windows.h>

enum class WifiIconState
{
    Full,
    High,
    Low,
    Zero,
    Disconnected,
};

enum class BluetoothIconState
{
    Off,
    On,
    Connected,
};

enum class VolumeIconState
{
    Muted,
    Off,
    Low,
    Medium,
    High,
};

struct AudioEndpoint
{
    std::wstring id;
    std::wstring name;
    bool isActive;
};

struct AudioSession
{
    DWORD processId;
    std::wstring name;
    float volume;
    bool muted;
    bool isSystemSound;
};

struct WifiNetwork
{
    std::wstring ssid;
    std::wstring profileName;
    ULONG signalQuality = 0;
    bool isConnected = false;
    bool isSecure = false;
    bool hasProfile = false;
    bool isConnecting = false;
};

struct BluetoothDevice
{
    std::wstring name;
    std::wstring addressString; // Represented as string for easy comparison
    bool isConnected = false;
    bool isConnecting = false;
    bool isDisconnecting = false;
};

struct NotificationInfo
{
    UINT32 id = 0;
    UINT64 createdUnix = 0;
    std::wstring appName;
    std::wstring appUserModelId;
    std::wstring title;
    std::wstring subtext;
    std::wstring timeText;
    std::vector<BYTE> iconBytes;
};

enum class MediaPlaybackState
{
    None,
    Stopped,
    Paused,
    Playing,
};

struct MediaSessionInfo
{
    std::wstring sessionId;
    std::wstring sourceAppUserModelId;
    std::wstring appName;
    std::wstring title;
    std::wstring subtitle;
    MediaPlaybackState playbackState = MediaPlaybackState::None;
    bool isCurrent = false;
    bool canPlayPause = false;
    bool canSkipPrevious = false;
    bool canSkipNext = false;
    bool hasTimeline = false;
    long long timelineStartTicks = 0;
    long long timelineEndTicks = 0;
    long long timelinePositionTicks = 0;
    ULONGLONG timelineSnapshotMs = 0;
    ULONGLONG mediaPropertiesChangedMs = 0;
    std::vector<BYTE> thumbnailBytes;
};

struct StatusSnapshot
{
    WifiIconState      wifi             = WifiIconState::Disconnected;
    BluetoothIconState bluetooth        = BluetoothIconState::Off;
    VolumeIconState    volume           = VolumeIconState::Off;

    // Extended data for the drawer panel
    std::wstring wifiSsid;              // Connected SSID, empty when disconnected
    std::wstring bluetoothDeviceName;   // Connected device name, empty when none
    float        volumeLevel  = 0.0f;  // Master volume scalar [0.0, 1.0]
    bool         volumeMuted  = false;
    bool         wifiRadioOn  = false;

    std::vector<AudioEndpoint> audioEndpoints;
    std::vector<AudioSession>  audioSessions;
    std::vector<WifiNetwork>   wifiNetworks;
    std::vector<BluetoothDevice> bluetoothDevices;
    std::vector<MediaSessionInfo> mediaSessions;
    int mediaCurrentIndex = -1;
    int mediaSelectedIndex = -1;
};

inline bool operator==(const AudioEndpoint& lhs, const AudioEndpoint& rhs)
{
    return lhs.id == rhs.id && lhs.name == rhs.name && lhs.isActive == rhs.isActive;
}

inline bool operator==(const AudioSession& lhs, const AudioSession& rhs)
{
    return lhs.processId == rhs.processId && lhs.name == rhs.name && lhs.volume == rhs.volume && lhs.muted == rhs.muted && lhs.isSystemSound == rhs.isSystemSound;
}

inline bool operator==(const WifiNetwork& lhs, const WifiNetwork& rhs)
{
    return lhs.ssid == rhs.ssid && lhs.profileName == rhs.profileName && lhs.signalQuality == rhs.signalQuality && lhs.isConnected == rhs.isConnected && lhs.isSecure == rhs.isSecure && lhs.hasProfile == rhs.hasProfile && lhs.isConnecting == rhs.isConnecting;
}

inline bool operator==(const BluetoothDevice& lhs, const BluetoothDevice& rhs)
{
    return lhs.name == rhs.name && lhs.addressString == rhs.addressString && lhs.isConnected == rhs.isConnected && lhs.isConnecting == rhs.isConnecting && lhs.isDisconnecting == rhs.isDisconnecting;
}

inline bool operator==(const NotificationInfo& lhs, const NotificationInfo& rhs)
{
    return lhs.id == rhs.id
        && lhs.createdUnix == rhs.createdUnix
        && lhs.appName == rhs.appName
        && lhs.appUserModelId == rhs.appUserModelId
        && lhs.title == rhs.title
        && lhs.subtext == rhs.subtext
        && lhs.timeText == rhs.timeText
        && lhs.iconBytes == rhs.iconBytes;
}

inline bool operator==(const MediaSessionInfo& lhs, const MediaSessionInfo& rhs)
{
    return lhs.sessionId == rhs.sessionId
        && lhs.sourceAppUserModelId == rhs.sourceAppUserModelId
        && lhs.appName == rhs.appName
        && lhs.title == rhs.title
        && lhs.subtitle == rhs.subtitle
        && lhs.playbackState == rhs.playbackState
        && lhs.isCurrent == rhs.isCurrent
        && lhs.canPlayPause == rhs.canPlayPause
        && lhs.canSkipPrevious == rhs.canSkipPrevious
        && lhs.canSkipNext == rhs.canSkipNext
        && lhs.hasTimeline == rhs.hasTimeline
        && lhs.timelineStartTicks == rhs.timelineStartTicks
        && lhs.timelineEndTicks == rhs.timelineEndTicks
        && lhs.timelinePositionTicks == rhs.timelinePositionTicks
        && lhs.timelineSnapshotMs == rhs.timelineSnapshotMs
        && lhs.thumbnailBytes == rhs.thumbnailBytes;
}

inline bool operator==(const StatusSnapshot& lhs, const StatusSnapshot& rhs)
{
    return lhs.wifi             == rhs.wifi
        && lhs.bluetooth        == rhs.bluetooth
        && lhs.volume           == rhs.volume
        && lhs.wifiSsid         == rhs.wifiSsid
        && lhs.bluetoothDeviceName == rhs.bluetoothDeviceName
        && lhs.volumeLevel      == rhs.volumeLevel
        && lhs.volumeMuted      == rhs.volumeMuted
        && lhs.wifiRadioOn      == rhs.wifiRadioOn
        && lhs.audioEndpoints   == rhs.audioEndpoints
        && lhs.audioSessions    == rhs.audioSessions
        && lhs.wifiNetworks     == rhs.wifiNetworks
        && lhs.bluetoothDevices == rhs.bluetoothDevices
        && lhs.mediaSessions    == rhs.mediaSessions
        && lhs.mediaCurrentIndex == rhs.mediaCurrentIndex
        && lhs.mediaSelectedIndex == rhs.mediaSelectedIndex;
}

inline bool operator!=(const StatusSnapshot& lhs, const StatusSnapshot& rhs)
{
    return !(lhs == rhs);
}
