#include "status/audio_status.h"
#include "status/status_monitor.h"
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <atomic>
#include <string>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>

// Primary CLSID — works on Windows 10 and Windows 11
class DECLSPEC_UUID("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9") CPolicyConfigClient;

// Fallback CLSID — Vista/7/8 era, used if primary CoCreateInstance fails
class DECLSPEC_UUID("294935CE-F637-4E7C-A41B-AB255460B862") CPolicyConfigVistaClient;

MIDL_INTERFACE("F8679F50-850A-41CF-9C72-430F290290C8")
IPolicyConfig : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, BOOL, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, BOOL, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, struct DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, struct DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, BOOL) = 0;
};

// Vista-era alternate IID (used with CPolicyConfigVistaClient fallback)
MIDL_INTERFACE("568b9108-44bf-40b4-9006-86afe5b5a620")
IPolicyConfigVista : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, BOOL, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, BOOL, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, struct DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, struct DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, BOOL) = 0;
};

using Microsoft::WRL::ComPtr;

struct AudioStatusNotifier;

struct AudioStatusProviderImpl
{
    HWND hwnd = nullptr;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> endpoint;
    ComPtr<IAudioEndpointVolume> endpointVolume;
    std::wstring endpointId;
    struct AudioStatusNotifier* notifier = nullptr;
    VolumeIconState iconState  = VolumeIconState::Off;
    float           level      = 0.0f;   // cached master volume scalar [0,1]
    bool            muted      = false;  // cached mute state
};

struct AudioStatusNotifier : IMMNotificationClient, IAudioEndpointVolumeCallback
{
    explicit AudioStatusNotifier(HWND hwndIn) : hwnd(hwndIn) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (!ppvObject)
            return E_POINTER;

        *ppvObject = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient))
            *ppvObject = static_cast<IMMNotificationClient*>(this);
        else if (riid == __uuidof(IAudioEndpointVolumeCallback))
            *ppvObject = static_cast<IAudioEndpointVolumeCallback*>(this);
        else
            return E_NOINTERFACE;

        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++refCount;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG count = --refCount;
        if (count == 0)
            delete this;
        return count;
    }

    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override
    {
        Notify();
        return S_OK;
    }

    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        if (flow == eRender && (role == eConsole || role == eMultimedia))
            Notify();
        return S_OK;
    }

    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    void Notify() const
    {
        if (hwnd)
            PostMessageW(hwnd, WM_APP_STATUS_PROVIDER_EVENT, 0, 0);
    }

    HWND hwnd = nullptr;
    std::atomic<ULONG> refCount = 1;
};

namespace
{

    bool GetDeviceId(IMMDevice* device, std::wstring& id)
    {
        id.clear();
        if (!device)
            return false;

        LPWSTR rawId = nullptr;
        const HRESULT hr = device->GetId(&rawId);
        if (FAILED(hr) || !rawId)
            return false;

        id = rawId;
        CoTaskMemFree(rawId);
        return true;
    }

    VolumeIconState VolumeStateFromLevel(BOOL muted, float level)
    {
        if (muted)
            return VolumeIconState::Muted;
        if (level <= 0.001f)
            return VolumeIconState::Off;
        if (level < 0.34f)
            return VolumeIconState::Low;
        if (level < 0.67f)
            return VolumeIconState::Medium;
        return VolumeIconState::High;
    }

    bool RebindDefaultEndpoint(AudioStatusProviderImpl& provider)
    {
        if (!provider.enumerator)
            return false;

        ComPtr<IMMDevice> nextEndpoint;
        const HRESULT endpointHr = provider.enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &nextEndpoint);
        if (FAILED(endpointHr))
        {
            provider.endpoint.Reset();
            provider.endpointVolume.Reset();
            provider.endpointId.clear();
            provider.iconState = VolumeIconState::Off;
            return false;
        }

        std::wstring nextId;
        if (!GetDeviceId(nextEndpoint.Get(), nextId))
            return false;

        if (provider.endpointVolume && provider.endpointId == nextId)
            return true;

        if (provider.endpointVolume && provider.notifier)
            provider.endpointVolume->UnregisterControlChangeNotify(provider.notifier);

        provider.endpoint = nextEndpoint;
        provider.endpointId = nextId;
        provider.endpointVolume.Reset();

        HRESULT volumeHr = provider.endpoint->Activate(
            __uuidof(IAudioEndpointVolume),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(provider.endpointVolume.GetAddressOf()));
        if (FAILED(volumeHr) || !provider.endpointVolume)
            return false;

        if (provider.notifier)
            provider.endpointVolume->RegisterControlChangeNotify(provider.notifier);

        return true;
    }
}

bool AudioStatus_Init(AudioStatusProvider& provider, HWND hwnd)
{
    if (!provider.impl)
        provider.impl = new AudioStatusProviderImpl();

    provider.impl->hwnd = hwnd;

    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(provider.impl->enumerator.GetAddressOf()))))
    {
        delete provider.impl;
        provider.impl = nullptr;
        return false;
    }

    provider.impl->notifier = new AudioStatusNotifier(hwnd);
    if (provider.impl->notifier)
        provider.impl->enumerator->RegisterEndpointNotificationCallback(provider.impl->notifier);

    AudioStatus_Refresh(provider);
    return true;
}

void AudioStatus_Shutdown(AudioStatusProvider& provider)
{
    if (!provider.impl)
        return;

    if (provider.impl->endpointVolume && provider.impl->notifier)
        provider.impl->endpointVolume->UnregisterControlChangeNotify(provider.impl->notifier);

    if (provider.impl->enumerator && provider.impl->notifier)
        provider.impl->enumerator->UnregisterEndpointNotificationCallback(provider.impl->notifier);

    if (provider.impl->notifier)
    {
        provider.impl->notifier->Release();
        provider.impl->notifier = nullptr;
    }

    provider.impl->endpointVolume.Reset();
    provider.impl->endpoint.Reset();
    provider.impl->enumerator.Reset();
    provider.impl->endpointId.clear();
    provider.impl->iconState = VolumeIconState::Off;
    delete provider.impl;
    provider.impl = nullptr;
}

void AudioStatus_Refresh(AudioStatusProvider& provider)
{
    if (!provider.impl)
        return;

    if (!RebindDefaultEndpoint(*provider.impl) || !provider.impl->endpointVolume)
    {
        provider.impl->iconState = VolumeIconState::Off;
        provider.impl->level = 0.0f;
        provider.impl->muted = false;
        return;
    }

    BOOL muted = FALSE;
    float level = 0.0f;
    provider.impl->endpointVolume->GetMute(&muted);
    provider.impl->endpointVolume->GetMasterVolumeLevelScalar(&level);
    provider.impl->level  = level;
    provider.impl->muted  = (muted != FALSE);
    provider.impl->iconState = VolumeStateFromLevel(muted, level);
}

VolumeIconState AudioStatus_GetIconState(const AudioStatusProvider& provider)
{
    return provider.impl ? provider.impl->iconState : VolumeIconState::Off;
}

float AudioStatus_GetVolumeLevel(const AudioStatusProvider& provider)
{
    return provider.impl ? provider.impl->level : 0.0f;
}

bool AudioStatus_GetMuted(const AudioStatusProvider& provider)
{
    return provider.impl ? provider.impl->muted : false;
}

bool AudioStatus_SetMasterVolume(AudioStatusProvider& provider, float level)
{
    if (!provider.impl || !provider.impl->endpointVolume)
        return false;
    // Clamp to [0,1] before calling into Core Audio
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    // nullptr = no event-context GUID; we don't distinguish self-initiated changes
    return SUCCEEDED(provider.impl->endpointVolume->SetMasterVolumeLevelScalar(level, nullptr));
}

bool AudioStatus_SetMute(AudioStatusProvider& provider, bool mute)
{
    if (!provider.impl || !provider.impl->endpointVolume)
        return false;
    return SUCCEEDED(provider.impl->endpointVolume->SetMute(mute ? TRUE : FALSE, nullptr));
}

void AudioStatus_GetEndpoints(const AudioStatusProvider& provider, std::vector<AudioEndpoint>& outEndpoints)
{
    outEndpoints.clear();
    if (!provider.impl || !provider.impl->enumerator) return;

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(provider.impl->enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) return;

    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> dev;
        if (SUCCEEDED(collection->Item(i, &dev)))
        {
            AudioEndpoint ep;
            GetDeviceId(dev.Get(), ep.id);
            ep.isActive = (ep.id == provider.impl->endpointId);

            ComPtr<IPropertyStore> props;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)))
            {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName)) && varName.vt == VT_LPWSTR)
                {
                    ep.name = varName.pwszVal;
                }
                PropVariantClear(&varName);
            }
            outEndpoints.push_back(ep);
        }
    }
}

void AudioStatus_GetSessions(const AudioStatusProvider& provider, std::vector<AudioSession>& outSessions)
{
    outSessions.clear();
    if (!provider.impl || !provider.impl->endpoint) return;

    ComPtr<IAudioSessionManager2> sessionManager;
    if (FAILED(provider.impl->endpoint->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(sessionManager.GetAddressOf()))))
        return;

    ComPtr<IAudioSessionEnumerator> enumerator;
    if (FAILED(sessionManager->GetSessionEnumerator(&enumerator))) return;

    int count = 0;
    enumerator->GetCount(&count);
    for (int i = 0; i < count; ++i)
    {
        ComPtr<IAudioSessionControl> sessionCtrl;
        if (SUCCEEDED(enumerator->GetSession(i, &sessionCtrl)))
        {
            AudioSessionState state;
            if (FAILED(sessionCtrl->GetState(&state)) || state == AudioSessionStateExpired)
                continue;

            ComPtr<IAudioSessionControl2> ctrl2;
            if (SUCCEEDED(sessionCtrl.As(&ctrl2)))
            {
                if (ctrl2->IsSystemSoundsSession() == S_OK)
                {
                    AudioSession sess;
                    sess.processId = 0;
                    sess.name = L"System Sounds";
                    sess.isSystemSound = true;
                    
                    ComPtr<ISimpleAudioVolume> vol;
                    if (SUCCEEDED(sessionCtrl.As(&vol)))
                    {
                        vol->GetMasterVolume(&sess.volume);
                        BOOL m = FALSE;
                        vol->GetMute(&m);
                        sess.muted = (m != FALSE);
                    }
                    outSessions.push_back(sess);
                    continue;
                }

                DWORD pid = 0;
                if (SUCCEEDED(ctrl2->GetProcessId(&pid)) && pid != 0)
                {
                    AudioSession sess;
                    sess.processId = pid;
                    sess.isSystemSound = false;

                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    if (hProcess)
                    {
                        WCHAR path[MAX_PATH];
                        DWORD size = MAX_PATH;
                        if (QueryFullProcessImageNameW(hProcess, 0, path, &size))
                        {
                            WCHAR* filename = wcsrchr(path, L'\\');
                            if (filename) sess.name = filename + 1;
                            else sess.name = path;
                        }
                        CloseHandle(hProcess);
                    }

                    ComPtr<ISimpleAudioVolume> vol;
                    if (SUCCEEDED(sessionCtrl.As(&vol)))
                    {
                        vol->GetMasterVolume(&sess.volume);
                        BOOL m = FALSE;
                        vol->GetMute(&m);
                        sess.muted = (m != FALSE);
                    }
                    outSessions.push_back(sess);
                }
            }
        }
    }
}

bool AudioStatus_SetDefaultEndpoint(AudioStatusProvider& provider, const std::wstring& endpointId)
{
    if (endpointId.empty()) return false;

    // Try primary CLSID (Windows 10 / 11) — CLSCTX_ALL covers in-proc and local-server registrations
    ComPtr<IPolicyConfig> policyConfig;
    if (SUCCEEDED(CoCreateInstance(__uuidof(CPolicyConfigClient), nullptr,
        CLSCTX_ALL, __uuidof(IPolicyConfig),
        reinterpret_cast<void**>(policyConfig.GetAddressOf()))))
    {
        HRESULT hr1 = policyConfig->SetDefaultEndpoint(endpointId.c_str(), eConsole);
        HRESULT hr2 = policyConfig->SetDefaultEndpoint(endpointId.c_str(), eMultimedia);
        HRESULT hr3 = policyConfig->SetDefaultEndpoint(endpointId.c_str(), eCommunications);
        if (SUCCEEDED(hr1) || SUCCEEDED(hr2) || SUCCEEDED(hr3))
            return true;
    }

    // Fallback CLSID (Vista / 7 / 8 era)
    ComPtr<IPolicyConfigVista> policyConfigVista;
    if (SUCCEEDED(CoCreateInstance(__uuidof(CPolicyConfigVistaClient), nullptr,
        CLSCTX_ALL, __uuidof(IPolicyConfigVista),
        reinterpret_cast<void**>(policyConfigVista.GetAddressOf()))))
    {
        HRESULT hr1 = policyConfigVista->SetDefaultEndpoint(endpointId.c_str(), eConsole);
        HRESULT hr2 = policyConfigVista->SetDefaultEndpoint(endpointId.c_str(), eMultimedia);
        HRESULT hr3 = policyConfigVista->SetDefaultEndpoint(endpointId.c_str(), eCommunications);
        if (SUCCEEDED(hr1) || SUCCEEDED(hr2) || SUCCEEDED(hr3))
            return true;
    }

    return false;
}

bool AudioStatus_SetSessionVolume(AudioStatusProvider& provider, DWORD processId, float level)
{
    if (!provider.impl || !provider.impl->endpoint) return false;
    
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;

    ComPtr<IAudioSessionManager2> sessionManager;
    if (FAILED(provider.impl->endpoint->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(sessionManager.GetAddressOf()))))
        return false;

    ComPtr<IAudioSessionEnumerator> enumerator;
    if (FAILED(sessionManager->GetSessionEnumerator(&enumerator))) return false;

    int count = 0;
    enumerator->GetCount(&count);
    for (int i = 0; i < count; ++i)
    {
        ComPtr<IAudioSessionControl> sessionCtrl;
        if (SUCCEEDED(enumerator->GetSession(i, &sessionCtrl)))
        {
            ComPtr<IAudioSessionControl2> ctrl2;
            if (SUCCEEDED(sessionCtrl.As(&ctrl2)))
            {
                if (processId == 0)
                {
                    if (ctrl2->IsSystemSoundsSession() == S_OK)
                    {
                        ComPtr<ISimpleAudioVolume> vol;
                        if (SUCCEEDED(sessionCtrl.As(&vol)))
                        {
                            vol->SetMasterVolume(level, nullptr);
                            return true;
                        }
                    }
                }
                else
                {
                    DWORD pid = 0;
                    if (SUCCEEDED(ctrl2->GetProcessId(&pid)) && pid == processId)
                    {
                        ComPtr<ISimpleAudioVolume> vol;
                        if (SUCCEEDED(sessionCtrl.As(&vol)))
                        {
                            vol->SetMasterVolume(level, nullptr);
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

bool AudioStatus_SetSessionMute(AudioStatusProvider& provider, DWORD processId, bool mute)
{
    if (!provider.impl || !provider.impl->endpoint) return false;

    ComPtr<IAudioSessionManager2> sessionManager;
    if (FAILED(provider.impl->endpoint->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(sessionManager.GetAddressOf()))))
        return false;

    ComPtr<IAudioSessionEnumerator> enumerator;
    if (FAILED(sessionManager->GetSessionEnumerator(&enumerator))) return false;

    int count = 0;
    enumerator->GetCount(&count);
    for (int i = 0; i < count; ++i)
    {
        ComPtr<IAudioSessionControl> sessionCtrl;
        if (SUCCEEDED(enumerator->GetSession(i, &sessionCtrl)))
        {
            ComPtr<IAudioSessionControl2> ctrl2;
            if (SUCCEEDED(sessionCtrl.As(&ctrl2)))
            {
                if (processId == 0)
                {
                    if (ctrl2->IsSystemSoundsSession() == S_OK)
                    {
                        ComPtr<ISimpleAudioVolume> vol;
                        if (SUCCEEDED(sessionCtrl.As(&vol)))
                        {
                            vol->SetMute(mute ? TRUE : FALSE, nullptr);
                            return true;
                        }
                    }
                }
                else
                {
                    DWORD pid = 0;
                    if (SUCCEEDED(ctrl2->GetProcessId(&pid)) && pid == processId)
                    {
                        ComPtr<ISimpleAudioVolume> vol;
                        if (SUCCEEDED(sessionCtrl.As(&vol)))
                        {
                            vol->SetMute(mute ? TRUE : FALSE, nullptr);
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}
