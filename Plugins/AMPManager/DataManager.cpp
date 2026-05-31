#include "pch.h"
#include "DataManager.h"
#include "../utilities/IniHelper.h"
#include "../utilities/Common.h"
#include "../utilities/JsonHelper.h"
#include "../utilities/bass64/base64.h"
#include "../utilities/yyjson/yyjson.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
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

    std::wstring FormatCompactNumber(double value, bool force_decimal = true)
    {
        const wchar_t* suffix = L"";
        double scaled = value;
        double abs_value = value < 0 ? -value : value;
        if (abs_value >= 1000000000.0)
        {
            scaled = value / 1000000000.0;
            suffix = L"B";
        }
        else if (abs_value >= 1000000.0)
        {
            scaled = value / 1000000.0;
            suffix = L"M";
        }
        else if (abs_value >= 1000.0)
        {
            scaled = value / 1000.0;
            suffix = L"K";
        }

        std::wstringstream ss;
        if (force_decimal || suffix[0] != L'\0')
            ss << std::fixed << std::setprecision(1) << scaled << suffix;
        else
            ss << static_cast<long long>(scaled) << suffix;
        return ss.str();
    }

    std::wstring FormatCompactInt(long long value)
    {
        return FormatCompactNumber(static_cast<double>(value), false);
    }

    std::wstring FormatLimit(int value)
    {
        return value > 0 ? FormatInt(value) : L"inf";
    }

    std::wstring FormatCompactCny(long long cent)
    {
        return FormatCompactNumber(static_cast<double>(cent) / 100.0);
    }

    std::wstring FormatMicrosAsUsd(int64_t micros)
    {
        return FormatCompactNumber(static_cast<double>(micros) / 1000000.0);
    }

    std::wstring NormalizeUsdString(const char* value, int64_t fallback_micros)
    {
        if (value != nullptr && value[0] != '\0')
        {
            return FormatCompactNumber(atof(value));
        }
        return FormatMicrosAsUsd(fallback_micros);
    }

    std::wstring FormatStatusCode(const std::wstring& status)
    {
        std::wstring normalized = status;
        utilities::StringHelper::StringTransform(normalized, false);
        if (normalized == L"operational" || normalized == L"ok" || normalized == L"success")
            return L"OK";
        if (normalized == L"成功" || normalized == L"正常")
            return L"OK";
        if (normalized == L"degraded" || normalized == L"delayed" || normalized == L"delay" || normalized == L"warning")
            return L"DW";
        if (normalized == L"延迟" || normalized == L"降级" || normalized == L"警告")
            return L"DW";
        if (normalized == L"failed" || normalized == L"failure" || normalized == L"fatal")
            return L"FATAL";
        if (normalized == L"失败" || normalized == L"致命")
            return L"FATAL";
        if (normalized == L"error" || normalized == L"err")
            return L"ERR";
        if (normalized == L"错误")
            return L"ERR";
        return L"--";
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

    const char* JsonCString(yyjson_val* obj, const char* key)
    {
        yyjson_val* value = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
        return value != nullptr && yyjson_is_str(value) ? yyjson_get_str(value) : nullptr;
    }

    yyjson_val* JsonArrayLastWithNumber(yyjson_val* arr, const char* key, const char* positive_key = nullptr)
    {
        if (arr == nullptr || !yyjson_is_arr(arr))
            return nullptr;
        size_t size = yyjson_arr_size(arr);
        while (size > 0)
        {
            yyjson_val* item = yyjson_arr_get(arr, --size);
            yyjson_val* value = item == nullptr ? nullptr : yyjson_obj_get(item, key);
            if (value == nullptr || yyjson_is_null(value))
                continue;
            if (positive_key != nullptr && JsonInt(item, positive_key) <= 0)
                continue;
            return item;
        }
        return nullptr;
    }

    yyjson_val* FindSubscriptionWindows(yyjson_val* subscription_windows, const char* subscription_id)
    {
        if (subscription_windows == nullptr || subscription_id == nullptr || !yyjson_is_arr(subscription_windows))
            return nullptr;
        yyjson_val* item = nullptr;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(subscription_windows, &iter);
        while ((item = yyjson_arr_iter_next(&iter)) != nullptr)
        {
            const char* item_subscription_id = JsonCString(item, "subscriptionId");
            if (item_subscription_id != nullptr && strcmp(item_subscription_id, subscription_id) == 0)
                return JsonArr(item, "windows");
        }
        return nullptr;
    }

    yyjson_val* PickWindowByLimitType(yyjson_val* windows, const char* limit_type)
    {
        if (windows == nullptr || !yyjson_is_arr(windows))
            return nullptr;
        yyjson_val* item = nullptr;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(windows, &iter);
        while ((item = yyjson_arr_iter_next(&iter)) != nullptr)
        {
            const char* item_limit_type = JsonCString(item, "limitType");
            if (item_limit_type != nullptr && strcmp(item_limit_type, limit_type) == 0)
                return item;
        }
        return nullptr;
    }

    int64_t PickLimitMicrosByType(yyjson_val* limits, const char* limit_type)
    {
        if (limits == nullptr || !yyjson_is_arr(limits))
            return -1;
        yyjson_val* item = nullptr;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(limits, &iter);
        while ((item = yyjson_arr_iter_next(&iter)) != nullptr)
        {
            const char* item_limit_type = JsonCString(item, "limitType");
            if (item_limit_type != nullptr && strcmp(item_limit_type, limit_type) == 0)
                return JsonInt(item, "limitMicros");
        }
        return -1;
    }

    std::wstring JsonWString(yyjson_val* obj, const char* key)
    {
        return utilities::JsonHelper::GetJsonWString(obj, key);
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
        { AmpMetricId::TodayCost, L"AMP user today cost", L"AMPTodayCost", L"Cost", L"999.9K", false },
        { AmpMetricId::Balance, L"AMP user balance", L"AMPBalance", L"Bal", L"999.9K", false },
        { AmpMetricId::SubscriptionLeft, L"AMP user subscription left", L"AMPSubLeft", L"Sub", L"999.9K", false },
        { AmpMetricId::SubscriptionExpires, L"AMP subscription expires", L"AMPSubExp", L"Exp", L"2099-12-31", false },
        { AmpMetricId::UserConcurrency, L"AMP user concurrency", L"AMPUserConc", L"UConc", L"999/999", false },
        { AmpMetricId::AdminConcurrency, L"AMP admin concurrency", L"AMPAdmConc", L"AConc", L"9999", true },
        { AmpMetricId::AdminRequests, L"AMP admin today requests", L"AMPAdmReq", L"AReq", L"999.9K", true },
        { AmpMetricId::AdminBalance, L"AMP admin total balance", L"AMPAdmBal", L"ABal", L"999.9K", true },
        { AmpMetricId::AdminTodayCost, L"AMP admin today cost", L"AMPAdmCost", L"ACost", L"999.9K", true },
        { AmpMetricId::AdminSubscriptionUsers, L"AMP admin subscription users", L"AMPAdmSub", L"ASub", L"9999", true },
        { AmpMetricId::PurchaseToday, L"AMP purchase CNY today", L"AMPTodayRev", L"CNYD", L"999.9K", true },
        { AmpMetricId::PurchaseMonth, L"AMP purchase CNY month", L"AMPMonthRev", L"CNYM", L"999.9K", true },
        { AmpMetricId::PurchaseTotal, L"AMP purchase CNY total", L"AMPTotalRev", L"CNYT", L"999.9M", true },
        { AmpMetricId::LatestRPM, L"AMP throughput RPM", L"AMPLatestRPM", L"TP", L"999.9K", true },
        { AmpMetricId::LatestTTFB, L"AMP latest TTFB", L"AMPLatestTTFB", L"TTFB", L"9999.9", true },
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
    int refresh_interval = m_setting_data.refresh_interval_sec < 10 ? 10 : m_setting_data.refresh_interval_sec;
    ini.WriteInt(L"config", L"refresh_interval_sec", refresh_interval);
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
        return data.has_status_dashboard ? FormatStatusCode(data.overall_status) : L"--";
    case AmpMetricId::TodayRequests:
        return FormatCompactInt(data.today_requests);
    case AmpMetricId::TodayCost:
        return data.today_cost_usd;
    case AmpMetricId::Balance:
        return data.balance_usd;
    case AmpMetricId::SubscriptionLeft:
        return data.subscription_left_usd;
    case AmpMetricId::SubscriptionExpires:
        return data.subscription_expires;
    case AmpMetricId::UserConcurrency:
        return FormatInt(data.user_current_concurrency) + L"/" + FormatLimit(data.user_concurrency_limit);
    case AmpMetricId::AdminConcurrency:
        return data.has_admin_dashboard ? FormatInt(data.admin_current_concurrency) : L"--";
    case AmpMetricId::AdminRequests:
        return data.has_admin_dashboard ? FormatCompactInt(data.admin_today_requests) : L"--";
    case AmpMetricId::AdminBalance:
        return data.has_admin_dashboard ? data.admin_balance_usd : L"--";
    case AmpMetricId::AdminTodayCost:
        return data.has_admin_dashboard ? data.admin_today_cost_usd : L"--";
    case AmpMetricId::AdminSubscriptionUsers:
        return data.has_purchase_stats ? FormatCompactInt(data.admin_subscription_user_count) : L"--";
    case AmpMetricId::PurchaseToday:
        return data.has_purchase_stats ? FormatCompactCny(data.today_revenue_cny_cent) : L"--";
    case AmpMetricId::PurchaseMonth:
        return data.has_purchase_stats ? FormatCompactCny(data.month_revenue_cny_cent) : L"--";
    case AmpMetricId::PurchaseTotal:
        return data.has_purchase_stats ? FormatCompactCny(data.total_revenue_cny_cent) : L"--";
    case AmpMetricId::LatestRPM:
        return data.has_latest_rpm ? FormatCompactNumber(data.latest_rpm5m) : L"--";
    case AmpMetricId::LatestTTFB:
        return data.has_latest_ttfb ? FormatCompactNumber(data.latest_ttfb_ms) : L"--";
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
    ss << L"\nStatus: " << FormatStatusCode(data.overall_status) << L" (" << data.overall_status << L")  OK " << data.status_operational << L"/" << data.status_total;
    ss << L"\nToday: " << data.today_requests << L" requests, " << data.today_cost_usd;
    ss << L"\nBalance: " << data.balance_usd << L", concurrency " << data.user_current_concurrency << L"/" << FormatLimit(data.user_concurrency_limit);
    ss << L"\nSubscription: " << data.subscription_name << L", daily left " << data.subscription_left_usd << L", expires " << data.subscription_expires;
    if (data.is_admin)
    {
        ss << L"\nAdmin: users " << data.admin_user_count << L", concurrency " << data.admin_current_concurrency
            << L", balance " << data.admin_balance_usd << L", today " << data.admin_today_requests
            << L" requests, cost " << data.admin_today_cost_usd << L", subscription users " << data.admin_subscription_user_count;
        ss << L"\nThroughput: RPM " << (data.has_latest_rpm ? FormatCompactNumber(data.latest_rpm5m) : L"--")
            << L", TTFB " << (data.has_latest_ttfb ? FormatCompactNumber(data.latest_ttfb_ms) : L"--") << L" ms";
        ss << L"\nPurchase CNY: today " << FormatCompactCny(data.today_revenue_cny_cent) << L" (" << data.today_sales_count
            << L" orders), month " << FormatCompactCny(data.month_revenue_cny_cent) << L" (" << data.month_sales_count
            << L" orders), total " << FormatCompactCny(data.total_revenue_cny_cent);
    }
    return ss.str();
}

bool CDataManager::IsMetricVisible(AmpMetricId id) const
{
    const size_t index = MetricIndex(id);
    return index < m_setting_data.enabled_metrics.size() && m_setting_data.enabled_metrics[index];
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

    WinHttpHandle session{ WinHttpOpen(L"AMPManager TrafficMonitor Plugin/1.03", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
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
        if (GetJson(L"/api/admin/dashboard?throughputWindow=1h", json, status_code, admin_error))
            ParseAdminDashboard(json, data, admin_error);
        if (GetJson(L"/api/admin/dashboard/trends?throughputWindow=1h", json, status_code, admin_error))
            ParseAdminTrends(json, data, admin_error);
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
    yyjson_val* subscriptions = JsonArr(root, "subscriptions");
    data.subscription_count = subscriptions == nullptr ? (subscription == nullptr ? 0 : 1) : static_cast<int>(yyjson_arr_size(subscriptions));
    if (data.subscription_count > 1)
    {
        data.subscription_name = FormatInt(data.subscription_count) + L" subs";
    }
    else if (subscription != nullptr)
    {
        data.subscription_name = JsonWString(subscription, "planName");
        if (data.subscription_name.empty())
            data.subscription_name = L"--";
    }

    std::wstring latest_expire;
    bool has_unlimited_expire = false;
    if (subscriptions != nullptr)
    {
        yyjson_val* item = nullptr;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(subscriptions, &iter);
        while ((item = yyjson_arr_iter_next(&iter)) != nullptr)
        {
            std::wstring expire = ShortDateTime(yyjson_obj_get(item, "expiresAt"));
            if (expire == L"--")
            {
                has_unlimited_expire = true;
                continue;
            }
            if (!has_unlimited_expire && expire > latest_expire)
            {
                latest_expire = expire;
            }
        }
    }
    else if (subscription != nullptr)
    {
        latest_expire = ShortDateTime(yyjson_obj_get(subscription, "expiresAt"));
        if (latest_expire == L"--")
            has_unlimited_expire = true;
    }
    if (has_unlimited_expire)
        data.subscription_expires = L"unlimited";
    else if (!latest_expire.empty())
        data.subscription_expires = latest_expire;
    else if (subscription == nullptr)
        data.subscription_expires = L"--";

    yyjson_val* subscription_windows = JsonArr(root, "subscriptionWindows");
    long long total_daily_left = 0;
    bool has_total_daily_left = false;
    if (subscriptions != nullptr && yyjson_arr_size(subscriptions) > 0)
    {
        const char* current_subscription_id = JsonCString(subscription, "id");
        yyjson_val* item = nullptr;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(subscriptions, &iter);
        while ((item = yyjson_arr_iter_next(&iter)) != nullptr)
        {
            const char* subscription_id = JsonCString(item, "id");
            yyjson_val* windows = FindSubscriptionWindows(subscription_windows, subscription_id);
            if (windows == nullptr && current_subscription_id != nullptr && subscription_id != nullptr && strcmp(subscription_id, current_subscription_id) == 0)
                windows = JsonArr(root, "windows");
            yyjson_val* daily = PickWindowByLimitType(windows, "daily");
            if (daily != nullptr)
            {
                total_daily_left += JsonInt(daily, "leftMicros");
                has_total_daily_left = true;
                continue;
            }
            int64_t daily_limit = PickLimitMicrosByType(JsonArr(item, "limits"), "daily");
            if (daily_limit >= 0)
            {
                total_daily_left += daily_limit;
                has_total_daily_left = true;
            }
        }
    }
    else if (subscription != nullptr)
    {
        yyjson_val* daily = PickWindowByLimitType(JsonArr(root, "windows"), "daily");
        if (daily != nullptr)
        {
            total_daily_left = JsonInt(daily, "leftMicros");
            has_total_daily_left = true;
        }
        else
        {
            int64_t daily_limit = PickLimitMicrosByType(JsonArr(subscription, "limits"), "daily");
            if (daily_limit >= 0)
            {
                total_daily_left = daily_limit;
                has_total_daily_left = true;
            }
        }
    }

    if (has_total_daily_left)
        data.subscription_left_usd = FormatMicrosAsUsd(total_daily_left);
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
    yyjson_val* latest_ttfb = JsonArrayLastWithNumber(JsonArr(root, "ttfbTrend"), "avgMs", "sampleCnt");
    data.admin_current_concurrency = static_cast<int>(JsonInt(balance, "currentConcurrency"));
    data.admin_balance_usd = NormalizeUsdString(JsonCString(balance, "totalBalanceUsd"), JsonInt(balance, "totalBalanceMicros"));
    data.admin_user_count = JsonInt(balance, "userCount");
    data.admin_today_requests = JsonInt(today, "requestCount");
    data.admin_today_cost_usd = NormalizeUsdString(JsonCString(today, "costUsd"), JsonInt(today, "costMicros"));
    yyjson_val* current_rpm = balance == nullptr ? nullptr : yyjson_obj_get(balance, "currentRpm");
    if (current_rpm != nullptr && !yyjson_is_null(current_rpm))
    {
        data.latest_rpm5m = JsonReal(balance, "currentRpm");
        data.has_latest_rpm = true;
    }
    if (latest_ttfb != nullptr)
    {
        data.latest_ttfb_ms = JsonReal(latest_ttfb, "avgMs");
        data.has_latest_ttfb = true;
    }
    data.has_admin_dashboard = true;
    yyjson_doc_free(doc);
    return true;
}

bool CDataManager::ParseAdminTrends(const std::string& json, RuntimeData& data, std::wstring& error) const
{
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (doc == nullptr)
    {
        error = L"Admin trends JSON parse failed.";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* latest_ttfb = JsonArrayLastWithNumber(JsonArr(root, "ttfbTrend"), "avgMs", "sampleCnt");
    if (latest_ttfb != nullptr)
    {
        data.latest_ttfb_ms = JsonReal(latest_ttfb, "avgMs");
        data.has_latest_ttfb = true;
    }
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
    yyjson_val* summary = JsonObj(root, "summary");
    yyjson_val* today = JsonObj(root, "today");
    yyjson_val* month = JsonObj(root, "month");
    data.total_revenue_cny_cent = JsonInt(summary, "totalRevenueCnyCent");
    data.admin_subscription_user_count = JsonInt(summary, "userCount");
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
    std::wstring token = m_token;
    m_runtime_data = data;
    m_runtime_data.is_admin = data.is_admin;
    m_token = token;
}
