#include "pch.h"
#include "DataManager.h"
#include "../utilities/IniHelper.h"
#include "../utilities/Common.h"
#include "../utilities/bass64/base64.h"
#include "../utilities/yyjson/yyjson.h"
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <wincrypt.h>
#include <winhttp.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    std::wstring TrimTrailingSlash(std::wstring value)
    {
        while (!value.empty() && (value.back() == L'/' || value.back() == L'\\'))
            value.pop_back();
        return value;
    }

    std::string ToUtf8(const std::wstring& value)
    {
        return utilities::StringHelper::UnicodeToStr(value.c_str(), true);
    }

    std::wstring FromUtf8(const char* value)
    {
        return utilities::StringHelper::StrToUnicode(value, true);
    }

    std::string EscapeJsonString(const std::wstring& value)
    {
        std::string text = ToUtf8(value);
        std::string out;
        out.reserve(text.size() + 8);
        for (char ch : text)
        {
        switch (ch)
        {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
                char buff[8];
                sprintf_s(buff, "\\u%04x", static_cast<unsigned char>(ch));
                out += buff;
            }
            else
            {
                out.push_back(ch);
            }
            break;
        }
        }
        return out;
    }

    int64_t JsonInt(yyjson_val* obj, const char* key)
    {
        yyjson_val* value = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
        if (value == nullptr || yyjson_is_null(value))
            return 0;
        if (yyjson_is_int(value))
            return yyjson_get_sint(value);
        if (yyjson_is_uint(value))
            return static_cast<int64_t>(yyjson_get_uint(value));
        if (yyjson_is_real(value))
            return static_cast<int64_t>(yyjson_get_real(value));
        if (yyjson_is_str(value))
            return _atoi64(yyjson_get_str(value));
        return 0;
    }

    std::wstring JsonWString(yyjson_val* obj, const char* key)
    {
        yyjson_val* value = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
        return value != nullptr && yyjson_is_str(value) ? FromUtf8(yyjson_get_str(value)) : L"";
    }

    bool JsonBool(yyjson_val* obj, const char* key)
    {
        yyjson_val* value = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
        if (value == nullptr || yyjson_is_null(value))
            return false;
        if (yyjson_is_bool(value))
            return yyjson_get_bool(value);
        if (yyjson_is_int(value))
            return yyjson_get_sint(value) != 0;
        return false;
    }

    std::wstring NowString()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t buff[32]{};
        swprintf_s(buff, L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buff;
    }

    struct WinHttpHandle
    {
        HINTERNET value{};
        ~WinHttpHandle()
        {
            if (value != nullptr)
                WinHttpCloseHandle(value);
        }
        operator HINTERNET() const { return value; }
    };

    std::wstring ExtractCookiePair(const std::wstring& set_cookie)
    {
        size_t end = set_cookie.find(L';');
        std::wstring pair = end == std::wstring::npos ? set_cookie : set_cookie.substr(0, end);
        while (!pair.empty() && (pair.back() == L'\0' || pair.back() == L'\r' || pair.back() == L'\n' || pair.back() == L' '))
            pair.pop_back();
        return pair;
    }
}

CDataManager CDataManager::m_instance;

CDataManager::CDataManager()
{
    HDC hDC = ::GetDC(HWND_DESKTOP);
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ::ReleaseDC(HWND_DESKTOP, hDC);
}

CDataManager::~CDataManager()
{
    SaveConfig();
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(hModule, path, MAX_PATH);
    std::wstring module_path = path;
    if (!config_dir.empty())
    {
        size_t index = module_path.find_last_of(L"\\/");
        std::wstring module_file_name = module_path.substr(index + 1);
        module_file_name = module_file_name.substr(0, module_file_name.find_last_of(L"."));
        m_config_path = config_dir + module_file_name + L".ini";
    }
    else
    {
        m_config_path = module_path.substr(0, module_path.find_last_of(L".")) + L".ini";
    }

    utilities::CIniHelper ini(m_config_path);
    m_setting_data.base_url = ini.GetString(L"config", L"base_url", m_setting_data.base_url.c_str());
    m_setting_data.channel_slug = ini.GetString(L"config", L"channel_slug", m_setting_data.channel_slug.c_str());
    std::wstring default_password = m_setting_data.password;
    std::wstring protected_password = ini.GetString(L"config", L"password", L"");
    if (!protected_password.empty())
        m_setting_data.password = UnprotectPassword(protected_password);
    std::wstring plain_password = ini.GetString(L"config", L"plain_password", L"");
    if (m_setting_data.password.empty() && !plain_password.empty())
        m_setting_data.password = plain_password;
    if (m_setting_data.password.empty())
        m_setting_data.password = default_password;
    m_setting_data.refresh_interval_sec = ini.GetInt(L"config", L"refresh_interval_sec", 30);
    if (m_setting_data.refresh_interval_sec < 10)
        m_setting_data.refresh_interval_sec = 10;
}

void CDataManager::SaveConfig() const
{
    if (m_config_path.empty())
        return;

    utilities::CIniHelper ini(m_config_path);
    ini.WriteString(L"config", L"base_url", TrimTrailingSlash(m_setting_data.base_url));
    ini.WriteString(L"config", L"channel_slug", m_setting_data.channel_slug);
    ini.WriteString(L"config", L"password", ProtectPassword(m_setting_data.password));
    ini.WriteString(L"config", L"plain_password", L"");
    ini.WriteInt(L"config", L"refresh_interval_sec", m_setting_data.refresh_interval_sec < 10 ? 10 : m_setting_data.refresh_interval_sec);
    ini.Save();
}

const CString& CDataManager::StringRes(UINT id)
{
    auto iter = m_string_table.find(id);
    if (iter != m_string_table.end())
        return iter->second;

    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    m_string_table[id].LoadString(id);
    return m_string_table[id];
}

void CDataManager::DPIFromWindow(CWnd* pWnd)
{
    CWindowDC dc(pWnd);
    HDC hDC = dc.GetSafeHdc();
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
}

int CDataManager::DPI(int pixel)
{
    return m_dpi * pixel / 96;
}

float CDataManager::DPIF(float pixel)
{
    return m_dpi * pixel / 96;
}

int CDataManager::RDPI(int pixel)
{
    return pixel * 96 / m_dpi;
}

HICON CDataManager::GetIcon(UINT id)
{
    auto iter = m_icons.find(id);
    if (iter != m_icons.end())
        return iter->second;

    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    HICON hIcon = (HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(id), IMAGE_ICON, DPI(16), DPI(16), 0);
    m_icons[id] = hIcon;
    return hIcon;
}

void CDataManager::Refresh()
{
    RuntimeData data;
    data.channel_slug = m_setting_data.channel_slug;

    std::wstring error;
    if (TrimTrailingSlash(m_setting_data.base_url).empty() || m_setting_data.password.empty() || m_setting_data.channel_slug.empty())
    {
        data.last_error = L"LiveWebUI viewer settings are incomplete.";
        data.last_refresh_time = NowString();
        SetRuntimeData(data);
        return;
    }

    bool ok = true;
    {
        std::lock_guard<std::mutex> lock(m_runtime_mutex);
        ok = !m_session_cookie.empty();
    }
    if (!ok)
        ok = Login(error);

    std::string json;
    int status_code = 0;
    if (ok)
    {
        ok = GetJson(L"/api/admin/channels", json, status_code, error) && ParseChannels(json, data, error);
        if (!ok && (status_code == 401 || error.find(L"401") != std::wstring::npos))
        {
            ClearSession();
            error.clear();
            if (Login(error))
                ok = GetJson(L"/api/admin/channels", json, status_code, error) && ParseChannels(json, data, error);
        }
    }

    data.login_ok = ok && error.empty();
    data.last_error = error;
    data.last_refresh_time = NowString();
    SetRuntimeData(data);
}

void CDataManager::ClearSession()
{
    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    m_session_cookie.clear();
}

RuntimeData CDataManager::GetRuntimeData() const
{
    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    return m_runtime_data;
}

std::wstring CDataManager::GetReadersText() const
{
    RuntimeData data = GetRuntimeData();
    return data.channel_found ? std::to_wstring(data.readers) : L"--";
}

std::wstring CDataManager::BuildTooltip() const
{
    RuntimeData data = GetRuntimeData();
    std::wstringstream ss;
    ss << L"LiveWebUI Viewer";
    ss << L"\nChannel: " << (data.channel_label.empty() ? data.channel_slug : data.channel_label);
    ss << L"\nReaders: " << (data.channel_found ? std::to_wstring(data.readers) : L"--");
    ss << L"\nState: " << data.state << L", online " << (data.online ? L"yes" : L"no");
    ss << L"\nHLS/WebRTC: " << data.hls_readers << L"/" << data.webrtc_readers;
    ss << L"\nLast refresh: " << (data.last_refresh_time.empty() ? L"--" : data.last_refresh_time);
    if (!data.last_error.empty())
        ss << L"\nError: " << data.last_error;
    return ss.str();
}

bool CDataManager::Login(std::wstring& error)
{
    std::string body = "{\"password\":\"" + EscapeJsonString(m_setting_data.password) + "\"}";
    std::string result;
    int status_code = 0;
    if (!PostJson(L"/api/admin/login", body, result, status_code, error))
        return false;

    yyjson_doc* doc = yyjson_read(result.c_str(), result.size(), 0);
    if (doc == nullptr)
    {
        error = L"Login response is not valid JSON.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    bool ok = JsonBool(root, "ok");
    yyjson_doc_free(doc);
    if (!ok)
    {
        error = L"Login failed.";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    if (m_session_cookie.empty())
    {
        error = L"Login response did not return admin_session cookie.";
        return false;
    }
    return true;
}

bool CDataManager::GetJson(const std::wstring& path, std::string& result, int& status_code, std::wstring& error)
{
    return RequestJson(L"GET", path, std::string(), true, result, status_code, error);
}

bool CDataManager::PostJson(const std::wstring& path, const std::string& body, std::string& result, int& status_code, std::wstring& error)
{
    return RequestJson(L"POST", path, body, false, result, status_code, error);
}

bool CDataManager::RequestJson(const wchar_t* method, const std::wstring& path, const std::string& body, bool with_session, std::string& result, int& status_code, std::wstring& error)
{
    result.clear();
    status_code = 0;

    std::wstring url = TrimTrailingSlash(m_setting_data.base_url) + path;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t url_path[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = _countof(host);
    parts.lpszUrlPath = url_path;
    parts.dwUrlPathLength = _countof(url_path);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts))
    {
        error = L"Invalid LiveWebUI URL.";
        return false;
    }

    std::wstring object_name(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo != nullptr && parts.dwExtraInfoLength > 0)
        object_name.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    WinHttpHandle session{ WinHttpOpen(L"LiveWebUIViewer TrafficMonitor Plugin/1.00", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
    if (!session)
    {
        error = L"WinHttpOpen failed.";
        return false;
    }

    WinHttpHandle connect{ WinHttpConnect(session, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(), parts.nPort, 0) };
    if (!connect)
    {
        error = L"WinHttpConnect failed.";
        return false;
    }

    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request{ WinHttpOpenRequest(connect, method, object_name.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) };
    if (!request)
    {
        error = L"WinHttpOpenRequest failed.";
        return false;
    }

    WinHttpSetTimeouts(request, 5000, 5000, 10000, 10000);

    std::wstring headers = L"Accept: application/json\r\n";
    if (_wcsicmp(method, L"POST") == 0)
        headers += L"Content-Type: application/json\r\n";
    if (with_session)
    {
        std::lock_guard<std::mutex> lock(m_runtime_mutex);
        if (!m_session_cookie.empty())
            headers += L"Cookie: " + m_session_cookie + L"\r\n";
    }

    BOOL ok = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);
    if (!ok || !WinHttpReceiveResponse(request, nullptr))
    {
        error = L"HTTP request failed.";
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX))
        status_code = static_cast<int>(status);

    if (_wcsicmp(method, L"POST") == 0)
    {
        DWORD cookie_size = 0;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &cookie_size, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && cookie_size > 0)
        {
            std::wstring set_cookie(cookie_size / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, &set_cookie[0], &cookie_size, WINHTTP_NO_HEADER_INDEX))
            {
                std::wstring cookie_pair = ExtractCookiePair(set_cookie);
                if (cookie_pair.find(L"admin_session=") == 0)
                {
                    std::lock_guard<std::mutex> lock(m_runtime_mutex);
                    m_session_cookie = cookie_pair;
                }
            }
        }
    }

    DWORD available = 0;
    do
    {
        available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
            break;
        if (available == 0)
            break;
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read))
            break;
        result.append(buffer.data(), read);
    } while (available > 0);

    if (status_code < 200 || status_code >= 300)
    {
        error = L"HTTP " + std::to_wstring(status_code);
        return false;
    }
    return true;
}

bool CDataManager::ParseChannels(const std::string& json, RuntimeData& data, std::wstring& error) const
{
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (doc == nullptr)
    {
        error = L"Channels response is not valid JSON.";
        return false;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (root == nullptr || !yyjson_is_arr(root))
    {
        yyjson_doc_free(doc);
        error = L"Channels response is not an array.";
        return false;
    }

    std::string target_slug = ToUtf8(m_setting_data.channel_slug);
    yyjson_val* item = nullptr;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(root, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != nullptr)
    {
        yyjson_val* slug_value = yyjson_obj_get(item, "slug");
        const char* slug = slug_value != nullptr && yyjson_is_str(slug_value) ? yyjson_get_str(slug_value) : "";
        if (target_slug == slug)
        {
            data.channel_found = true;
            data.channel_slug = FromUtf8(slug);
            data.channel_label = JsonWString(item, "label");
            data.state = JsonWString(item, "state");
            if (data.state.empty())
                data.state = L"--";
            data.online = JsonBool(item, "online");
            data.readers = JsonInt(item, "readers");
            data.hls_readers = JsonInt(item, "hlsReaders");
            data.webrtc_readers = JsonInt(item, "webrtcReaders");
            break;
        }
    }
    yyjson_doc_free(doc);

    if (!data.channel_found)
    {
        error = L"Channel not found: " + m_setting_data.channel_slug;
        return false;
    }
    return true;
}

std::wstring CDataManager::ProtectPassword(const std::wstring& plain) const
{
    if (plain.empty())
        return L"";
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(plain.data()));
    input.cbData = static_cast<DWORD>(plain.size() * sizeof(wchar_t));
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"LiveWebUIViewer", nullptr, nullptr, nullptr, 0, &output))
        return L"";
    std::string bytes(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return FromUtf8(utilities::Base64Encode(bytes).c_str());
}

std::wstring CDataManager::UnprotectPassword(const std::wstring& encoded) const
{
    if (encoded.empty())
        return L"";
    std::string bytes = utilities::Base64Decode(ToUtf8(encoded));
    if (bytes.empty())
        return L"";
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()));
    input.cbData = static_cast<DWORD>(bytes.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
        return L"";
    std::wstring plain(reinterpret_cast<wchar_t*>(output.pbData), output.cbData / sizeof(wchar_t));
    LocalFree(output.pbData);
    return plain;
}

void CDataManager::SetRuntimeData(const RuntimeData& data)
{
    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    std::wstring session_cookie = m_session_cookie;
    m_runtime_data = data;
    m_session_cookie = session_cookie;
}
