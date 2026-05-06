#include "virtual_desktop.h"

#include <objectarray.h>
#include <servprov.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

static constexpr GUID CLSID_ImmersiveShell =
{ 0xC2F03A33, 0x21F5, 0x47FA, { 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39 } };

static constexpr GUID CLSID_VirtualDesktopManagerInternal =
{ 0xC5E0CDCA, 0x7B6E, 0x41B2, { 0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B } };

static constexpr GUID CLSID_VirtualDesktopManager =
{ 0xAA509086, 0x5CA9, 0x4C25, { 0x8F, 0x95, 0x58, 0x9D, 0x3C, 0x07, 0xB4, 0x8A } };

MIDL_INTERFACE("A5CD92FF-29BE-454C-8D04-D82879FB3F1B")
IVirtualDesktopManagerPublic : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE IsWindowOnCurrentVirtualDesktop(HWND topLevelWindow, BOOL* onCurrentDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWindowDesktopId(HWND topLevelWindow, GUID* desktopId) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveWindowToDesktop(HWND topLevelWindow, REFGUID desktopId) = 0;
};

MIDL_INTERFACE("3F07F4BE-B107-441A-AF0F-39D82529072C")
IVirtualDesktop11 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(IUnknown* view, BOOL* visible) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetId(GUID* id) = 0;
};

MIDL_INTERFACE("53F5CA0B-158F-4124-900C-057158060B27")
IVirtualDesktopManagerInternal11 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(IUnknown* view, IVirtualDesktop11* desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(IUnknown* view, BOOL* canMove) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(IVirtualDesktop11** desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(IObjectArray** desktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(IVirtualDesktop11* from, int direction, IVirtualDesktop11** desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(IVirtualDesktop11* desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktopAndMoveForegroundView(IVirtualDesktop11* desktop) = 0;
};

MIDL_INTERFACE("FF72FFDD-BE7E-43FC-9C03-AD81681E88E4")
IVirtualDesktop10 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(IUnknown* view, BOOL* visible) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetId(GUID* id) = 0;
};

MIDL_INTERFACE("F31574D6-B682-4CDC-BD56-1827860ABEC6")
IVirtualDesktopManagerInternal10 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(IUnknown* view, IVirtualDesktop10* desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(IUnknown* view, BOOL* canMove) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(IVirtualDesktop10** desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(IObjectArray** desktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(IVirtualDesktop10* from, int direction, IVirtualDesktop10** desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(IVirtualDesktop10* desktop) = 0;
};

MIDL_INTERFACE("0F3A72B0-4566-487E-9A33-4ED302F6D6CE")
IVirtualDesktopManagerInternal10Alt : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(IUnknown* view, IVirtualDesktop10* desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(IUnknown* view, BOOL* canMove) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(IVirtualDesktop10** desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(IObjectArray** desktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(IVirtualDesktop10* from, int direction, IVirtualDesktop10** desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(IVirtualDesktop10* desktop) = 0;
};

enum class VirtualDesktopApi
{
    None,
    Win11,
    Win10,
    Win10Alt,
};

static VirtualDesktopApi s_api = VirtualDesktopApi::None;
static ComPtr<IVirtualDesktopManagerInternal11> s_manager11;
static ComPtr<IVirtualDesktopManagerInternal10> s_manager10;
static ComPtr<IVirtualDesktopManagerInternal10Alt> s_manager10Alt;
static ComPtr<IVirtualDesktopManagerPublic> s_publicManager;
static VirtualDesktopSnapshot s_snapshot = {};

template <typename TDesktop>
static int FindCurrentIndex(IObjectArray* desktops, int count, const GUID& currentId)
{
    for (int i = 0; i < count; ++i)
    {
        ComPtr<TDesktop> desktop;
        HRESULT hr = desktops->GetAt(static_cast<UINT>(i), __uuidof(TDesktop), reinterpret_cast<void**>(desktop.GetAddressOf()));
        if (FAILED(hr) || !desktop)
            continue;

        GUID desktopId = {};
        hr = desktop->GetId(&desktopId);
        if (SUCCEEDED(hr) && IsEqualGUID(desktopId, currentId))
            return i;
    }
    return -1;
}

template <typename TManager, typename TDesktop>
static bool RefreshFromManager(TManager* manager, VirtualDesktopSnapshot& out)
{
    if (!manager)
        return false;

    UINT count = 0;
    HRESULT hr = manager->GetCount(&count);
    if (FAILED(hr))
        return false;
    if (count <= 0)
        return false;

    ComPtr<TDesktop> current;
    hr = manager->GetCurrentDesktop(current.GetAddressOf());
    if (FAILED(hr) || !current)
        return false;

    ComPtr<IObjectArray> desktops;
    hr = manager->GetDesktops(desktops.GetAddressOf());
    if (FAILED(hr) || !desktops)
        return false;

    out.available = true;
    out.count = static_cast<int>(count);

    GUID currentId = {};
    hr = current->GetId(&currentId);
    if (FAILED(hr))
        return false;

    out.currentIndex = FindCurrentIndex<TDesktop>(desktops.Get(), static_cast<int>(count), currentId);
    if (out.currentIndex < 0)
        return false;

    return true;
}

template <typename TManager, typename TDesktop>
static bool SwitchWithManager(TManager* manager, int index)
{
    if (!manager || index < 0)
        return false;

    UINT count = 0;
    HRESULT hr = manager->GetCount(&count);
    if (FAILED(hr) || index >= static_cast<int>(count))
        return false;

    ComPtr<IObjectArray> desktops;
    hr = manager->GetDesktops(desktops.GetAddressOf());
    if (FAILED(hr) || !desktops)
        return false;

    ComPtr<TDesktop> target;
    hr = desktops->GetAt(static_cast<UINT>(index), __uuidof(TDesktop), reinterpret_cast<void**>(target.GetAddressOf()));
    if (FAILED(hr) || !target)
        return false;

    return SUCCEEDED(manager->SwitchDesktop(target.Get()));
}

template <typename TManager, typename TDesktop>
static bool GetDesktopIdWithManager(TManager* manager, int index, GUID* id)
{
    if (!manager || !id || index < 0)
        return false;

    UINT count = 0;
    HRESULT hr = manager->GetCount(&count);
    if (FAILED(hr) || index >= static_cast<int>(count))
        return false;

    ComPtr<IObjectArray> desktops;
    hr = manager->GetDesktops(desktops.GetAddressOf());
    if (FAILED(hr) || !desktops)
        return false;

    ComPtr<TDesktop> desktop;
    hr = desktops->GetAt(static_cast<UINT>(index), __uuidof(TDesktop), reinterpret_cast<void**>(desktop.GetAddressOf()));
    if (FAILED(hr) || !desktop)
        return false;

    return SUCCEEDED(desktop->GetId(id));
}

static bool QueryManagers(IServiceProvider* serviceProvider)
{
    if (!serviceProvider)
        return false;

    HRESULT hr = serviceProvider->QueryService(
        CLSID_VirtualDesktopManagerInternal,
        __uuidof(IVirtualDesktopManagerInternal11),
        reinterpret_cast<void**>(s_manager11.GetAddressOf()));
    if (SUCCEEDED(hr) && s_manager11)
    {
        s_api = VirtualDesktopApi::Win11;
        return true;
    }

    hr = serviceProvider->QueryService(
        CLSID_VirtualDesktopManagerInternal,
        __uuidof(IVirtualDesktopManagerInternal10),
        reinterpret_cast<void**>(s_manager10.GetAddressOf()));
    if (SUCCEEDED(hr) && s_manager10)
    {
        s_api = VirtualDesktopApi::Win10;
        return true;
    }

    hr = serviceProvider->QueryService(
        CLSID_VirtualDesktopManagerInternal,
        __uuidof(IVirtualDesktopManagerInternal10Alt),
        reinterpret_cast<void**>(s_manager10Alt.GetAddressOf()));
    if (SUCCEEDED(hr) && s_manager10Alt)
    {
        s_api = VirtualDesktopApi::Win10Alt;
        return true;
    }

    return false;
}

HRESULT VirtualDesktop_Init()
{
    VirtualDesktop_Shutdown();

    ComPtr<IServiceProvider> serviceProvider;
    HRESULT hr = CoCreateInstance(
        CLSID_ImmersiveShell,
        nullptr,
        CLSCTX_LOCAL_SERVER,
        IID_PPV_ARGS(&serviceProvider));
    if (FAILED(hr))
        return hr;

    if (!QueryManagers(serviceProvider.Get()))
        return E_NOINTERFACE;

    CoCreateInstance(
        CLSID_VirtualDesktopManager,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&s_publicManager));

    VirtualDesktop_Refresh();
    return S_OK;
}

void VirtualDesktop_Shutdown()
{
    s_manager11.Reset();
    s_manager10.Reset();
    s_manager10Alt.Reset();
    s_publicManager.Reset();
    s_api = VirtualDesktopApi::None;
    s_snapshot = {};
}

bool VirtualDesktop_Refresh()
{
    VirtualDesktopSnapshot next = {};
    bool ok = false;

    switch (s_api)
    {
    case VirtualDesktopApi::Win11:
        ok = RefreshFromManager<IVirtualDesktopManagerInternal11, IVirtualDesktop11>(s_manager11.Get(), next);
        break;
    case VirtualDesktopApi::Win10:
        ok = RefreshFromManager<IVirtualDesktopManagerInternal10, IVirtualDesktop10>(s_manager10.Get(), next);
        break;
    case VirtualDesktopApi::Win10Alt:
        ok = RefreshFromManager<IVirtualDesktopManagerInternal10Alt, IVirtualDesktop10>(s_manager10Alt.Get(), next);
        break;
    default:
        ok = false;
        break;
    }

    s_snapshot = ok ? next : VirtualDesktopSnapshot{};
    return ok;
}

VirtualDesktopSnapshot VirtualDesktop_GetSnapshot()
{
    return s_snapshot;
}

bool VirtualDesktop_GetDesktopId(int index, GUID* id)
{
    if (!id)
        return false;

    *id = {};
    switch (s_api)
    {
    case VirtualDesktopApi::Win11:
        return GetDesktopIdWithManager<IVirtualDesktopManagerInternal11, IVirtualDesktop11>(s_manager11.Get(), index, id);
    case VirtualDesktopApi::Win10:
        return GetDesktopIdWithManager<IVirtualDesktopManagerInternal10, IVirtualDesktop10>(s_manager10.Get(), index, id);
    case VirtualDesktopApi::Win10Alt:
        return GetDesktopIdWithManager<IVirtualDesktopManagerInternal10Alt, IVirtualDesktop10>(s_manager10Alt.Get(), index, id);
    default:
        return false;
    }
}

bool VirtualDesktop_GetWindowDesktopId(HWND hwnd, GUID* id)
{
    if (!s_publicManager || !hwnd || !id)
        return false;

    *id = {};
    return SUCCEEDED(s_publicManager->GetWindowDesktopId(hwnd, id));
}

bool VirtualDesktop_SwitchToIndex(int index)
{
    bool ok = false;

    switch (s_api)
    {
    case VirtualDesktopApi::Win11:
        ok = SwitchWithManager<IVirtualDesktopManagerInternal11, IVirtualDesktop11>(s_manager11.Get(), index);
        break;
    case VirtualDesktopApi::Win10:
        ok = SwitchWithManager<IVirtualDesktopManagerInternal10, IVirtualDesktop10>(s_manager10.Get(), index);
        break;
    case VirtualDesktopApi::Win10Alt:
        ok = SwitchWithManager<IVirtualDesktopManagerInternal10Alt, IVirtualDesktop10>(s_manager10Alt.Get(), index);
        break;
    default:
        ok = false;
        break;
    }

    VirtualDesktop_Refresh();
    return ok;
}
