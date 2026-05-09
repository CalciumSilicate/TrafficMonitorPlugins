#include "pch.h"
#include "DataManager.h"
#include "../utilities/IniHelper.h"
#include "../utilities/Common.h"
#include "../utilities/JsonHelper.h"
#include "../utilities/bass64/base64.h"
#include "../utilities/yyjson/yyjson.h"
#include <algorithm>
#include <climits>
#include <iomanip>
#include <sstream>
#include <vector>
#include <wincrypt.h>
#include <winhttp.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    constexpr size_t MetricCount = static_cast<size_t>(AmpMetricId::Count);

    size_t MetricIndex(AmpMetricId id)
    {
        return static_cast<size_t>(id);
    }

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

    std::wstring FormatInt(long long value)
    {
        return std::to_wstring(value);
    }

    std::wstring FormatCny(long long cent)
    {
        std::wstringstream ss;
        ss << L"CNY " << std::fixed << std::setprecision(2) << (static_cast<double>(cent) / 100.0);
        return ss.str();
    }

    std::wstring FormatMicrosAsUsd(int64_t micros)
    {
        std::wstringstream ss;
        ss << L"$" << std::fixed << std::setprecision(4) << (static_cast<double>(micros) / 1000000.0);
        return ss.str();
    }

    std::wstring NormalizeUsdString(const char* value, int64_t fallback_micros)
    {
        if (value != nullptr && value[0] != '\0')
        {
            std::wstring text = FromUtf8(value);
            if (!text.empty() && text[0] != L'$')
                text = L"$" + text;
            return text;
        }
        return FormatMicrosAsUsd(fallback_micros);
    }

    std::wstring ShortDateTime(yyjson_val* value)
    {
        if (value == nullptr || yyjson_is_null(value))
            return L"--";
        const char* str = yyjson_get_str(value);
        if (str == nullptr || str[0] == '\0')
            return L"--";
        std::wstring text = FromUtf8(str);
        if (text.size() >= 10)
            return text.substr(0, 10);
        return text;
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

    double JsonReal(yyjson_val* obj, const char* key)
    {
        yyjson_val* value = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
        if (value == nullptr || yyjson_is_null(value))
            return 0;
        if (yyjson_is_real(value))
            return yyjson_get_real(value);
        if (yyjson_is_int(value))
            return static_cast<double>(yyjson_get_sint(value));
        if (yyjson_is_uint(value))
            return static_cast<double>(yyjson_get_uint(value));
        if (yyjson_is_str(value))
            return atof(yyjson_get_str(value));
        return 0;
    }

    yyjson_val* JsonObj(yyjson_val* obj, const char* key)
    {
        yyjson_val* value = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
        return value != nullptr && yyjson_is_obj(value) ? value : nullptr;
    }

    yyjson_val* JsonArr(yyjson_val* obj, const char* key)
    {
        yyjson_val* value = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
        return value != nullptr && yyjson_is_arr(value) ? value : nullptr;
    }

    std::wstring JsonWString(yyjson_val* obj, const char* key)
    {
        return utilities::JsonHelper::GetJsonWString(obj, key);
    }

    const char* JsonCString(yyjson_val* obj, const char* key)
    {
        yyjson_val* value = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
        return value != nullptr && yyjson_is_str(value) ? yyjson_get_str(value) : nullptr;
    }

    std::wstring BuildMetricKey(const MetricDefinition& definition)
    {
        return definition.item_id;
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
}

SettingData::SettingData()
{
    enabled_metrics.fill(true);
}

CDataManager CDataManager::m_instance;

CDataManager::CDataManager()
{
    HDC hDC = ::GetDC(HWND_DESKTOP);
    m_dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ::ReleaseDC(HWND_DESKTOP, hDC);

    m_metric_definitions = {
        { AmpMetricId::Status, L"AMP status", L"AMPStatus", L"AMP", L"OK", false },
        { AmpMetricId::TodayRequests, L"AMP today requests", L"AMPTodayReq", L"Req", L"999999", false },
        { AmpMetricId::TodayCost, L"AMP today cost", L"AMPTodayCost", L"Cost", L"$999.9999", false },
        { AmpMetricId::Balance, L"AMP balance", L"AMPBalance", L"Bal", L"$999.9999", false },
        { AmpMetricId::SubscriptionLeft, L"AMP subscription left", L"AMPSubLeft", L"Sub", L"$999.9999", false },
        { AmpMetricId::SubscriptionExpires, L"AMP subscription expires", L"AMPSubExp", L"Exp", L"2099-12-31", false },
        { AmpMetricId::AdminConcurrency, L"AMP admin concurrency", L"AMPAdmConc", L"Conc", L"9999", true },
        { AmpMetricId::TodayRevenue, L"AMP today revenue", L"AMPTodayRev", L"RevD", L"CNY 99999.99", true },
        { AmpMetricId::MonthRevenue, L"AMP month revenue", L"AMPMonthRev", L"RevM", L"CNY 999999.99", true },
    };
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
    m_setting_data.username = ini.GetString(L"config", L"username", L"");
    m_setting_data.password = UnprotectPassword(ini.GetString(L"config", L"password", L""));
    m_setting_data.refresh_interval_sec = ini.GetInt(L"config", L"refresh_interval_sec", 30);
    if (m_setting_data.refresh_interval_sec < 10)
        m_setting_data.refresh_interval_sec = 10;

    for (const auto& definition : m_metric_definitions)
    {
        const size_t index = MetricIndex(definition.id);
        m_setting_data.enabled_metrics[index] = ini.GetBool(L"metrics", BuildMetricKey(definition).c_str(), true);
    }
}

void CDataManager::SaveConfig() const
{
    if (m_config_path.empty())
        return;

    utilities::CIniHelper ini(m_config_path);
    ini.WriteString(L"config", L"base_url", TrimTrailingSlash(m_setting_data.base_url));
    ini.WriteString(L"config", L"username", m_setting_data.username);
    ini.WriteString(L"config", L"password", ProtectPassword(m_setting_data.password));
    ini.WriteInt(L"config", L"refresh_interval_sec", std::max(10, m_setting_data.refresh_interval_sec));
    for (const auto& definition : m_metric_definitions)
    {
        const size_t index = MetricIndex(definition.id);
        ini.WriteBool(L"metrics", BuildMetricKey(definition).c_str(), m_setting_data.enabled_metrics[index]);
    }
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
    data.username = m_setting_data.username;

    std::wstring error;
    if (TrimTrailingSlash(m_setting_data.base_url).empty() || m_setting_data.username.empty() || m_setting_data.password.empty())
    {
        data.last_error = L"AMP Manager settings are incomplete.";
        data.last_refresh_time = NowString();
        SetRuntimeData(data);
        return;
    }

    bool ok = false;
    if (m_token.empty())
        ok = Login(error);
    else
        ok = true;

    if (ok)
    {
        ok = RefreshWithCurrentToken(data, error);
        if (!ok && error.find(L"401") != std::wstring::npos)
        {
            ClearToken();
            error.clear();
            if (Login(error))
                ok = RefreshWithCurrentToken(data, error);
        }
    }

    data.login_ok = ok && error.empty();
    data.last_error = error;
    data.last_refresh_time = NowString();
    SetRuntimeData(data);
}

void CDataManager::ClearToken()
{
    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    m_token.clear();
}

RuntimeData CDataManager::GetRuntimeData() const
{
    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    return m_runtime_data;
}

std::wstring CDataManager::GetMetricValue(AmpMetricId id) const
{
    RuntimeData data = GetRuntimeData();
    switch (id)
    {
    case AmpMetricId::Status:
        return data.has_status_dashboard ? data.overall_status : L"--";
    case AmpMetricId::TodayRequests:
        return data.has_admin_dashboard && data.is_admin ? FormatInt(data.admin_today_requests) : FormatInt(data.today_requests);
    case AmpMetricId::TodayCost:
        return data.has_admin_dashboard && data.is_admin ? data.admin_today_cost_usd : data.today_cost_usd;
    case AmpMetricId::Balance:
        return data.balance_usd;
    case AmpMetricId::SubscriptionLeft:
        return data.subscription_left_usd;
    case AmpMetricId::SubscriptionExpires:
        return data.subscription_expires;
    case AmpMetricId::AdminConcurrency:
        return data.has_admin_dashboard ? FormatInt(data.admin_current_concurrency) : L"--";
    case AmpMetricId::TodayRevenue:
        return data.has_purchase_stats ? FormatCny(data.today_revenue_cny_cent) : L"--";
    case AmpMetricId::MonthRevenue:
        return data.has_purchase_stats ? FormatCny(data.month_revenue_cny_cent) : L"--";
    default:
        return L"--";
    }
}

std::wstring CDataManager::BuildTooltip() const
{
    RuntimeData data = GetRuntimeData();
    std::wstringstream ss;
    ss << L"AMP Manager";
    if (!data.username.empty())
        ss << L" (" << data.username << L")";
    ss << L"\nLast refresh: " << (data.last_refresh_time.empty() ? L"--" : data.last_refresh_time);
    if (!data.last_error.empty())
        ss << L"\nError: " << data.last_error;
    ss << L"\nStatus: " << data.overall_status << L"  OK " << data.status_operational << L"/" << data.status_total;
    ss << L"\nToday: " << data.today_requests << L" requests, " << data.today_cost_usd;
    ss << L"\nBalance: " << data.balance_usd << L", concurrency " << data.user_current_concurrency << L"/" << data.user_concurrency_limit;
    ss << L"\nSubscription: " << data.subscription_name << L", left " << data.subscription_left_usd << L", expires " << data.subscription_expires;
    if (data.is_admin)
    {
        ss << L"\nAdmin: users " << data.admin_user_count << L", concurrency " << data.admin_current_concurrency
            << L", today cost " << data.admin_today_cost_usd;
        ss << L"\nPurchase: today " << FormatCny(data.today_revenue_cny_cent) << L" (" << data.today_sales_count
            << L" orders), month " << FormatCny(data.month_revenue_cny_cent) << L" (" << data.month_sales_count << L" orders)";
    }
    return ss.str();
}

bool CDataManager::IsMetricVisible(AmpMetricId id) const
{
    const size_t index = MetricIndex(id);
    if (index >= m_setting_data.enabled_metrics.size() || !m_setting_data.enabled_metrics[index])
        return false;
    if (!IsAdminMetric(id))
        return true;
    return GetRuntimeData().is_admin;
}

bool CDataManager::IsAdminMetric(AmpMetricId id) const
{
    return GetMetricDefinition(id).admin_only;
}

const MetricDefinition& CDataManager::GetMetricDefinition(AmpMetricId id) const
{
    const size_t index = MetricIndex(id);
    return m_metric_definitions[index];
}

const std::vector<MetricDefinition>& CDataManager::GetMetricDefinitions() const
{
    return m_metric_definitions;
}

bool CDataManager::Login(std::wstring& error)
{
    std::string body = "{\"username\":\"" + EscapeJsonString(m_setting_data.username) + "\",\"password\":\"" + EscapeJsonString(m_setting_data.password) + "\"}";
    std::string result;
    int status_code = 0;
    if (!PostJson(L"/api/manage/auth/login", body, result, status_code, error))
        return false;

    yyjson_doc* doc = yyjson_read(result.c_str(), result.size(), 0);
    if (doc == nullptr)
    {
        error = L"Login response is not valid JSON.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    std::wstring token = JsonWString(root, "token");
    yyjson_val* is_admin_value = yyjson_obj_get(root, "isAdmin");
    bool is_admin = is_admin_value != nullptr && yyjson_get_bool(is_admin_value);
    yyjson_doc_free(doc);

    if (token.empty())
    {
        error = L"Login succeeded without token.";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    m_token = token;
    m_runtime_data.is_admin = is_admin;
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

bool CDataManager::RequestJson(const wchar_t* method, const std::wstring& path, const std::string& body, bool with_auth, std::string& result, int& status_code, std::wstring& error)
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
        error = L"Invalid AMP Manager URL.";
        return false;
    }

    std::wstring object_name(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo != nullptr && parts.dwExtraInfoLength > 0)
        object_name.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    WinHttpHandle session{ WinHttpOpen(L"AMPManager TrafficMonitor Plugin/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
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
    if (with_auth)
    {
        std::lock_guard<std::mutex> lock(m_runtime_mutex);
        if (!m_token.empty())
            headers += L"Authorization: Bearer " + m_token + L"\r\n";
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

bool CDataManager::RefreshWithCurrentToken(RuntimeData& data, std::wstring& error)
{
    {
        std::lock_guard<std::mutex> lock(m_runtime_mutex);
        data.is_admin = m_runtime_data.is_admin;
    }

    std::string json;
    int status_code = 0;
    if (!GetJson(L"/api/me/dashboard", json, status_code, error) || !ParseUserDashboard(json, data, error))
        return false;

    if (!GetJson(L"/api/me/billing/state", json, status_code, error) || !ParseBillingState(json, data, error))
        return false;

    std::wstring status_error;
    if (GetJson(L"/api/me/status/dashboard?period=1h", json, status_code, status_error))
        ParseStatusDashboard(json, data, status_error);

    if (data.is_admin)
    {
        std::wstring admin_error;
        if (GetJson(L"/api/admin/dashboard?throughputWindow=24h", json, status_code, admin_error))
            ParseAdminDashboard(json, data, admin_error);
        if (GetJson(L"/api/admin/purchase/stats", json, status_code, admin_error))
            ParsePurchaseStats(json, data, admin_error);
    }

    error.clear();
    return true;
}

bool CDataManager::ParseUserDashboard(const std::string& json, RuntimeData& data, std::wstring& error) const
{
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (doc == nullptr)
    {
        error = L"User dashboard JSON parse failed.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* balance = JsonObj(root, "balance");
    yyjson_val* today = JsonObj(root, "today");
    data.balance_usd = NormalizeUsdString(JsonCString(balance, "balanceUsd"), JsonInt(balance, "balanceMicros"));
    data.user_current_concurrency = static_cast<int>(JsonInt(balance, "currentConcurrency"));
    data.user_concurrency_limit = static_cast<int>(JsonInt(balance, "concurrencyLimit"));
    data.today_requests = JsonInt(today, "requestCount");
    data.today_cost_usd = NormalizeUsdString(JsonCString(today, "costUsd"), JsonInt(today, "costMicros"));
    data.has_user_dashboard = true;
    yyjson_doc_free(doc);
    return true;
}

bool CDataManager::ParseBillingState(const std::string& json, RuntimeData& data, std::wstring& error) const
{
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (doc == nullptr)
    {
        error = L"Billing state JSON parse failed.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    data.balance_usd = NormalizeUsdString(JsonCString(root, "balanceUsd"), JsonInt(root, "balanceMicros"));

    yyjson_val* subscription = JsonObj(root, "subscription");
    if (subscription != nullptr)
    {
        data.subscription_name = JsonWString(subscription, "planName");
        if (data.subscription_name.empty())
            data.subscription_name = L"--";
        data.subscription_expires = ShortDateTime(yyjson_obj_get(subscription, "expiresAt"));
        if (data.subscription_expires == L"--")
            data.subscription_expires = L"unlimited";
    }

    int64_t min_left = LLONG_MAX;
    yyjson_val* windows = JsonArr(root, "windows");
    if (windows != nullptr)
    {
        yyjson_val* item = nullptr;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(windows, &iter);
        while ((item = yyjson_arr_iter_next(&iter)) != nullptr)
        {
            min_left = std::min(min_left, JsonInt(item, "leftMicros"));
        }
    }
    if (min_left != LLONG_MAX)
        data.subscription_left_usd = FormatMicrosAsUsd(min_left);
    else if (subscription == nullptr)
        data.subscription_left_usd = L"--";
    else
        data.subscription_left_usd = L"unlimited";

    data.has_billing_state = true;
    yyjson_doc_free(doc);
    return true;
}

bool CDataManager::ParseStatusDashboard(const std::string& json, RuntimeData& data, std::wstring& error) const
{
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (doc == nullptr)
    {
        error = L"Status dashboard JSON parse failed.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    data.overall_status = JsonWString(root, "overallStatus");
    if (data.overall_status.empty())
        data.overall_status = L"--";
    yyjson_val* counts = JsonObj(root, "summaryCounts");
    data.status_total = static_cast<int>(JsonInt(counts, "total"));
    data.status_operational = static_cast<int>(JsonInt(counts, "operational"));
    data.status_degraded = static_cast<int>(JsonInt(counts, "degraded"));
    data.status_error = static_cast<int>(JsonInt(counts, "error"));
    data.status_failed = static_cast<int>(JsonInt(counts, "failed"));
    data.has_status_dashboard = true;
    yyjson_doc_free(doc);
    return true;
}

bool CDataManager::ParseAdminDashboard(const std::string& json, RuntimeData& data, std::wstring& error) const
{
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (doc == nullptr)
    {
        error = L"Admin dashboard JSON parse failed.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* balance = JsonObj(root, "balance");
    yyjson_val* today = JsonObj(root, "today");
    data.admin_current_concurrency = static_cast<int>(JsonInt(balance, "currentConcurrency"));
    data.admin_user_count = JsonInt(balance, "userCount");
    data.admin_today_requests = JsonInt(today, "requestCount");
    data.admin_today_cost_usd = NormalizeUsdString(JsonCString(today, "costUsd"), JsonInt(today, "costMicros"));
    data.has_admin_dashboard = true;
    yyjson_doc_free(doc);
    return true;
}

bool CDataManager::ParsePurchaseStats(const std::string& json, RuntimeData& data, std::wstring& error) const
{
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (doc == nullptr)
    {
        error = L"Purchase stats JSON parse failed.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* today = JsonObj(root, "today");
    yyjson_val* month = JsonObj(root, "month");
    data.today_revenue_cny_cent = JsonInt(today, "revenueCnyCent");
    data.today_sales_count = JsonInt(today, "salesCount");
    data.month_revenue_cny_cent = JsonInt(month, "revenueCnyCent");
    data.month_sales_count = JsonInt(month, "salesCount");
    data.has_purchase_stats = true;
    yyjson_doc_free(doc);
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
    if (!CryptProtectData(&input, L"AMPManager", nullptr, nullptr, nullptr, 0, &output))
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
    input.pbData = reinterpret_cast<BYTE*>(bytes.data());
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
    std::wstring token = m_token;
    m_runtime_data = data;
    m_runtime_data.is_admin = data.is_admin;
    m_token = token;
}
