#include "updater.h"

#include <shellapi.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr wchar_t GITHUB_HOST[] = L"api.github.com";
constexpr wchar_t LATEST_RELEASE_PATH[] = L"/repos/Krevon-Studios/Krevon-Station/releases/latest";
constexpr wchar_t APP_NAME[] = L"Krevon Station";

std::atomic_bool g_checkRunning = false;

struct UpdateInfo
{
    std::wstring version;
    std::wstring installerUrl;
};

struct HttpResult
{
    bool ok = false;
    DWORD status = 0;
    std::string body;
    std::wstring error;
};

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
        return {};

    const int len = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (len <= 0)
        return {};

    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), len);
    return out;
}

std::wstring GetLastErrorMessage(const wchar_t* context)
{
    wchar_t* buffer = nullptr;
    const DWORD error = GetLastError();
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message = context;
    if (buffer)
    {
        message += L": ";
        message += buffer;
        LocalFree(buffer);
    }
    else
    {
        wchar_t code[32];
        swprintf_s(code, L": error %lu", error);
        message += code;
    }
    return message;
}

std::wstring GetCurrentVersion()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(exePath, &handle);
    if (size == 0)
        return L"0.0.0";

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(exePath, 0, size, data.data()))
        return L"0.0.0";

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoLen = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoLen) || !info)
        return L"0.0.0";

    wchar_t version[64];
    swprintf_s(
        version,
        L"%u.%u.%u",
        HIWORD(info->dwProductVersionMS),
        LOWORD(info->dwProductVersionMS),
        HIWORD(info->dwProductVersionLS));
    return version;
}

std::array<int, 4> ParseVersion(const std::wstring& value)
{
    std::array<int, 4> parts = { 0, 0, 0, 0 };
    size_t index = 0;
    size_t partIndex = 0;

    while (index < value.size() && (value[index] == L'v' || value[index] == L'V' || iswspace(value[index])))
        ++index;

    while (index < value.size() && partIndex < parts.size())
    {
        if (!iswdigit(value[index]))
            break;

        int number = 0;
        while (index < value.size() && iswdigit(value[index]))
        {
            number = number * 10 + (value[index] - L'0');
            ++index;
        }
        parts[partIndex++] = number;

        if (index >= value.size() || value[index] != L'.')
            break;
        ++index;
    }

    return parts;
}

bool IsNewerVersion(const std::wstring& latest, const std::wstring& current)
{
    return ParseVersion(latest) > ParseVersion(current);
}

std::string ToLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<std::string> ExtractJsonString(const std::string& json, const std::string& key, size_t start = 0)
{
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = json.find(needle, start);
    if (keyPos == std::string::npos)
        return std::nullopt;

    const size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos)
        return std::nullopt;

    const size_t firstQuote = json.find('"', colon + 1);
    if (firstQuote == std::string::npos)
        return std::nullopt;

    std::string out;
    bool escape = false;
    for (size_t i = firstQuote + 1; i < json.size(); ++i)
    {
        const char ch = json[i];
        if (escape)
        {
            switch (ch)
            {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(ch); break;
            }
            escape = false;
            continue;
        }
        if (ch == '\\')
        {
            escape = true;
            continue;
        }
        if (ch == '"')
            return out;
        out.push_back(ch);
    }

    return std::nullopt;
}

std::optional<std::string> FindInstallerUrl(const std::string& json)
{
    const size_t assetsPos = json.find("\"assets\"");
    size_t pos = (assetsPos == std::string::npos) ? 0 : assetsPos;
    std::optional<std::string> firstExe;

    while ((pos = json.find("\"browser_download_url\"", pos)) != std::string::npos)
    {
        auto url = ExtractJsonString(json, "browser_download_url", pos);
        if (!url)
            break;

        const std::string lower = ToLowerAscii(*url);
        const bool isExe = lower.find(".exe") != std::string::npos;
        if (isExe && !firstExe)
            firstExe = url;

        if (isExe &&
            lower.find("krevon") != std::string::npos &&
            lower.find("setup") != std::string::npos)
        {
            return url;
        }

        pos += 24;
    }

    return firstExe;
}

HttpResult HttpGetText(const wchar_t* host, const wchar_t* path)
{
    HttpResult result;

    HINTERNET session = WinHttpOpen(
        L"KrevonStation/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session)
    {
        result.error = GetLastErrorMessage(L"Could not start the update request");
        return result;
    }

    HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect)
    {
        result.error = GetLastErrorMessage(L"Could not connect to GitHub");
        WinHttpCloseHandle(session);
        return result;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"GET",
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!request)
    {
        result.error = GetLastErrorMessage(L"Could not create the update request");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    const wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";

    if (!WinHttpSendRequest(
            request,
            headers,
            static_cast<DWORD>(wcslen(headers)),
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(request, nullptr))
    {
        result.error = GetLastErrorMessage(L"GitHub did not return update information");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);
    result.status = status;

    DWORD available = 0;
    do
    {
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            result.error = GetLastErrorMessage(L"Could not read update information");
            break;
        }
        if (available == 0)
            break;

        std::string buffer(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read))
        {
            result.error = GetLastErrorMessage(L"Could not read update information");
            break;
        }
        buffer.resize(read);
        result.body += buffer;
    } while (available > 0);

    result.ok = result.error.empty() && status >= 200 && status < 300;
    if (!result.ok && result.error.empty())
    {
        wchar_t message[96];
        swprintf_s(message, L"GitHub returned HTTP %lu while checking for updates.", status);
        result.error = message;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

bool DownloadFile(const std::wstring& url, const std::wstring& targetPath, std::wstring& error)
{
    URL_COMPONENTSW parts = {};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
    {
        error = GetLastErrorMessage(L"The update download URL is invalid");
        return false;
    }

    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    HINTERNET session = WinHttpOpen(
        L"KrevonStation/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session)
    {
        error = GetLastErrorMessage(L"Could not start the installer download");
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    if (!connect)
    {
        error = GetLastErrorMessage(L"Could not connect to the download server");
        WinHttpCloseHandle(session);
        return false;
    }

    const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!request)
    {
        error = GetLastErrorMessage(L"Could not create the installer download request");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    bool success =
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr);
    if (!success)
    {
        error = GetLastErrorMessage(L"The installer download failed");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300)
    {
        wchar_t message[96];
        swprintf_s(message, L"The installer download returned HTTP %lu.", status);
        error = message;
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    HANDLE file = CreateFileW(
        targetPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastErrorMessage(L"Could not create the installer file");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD available = 0;
    do
    {
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            error = GetLastErrorMessage(L"Could not read the installer download");
            success = false;
            break;
        }
        if (available == 0)
            break;

        std::vector<BYTE> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read))
        {
            error = GetLastErrorMessage(L"Could not read the installer download");
            success = false;
            break;
        }

        DWORD written = 0;
        if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read)
        {
            error = GetLastErrorMessage(L"Could not save the installer download");
            success = false;
            break;
        }
    } while (available > 0);

    CloseHandle(file);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (!success)
        DeleteFileW(targetPath.c_str());
    return success;
}

std::optional<UpdateInfo> FetchLatestUpdate(std::wstring& error)
{
    HttpResult response = HttpGetText(GITHUB_HOST, LATEST_RELEASE_PATH);
    if (!response.ok)
    {
        error = response.error;
        return std::nullopt;
    }

    auto tag = ExtractJsonString(response.body, "tag_name");
    auto installerUrl = FindInstallerUrl(response.body);
    if (!tag)
    {
        error = L"The latest GitHub release is missing a version tag.";
        return std::nullopt;
    }
    if (!installerUrl)
    {
        error = L"The latest GitHub release does not include a Windows installer asset.";
        return std::nullopt;
    }

    UpdateInfo info;
    info.version = Utf8ToWide(*tag);
    if (!info.version.empty() && (info.version[0] == L'v' || info.version[0] == L'V'))
        info.version.erase(info.version.begin());
    info.installerUrl = Utf8ToWide(*installerUrl);
    return info;
}

std::wstring MakeInstallerTempPath(const std::wstring& version)
{
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);

    std::wstring cleanVersion = version;
    for (wchar_t& ch : cleanVersion)
    {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
            ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
        {
            ch = L'_';
        }
    }

    wchar_t fileName[MAX_PATH];
    swprintf_s(fileName, L"Krevon Station Setup %s.exe", cleanVersion.c_str());

    std::wstring path = tempDir;
    path += fileName;
    return path;
}

HWND SafeOwner(HWND owner)
{
    return IsWindow(owner) ? owner : nullptr;
}

void ShowErrorIfManual(HWND owner, bool userInitiated, const std::wstring& error)
{
    if (!userInitiated)
        return;

    std::wstring message = L"Could not check for updates.";
    if (!error.empty())
    {
        message += L"\n\n";
        message += error;
    }
    MessageBoxW(SafeOwner(owner), message.c_str(), APP_NAME, MB_OK | MB_ICONERROR);
}
}

void Updater_CheckForUpdatesAsync(HWND owner, bool userInitiated)
{
    bool expected = false;
    if (!g_checkRunning.compare_exchange_strong(expected, true))
    {
        if (userInitiated)
        {
            MessageBoxW(
                SafeOwner(owner),
                L"Krevon Station is already checking for updates.",
                APP_NAME,
                MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    std::thread([owner, userInitiated]() {
        const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        std::wstring error;
        auto latest = FetchLatestUpdate(error);
        if (!latest)
        {
            ShowErrorIfManual(owner, userInitiated, error);
            g_checkRunning = false;
            if (SUCCEEDED(coInit))
                CoUninitialize();
            return;
        }

        const std::wstring currentVersion = GetCurrentVersion();
        if (!IsNewerVersion(latest->version, currentVersion))
        {
            if (userInitiated)
            {
                std::wstring message = L"You're up to date.\n\nKrevon Station ";
                message += currentVersion;
                message += L" is installed.";
                MessageBoxW(SafeOwner(owner), message.c_str(), APP_NAME, MB_OK | MB_ICONINFORMATION);
            }
            g_checkRunning = false;
            if (SUCCEEDED(coInit))
                CoUninitialize();
            return;
        }

        std::wstring prompt = L"Krevon Station ";
        prompt += latest->version;
        prompt += L" is available.\n\nYou have ";
        prompt += currentVersion;
        prompt += L". Download and install the update now?";

        const int choice = MessageBoxW(
            SafeOwner(owner),
            prompt.c_str(),
            APP_NAME,
            MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1);
        if (choice != IDYES)
        {
            g_checkRunning = false;
            if (SUCCEEDED(coInit))
                CoUninitialize();
            return;
        }

        const std::wstring installerPath = MakeInstallerTempPath(latest->version);
        std::wstring downloadError;
        if (!DownloadFile(latest->installerUrl, installerPath, downloadError))
        {
            ShowErrorIfManual(owner, true, downloadError);
            g_checkRunning = false;
            if (SUCCEEDED(coInit))
                CoUninitialize();
            return;
        }

        HINSTANCE launched = ShellExecuteW(
            SafeOwner(owner),
            L"open",
            installerPath.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(launched) <= 32)
        {
            ShowErrorIfManual(owner, true, L"Windows could not start the downloaded installer.");
            g_checkRunning = false;
            if (SUCCEEDED(coInit))
                CoUninitialize();
            return;
        }

        if (IsWindow(owner))
            PostMessageW(owner, WM_CLOSE, 0, 0);

        g_checkRunning = false;
        if (SUCCEEDED(coInit))
            CoUninitialize();
    }).detach();
}
