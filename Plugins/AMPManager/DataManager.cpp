#include "pch.h"
#include "DataManager.h"
#include "../utilities/IniHelper.h"
#include "../utilities/Common.h"
#include "../utilities/JsonHelper.h"
#include "../utilities/bass64/base64.h"
#include "../utilities/yyjson/yyjson.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <thread>
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

    bool StartsWithIgnoreCase(const std::wstring& value, const wchar_t* prefix)
    {
        const size_t prefix_length = wcslen(prefix);
        return value.size() >= prefix_length && _wcsnicmp(value.c_str(), prefix, prefix_length) == 0;
    }

    std::wstring WinHttpError(const wchar_t* operation)
    {
        return std::wstring(operation) + L" failed (" + std::to_wstring(GetLastError()) + L").";
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

    std::wstring FormatFixed(double value, int decimals)
    {
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(decimals) << value;
        return ss.str();
    }

    int IntegerDigitCount(double value)
    {
        double abs_value = std::fabs(value);
        if (!std::isfinite(abs_value) || abs_value < 1.0)
            return 1;

        int digits = 1;
        while (abs_value >= 10.0)
        {
            abs_value /= 10.0;
            ++digits;
        }
        return digits;
    }

    int DecimalPlacesForCompactDigits(int digits)
    {
        return digits >= 3 ? 1 : 2;
    }

    std::wstring FormatCompactNumber(double value)
    {
        static const wchar_t* suffixes[] = { L"", L"K", L"M", L"B", L"T", L"P", L"E" };
        double abs_value = value < 0 ? -value : value;

        int digits = IntegerDigitCount(abs_value);
        if (digits <= 4)
            return FormatFixed(value, digits >= 3 ? 1 : 2);

        int suffix_index = (digits - 4) / 3 + 1;
        int max_suffix_index = static_cast<int>(_countof(suffixes)) - 1;
        if (suffix_index > max_suffix_index)
            suffix_index = max_suffix_index;

        double scale = 1.0;
        for (int i = 0; i < suffix_index; ++i)
            scale *= 1000.0;

        double scaled = value / scale;
        return FormatFixed(scaled, DecimalPlacesForCompactDigits(IntegerDigitCount(scaled))) + suffixes[suffix_index];
    }

    std::wstring FormatTTFB(double milliseconds)
    {
        if (milliseconds >= 10000.0)
            return FormatFixed(milliseconds / 1000.0, 1) + L"s";
        return FormatCompactNumber(milliseconds) + L"ms";
    }

    std::wstring MakeWarningMask(const std::wstring& value)
    {
        size_t count = value.empty() ? 2 : value.size();
        return std::wstring(count, L'!');
    }

    std::wstring FormatCompactInt(long long value)
    {
        return FormatCompactNumber(static_cast<double>(value));
    }

    std::wstring FormatLimit(int value)
    {
        return value > 0 ? FormatCompactInt(value) : L"inf";
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

    int StatusSeverity(const std::wstring& status)
    {
        std::wstring normalized = status;
        utilities::StringHelper::StringTransform(normalized, false);
        if (normalized == L"failed" || normalized == L"failure" || normalized == L"fatal" || normalized == L"失败" || normalized == L"致命")
            return 5;
        if (normalized == L"error" || normalized == L"err" || normalized == L"错误")
            return 4;
        if (normalized == L"degraded" || normalized == L"delayed" || normalized == L"delay" || normalized == L"warning"
            || normalized == L"延迟" || normalized == L"降级" || normalized == L"警告")
            return 3;
        if (normalized == L"operational" || normalized == L"ok" || normalized == L"success"
            || normalized == L"成功" || normalized == L"正常")
            return 2;
        return 1;
    }

    std::wstring WorseStatus(const std::wstring& left, const std::wstring& right)
    {
        return StatusSeverity(left) >= StatusSeverity(right) ? left : right;
    }

    std::vector<std::wstring> ParseBlockWords(const std::wstring& raw)
    {
        std::vector<std::wstring> words;
        utilities::StringHelper::StringSplit(raw, L',', words, true, true);
        for (auto& word : words)
            utilities::StringHelper::StringTransform(word, false);
        words.erase(std::remove_if(words.begin(), words.end(), [](const std::wstring& word) {
            return word.empty();
        }), words.end());
        return words;
    }

    bool ContainsIgnoreCase(std::wstring haystack, const std::wstring& needle)
    {
        if (needle.empty())
            return false;
        utilities::StringHelper::StringTransform(haystack, false);
        return haystack.find(needle) != std::wstring::npos;
    }

    bool IsBlockedByWords(const std::wstring& name, const std::wstring& channel_name, const std::wstring& model, const std::vector<std::wstring>& block_words)
    {
        if (block_words.empty())
            return false;

        for (const auto& word : block_words)
        {
            if (ContainsIgnoreCase(name, word)
                || ContainsIgnoreCase(channel_name, word)
                || ContainsIgnoreCase(model, word))
            {
                return true;
            }
        }
        return false;
    }

    void CountStatusBucket(const std::wstring& status, int& operational, int& degraded, int& error, int& failed, int& unknown)
    {
        switch (StatusSeverity(status))
        {
        case 5: ++failed; break;
        case 4: ++error; break;
        case 3: ++degraded; break;
        case 2: ++operational; break;
        default: ++unknown; break;
        }
    }

    bool IsErrorStatusCode(const std::wstring& status_code)
    {
        return status_code == L"ERR" || status_code == L"FATAL";
    }

    bool IsErrorBlinkFrame(const RuntimeData& data, AmpMetricId id)
    {
        if (id == AmpMetricId::Status || !data.has_status_dashboard || !IsErrorStatusCode(FormatStatusCode(data.overall_status)))
            return false;

        return GetTickCount64() % 800 < 250;
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

    bool IsOneBotAlertResponse(const std::string& json)
    {
        yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
        if (doc == nullptr)
            return false;
        const char* echo = JsonCString(yyjson_doc_get_root(doc), "echo");
        const bool matches = echo != nullptr && strcmp(echo, "amp-manager-alert") == 0;
        yyjson_doc_free(doc);
        return matches;
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
        { AmpMetricId::Status, L"AMP status", L"AMPStatus", L"AMP", L"FATAL", false },
        { AmpMetricId::TodayRequests, L"AMP today requests", L"AMPTodayReq", L"Req", L"999999", false },
        { AmpMetricId::TodayCost, L"AMP user today cost", L"AMPTodayCost", L"Cost", L"999.9K", false },
        { AmpMetricId::Balance, L"AMP user balance", L"AMPBalance", L"Bal", L"999.9K", false },
        { AmpMetricId::SubscriptionLeft, L"AMP user subscription left", L"AMPSubLeft", L"Sub", L"999.9K", false },
        { AmpMetricId::SubscriptionExpires, L"AMP subscription expires", L"AMPSubExp", L"Exp", L"2099-12-31", false },
        { AmpMetricId::UserConcurrency, L"AMP user concurrency", L"AMPUserConc", L"UConc", L"999.0/999.0", false },
        { AmpMetricId::AdminConcurrency, L"AMP admin concurrency", L"AMPAdmConc", L"AConc", L"9999.0", true },
        { AmpMetricId::AdminRequests, L"AMP admin today requests", L"AMPAdmReq", L"AReq", L"999.9K", true },
        { AmpMetricId::AdminBalance, L"AMP admin total balance", L"AMPAdmBal", L"ABal", L"999.9K", true },
        { AmpMetricId::AdminTodayCost, L"AMP admin today cost", L"AMPAdmCost", L"ACost", L"999.9K", true },
        { AmpMetricId::AdminSubscriptionUsers, L"AMP admin subscription users", L"AMPAdmSub", L"ASub", L"9999.0", true },
        { AmpMetricId::PurchaseToday, L"AMP purchase CNY today", L"AMPTodayRev", L"CNYD", L"999.9K", true },
        { AmpMetricId::PurchaseMonth, L"AMP purchase CNY month", L"AMPMonthRev", L"CNYM", L"999.9K", true },
        { AmpMetricId::PurchaseTotal, L"AMP purchase CNY total", L"AMPTotalRev", L"CNYT", L"999.9M", true },
        { AmpMetricId::LatestRPM, L"AMP throughput RPM", L"AMPLatestRPM", L"TP", L"999.9K", true },
        { AmpMetricId::LatestTTFB, L"AMP latest TTFB", L"AMPLatestTTFB", L"TTFB", L"9999.9ms", true },
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
    m_setting_data.block_words = ini.GetString(L"config", L"block_words", L"");
    m_setting_data.onebot_enabled = ini.GetBool(L"onebot", L"enabled", false);
    m_setting_data.onebot_ws_url = ini.GetString(L"onebot", L"ws_url", m_setting_data.onebot_ws_url.c_str());
    m_setting_data.onebot_token = UnprotectPassword(ini.GetString(L"onebot", L"token", L""));
    m_setting_data.onebot_private_target = ini.GetString(L"onebot", L"private_target", L"");
    if (m_setting_data.refresh_interval_sec < 10)
        m_setting_data.refresh_interval_sec = 10;

    for (const auto& definition : m_metric_definitions)
    {
        const size_t index = MetricIndex(definition.id);
        m_setting_data.enabled_metrics[index] = ini.GetBool(L"metrics", BuildMetricKey(definition).c_str(), true);
    }
    ResetOneBotNotification();
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
    ini.WriteString(L"config", L"block_words", m_setting_data.block_words);
    ini.WriteBool(L"onebot", L"enabled", m_setting_data.onebot_enabled);
    ini.WriteString(L"onebot", L"ws_url", m_setting_data.onebot_ws_url);
    ini.WriteString(L"onebot", L"token", ProtectPassword(m_setting_data.onebot_token));
    ini.WriteString(L"onebot", L"private_target", m_setting_data.onebot_private_target);
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
    NotifyOneBotIfNeeded(data);
    SetRuntimeData(data);
}

void CDataManager::ClearToken()
{
    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    m_token.clear();
}

void CDataManager::ResetOneBotNotification()
{
    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    m_last_notification_key.clear();
}

RuntimeData CDataManager::GetRuntimeData() const
{
    std::lock_guard<std::mutex> lock(m_runtime_mutex);
    return m_runtime_data;
}

std::wstring CDataManager::GetMetricValue(AmpMetricId id) const
{
    RuntimeData data = GetRuntimeData();
    std::wstring value;
    switch (id)
    {
    case AmpMetricId::Status:
        value = data.has_status_dashboard ? FormatStatusCode(data.overall_status) : L"--";
        break;
    case AmpMetricId::TodayRequests:
        value = FormatCompactInt(data.today_requests);
        break;
    case AmpMetricId::TodayCost:
        value = data.today_cost_usd;
        break;
    case AmpMetricId::Balance:
        value = data.balance_usd;
        break;
    case AmpMetricId::SubscriptionLeft:
        value = data.subscription_left_usd;
        break;
    case AmpMetricId::SubscriptionExpires:
        value = data.subscription_expires;
        break;
    case AmpMetricId::UserConcurrency:
        value = FormatCompactInt(data.user_current_concurrency) + L"/" + FormatLimit(data.user_concurrency_limit);
        break;
    case AmpMetricId::AdminConcurrency:
        value = data.has_admin_dashboard ? FormatCompactInt(data.admin_current_concurrency) : L"--";
        break;
    case AmpMetricId::AdminRequests:
        value = data.has_admin_dashboard ? FormatCompactInt(data.admin_today_requests) : L"--";
        break;
    case AmpMetricId::AdminBalance:
        value = data.has_admin_dashboard ? data.admin_balance_usd : L"--";
        break;
    case AmpMetricId::AdminTodayCost:
        value = data.has_admin_dashboard ? data.admin_today_cost_usd : L"--";
        break;
    case AmpMetricId::AdminSubscriptionUsers:
        value = data.has_purchase_stats ? FormatCompactInt(data.admin_subscription_user_count) : L"--";
        break;
    case AmpMetricId::PurchaseToday:
        value = data.has_purchase_stats ? FormatCompactCny(data.today_revenue_cny_cent) : L"--";
        break;
    case AmpMetricId::PurchaseMonth:
        value = data.has_purchase_stats ? FormatCompactCny(data.month_revenue_cny_cent) : L"--";
        break;
    case AmpMetricId::PurchaseTotal:
        value = data.has_purchase_stats ? FormatCompactCny(data.total_revenue_cny_cent) : L"--";
        break;
    case AmpMetricId::LatestRPM:
        value = data.has_latest_rpm ? FormatCompactNumber(data.latest_rpm5m) : L"--";
        break;
    case AmpMetricId::LatestTTFB:
        value = data.has_latest_ttfb ? FormatTTFB(data.latest_ttfb_ms) : L"--";
        break;
    default:
        value = L"--";
        break;
    }
    return IsErrorBlinkFrame(data, id) ? MakeWarningMask(value) : value;
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
    if (!data.onebot_last_error.empty())
        ss << L"\nOneBot alert error: " << data.onebot_last_error;
    ss << L"\nStatus: " << FormatStatusCode(data.overall_status) << L" (" << data.overall_status << L")  OK " << data.status_operational << L"/" << data.status_total;
    if (data.status_blocked > 0)
        ss << L", blocked " << data.status_blocked;
    ss << L"\nToday: " << data.today_requests << L" requests, " << data.today_cost_usd;
    ss << L"\nBalance: " << data.balance_usd << L", concurrency " << data.user_current_concurrency << L"/" << FormatLimit(data.user_concurrency_limit);
    ss << L"\nSubscription: " << data.subscription_name << L", daily left " << data.subscription_left_usd << L", expires " << data.subscription_expires;
    if (data.is_admin)
    {
        ss << L"\nAdmin: users " << data.admin_user_count << L", concurrency " << data.admin_current_concurrency
            << L", balance " << data.admin_balance_usd << L", today " << data.admin_today_requests
            << L" requests, cost " << data.admin_today_cost_usd << L", subscription users " << data.admin_subscription_user_count;
        ss << L"\nThroughput: RPM " << (data.has_latest_rpm ? FormatCompactNumber(data.latest_rpm5m) : L"--")
            << L", TTFB " << (data.has_latest_ttfb ? FormatTTFB(data.latest_ttfb_ms) : L"--");
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

    WinHttpHandle session{ WinHttpOpen(L"AMPManager TrafficMonitor Plugin/1.05", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
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
    const std::vector<std::wstring> block_words = ParseBlockWords(m_setting_data.block_words);

    // Prefer local recompute from groups/items so block words can veto noisy channels.
    // Fall back to server overallStatus/summaryCounts when groups are absent.
    yyjson_val* groups = JsonArr(root, "groups");
    bool recomputed = false;
    if (groups != nullptr)
    {
        int total = 0;
        int operational = 0;
        int degraded = 0;
        int error_count = 0;
        int failed = 0;
        int unknown = 0;
        int blocked = 0;
        std::wstring overall = L"unknown";
        bool has_items = false;

        yyjson_val* group = nullptr;
        yyjson_arr_iter group_iter;
        yyjson_arr_iter_init(groups, &group_iter);
        while ((group = yyjson_arr_iter_next(&group_iter)) != nullptr)
        {
            yyjson_val* items = JsonArr(group, "items");
            if (items == nullptr)
                continue;

            yyjson_val* item = nullptr;
            yyjson_arr_iter item_iter;
            yyjson_arr_iter_init(items, &item_iter);
            while ((item = yyjson_arr_iter_next(&item_iter)) != nullptr)
            {
                const std::wstring name = JsonWString(item, "name");
                const std::wstring channel_name = JsonWString(item, "channelName");
                const std::wstring model = JsonWString(item, "model");
                yyjson_val* latest = JsonObj(item, "latest");
                std::wstring status = latest != nullptr ? JsonWString(latest, "status") : L"unknown";
                if (status.empty())
                    status = L"unknown";

                if (IsBlockedByWords(name, channel_name, model, block_words))
                {
                    ++blocked;
                    continue;
                }

                has_items = true;
                ++total;
                CountStatusBucket(status, operational, degraded, error_count, failed, unknown);
                overall = WorseStatus(overall, status);
                if (StatusSeverity(status) >= 3)
                    data.status_issues.push_back({ status, channel_name, name, model });
            }
        }

        std::sort(data.status_issues.begin(), data.status_issues.end(), [](const StatusIssueData& left, const StatusIssueData& right) {
            const int left_severity = StatusSeverity(left.status);
            const int right_severity = StatusSeverity(right.status);
            if (left_severity != right_severity)
                return left_severity > right_severity;
            if (left.channel_name != right.channel_name)
                return left.channel_name < right.channel_name;
            if (left.name != right.name)
                return left.name < right.name;
            return left.model < right.model;
        });

        data.overall_status = has_items ? overall : L"--";
        data.status_total = total;
        data.status_operational = operational;
        data.status_degraded = degraded;
        data.status_error = error_count;
        data.status_failed = failed;
        data.status_blocked = blocked;
        recomputed = true;
    }

    if (!recomputed)
    {
        data.overall_status = JsonWString(root, "overallStatus");
        if (data.overall_status.empty())
            data.overall_status = L"--";
        yyjson_val* counts = JsonObj(root, "summaryCounts");
        data.status_total = static_cast<int>(JsonInt(counts, "total"));
        data.status_operational = static_cast<int>(JsonInt(counts, "operational"));
        data.status_degraded = static_cast<int>(JsonInt(counts, "degraded"));
        data.status_error = static_cast<int>(JsonInt(counts, "error"));
        data.status_failed = static_cast<int>(JsonInt(counts, "failed"));
        data.status_blocked = 0;
    }

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

void CDataManager::NotifyOneBotIfNeeded(RuntimeData& data)
{
    if (!m_setting_data.onebot_enabled)
    {
        ResetOneBotNotification();
        return;
    }
    if (!data.has_status_dashboard)
        return;

    const std::wstring status_code = FormatStatusCode(data.overall_status);
    if (status_code != L"DW" && status_code != L"ERR" && status_code != L"FATAL")
    {
        ResetOneBotNotification();
        return;
    }

    std::wstring notification_key = status_code;
    for (const auto& issue : data.status_issues)
    {
        notification_key += L"\n" + FormatStatusCode(issue.status) + L"\t"
            + issue.channel_name + L"\t" + issue.name + L"\t" + issue.model;
    }
    {
        std::lock_guard<std::mutex> lock(m_runtime_mutex);
        if (notification_key == m_last_notification_key)
            return;
    }

    std::wstringstream message;
    message << L"\u3010AMP Manager \u544a\u8b66\u3011\n"
        << L"AMP \u72b6\u6001\u5f02\u5e38\uff1a" << status_code << L"\n"
        << L"\u6b63\u5e38 " << data.status_operational << L"/" << data.status_total
        << L"\uff0c\u964d\u7ea7 " << data.status_degraded
        << L"\uff0c\u9519\u8bef " << data.status_error
        << L"\uff0c\u81f4\u547d " << data.status_failed << L"\n"
        << L"\u5f02\u5e38\u6e20\u9053\uff1a\n";
    if (data.status_issues.empty())
    {
        message << L"- \u63a5\u53e3\u672a\u8fd4\u56de\u6e20\u9053\u660e\u7ec6\n";
    }
    else
    {
        for (size_t i = 0; i < data.status_issues.size(); ++i)
        {
            const StatusIssueData& issue = data.status_issues[i];
            message << i + 1 << L". [" << FormatStatusCode(issue.status) << L"] ";
            if (!issue.channel_name.empty())
                message << L"\u6e20\u9053\uff1a" << issue.channel_name;
            else if (!issue.name.empty())
                message << L"\u6e20\u9053\uff1a" << issue.name;
            else
                message << L"\u672a\u547d\u540d\u6e20\u9053";
            if (!issue.name.empty() && issue.name != issue.channel_name)
                message << L"\uff1b\u9879\u76ee\uff1a" << issue.name;
            if (!issue.model.empty() && issue.model != issue.channel_name && issue.model != issue.name)
                message << L"\uff1b\u6a21\u578b\uff1a" << issue.model;
            message << L"\n";
        }
    }
    message << L"\u68c0\u6d4b\u65f6\u95f4\uff1a" << data.last_refresh_time << L"\n"
        << L"\u8bf7\u53ca\u65f6\u68c0\u67e5 AMP \u670d\u52a1\u72b6\u6001\u3002";

    std::wstring error;
    if (SendOneBotPrivateMessage(message.str(), error))
    {
        std::lock_guard<std::mutex> lock(m_runtime_mutex);
        m_last_notification_key = notification_key;
    }
    else
        data.onebot_last_error = error;
}

bool CDataManager::SendOneBotPrivateMessage(const std::wstring& message, std::wstring& error) const
{
    if (m_setting_data.onebot_ws_url.empty() || m_setting_data.onebot_private_target.empty())
    {
        error = L"OneBot WS URL or private target is empty.";
        return false;
    }

    std::wstring url = m_setting_data.onebot_ws_url;
    if (StartsWithIgnoreCase(url, L"wss://"))
        url.replace(0, 6, L"https://");
    else if (StartsWithIgnoreCase(url, L"ws://"))
        url.replace(0, 5, L"http://");
    else
    {
        error = L"OneBot URL must start with ws:// or wss://.";
        return false;
    }

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts))
    {
        error = L"Invalid OneBot WS URL.";
        return false;
    }

    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring object_name;
    if (parts.lpszUrlPath != nullptr && parts.dwUrlPathLength > 0)
        object_name.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (object_name.empty())
        object_name = L"/";
    if (parts.lpszExtraInfo != nullptr && parts.dwExtraInfoLength > 0)
        object_name.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    WinHttpHandle session{ WinHttpOpen(L"AMPManager TrafficMonitor Plugin/1.08", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
    if (!session)
    {
        error = WinHttpError(L"WinHttpOpen");
        return false;
    }

    WinHttpHandle connect{ WinHttpConnect(session, host.c_str(), parts.nPort, 0) };
    if (!connect)
    {
        error = WinHttpError(L"WinHttpConnect");
        return false;
    }

    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request{ WinHttpOpenRequest(connect, L"GET", object_name.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) };
    if (!request)
    {
        error = WinHttpError(L"WinHttpOpenRequest");
        return false;
    }
    WinHttpSetTimeouts(request, 5000, 5000, 10000, 10000);
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
    {
        error = WinHttpError(L"WebSocket upgrade setup");
        return false;
    }

    std::wstring headers;
    if (!m_setting_data.onebot_token.empty())
        headers = L"Authorization: Bearer " + m_setting_data.onebot_token + L"\r\n";
    if (!WinHttpSendRequest(
        request,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0) || !WinHttpReceiveResponse(request, nullptr))
    {
        error = WinHttpError(L"OneBot WebSocket handshake");
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX) || status != 101)
    {
        error = L"OneBot WebSocket handshake returned HTTP " + std::to_wstring(status) + L".";
        return false;
    }

    WinHttpHandle websocket{ WinHttpWebSocketCompleteUpgrade(request, 0) };
    if (!websocket)
    {
        error = WinHttpError(L"WebSocket upgrade");
        return false;
    }
    WinHttpCloseHandle(request.value);
    request.value = nullptr;
    DWORD close_timeout = 1000;
    WinHttpSetOption(websocket, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &close_timeout, sizeof(close_timeout));

    std::string body = "{\"action\":\"send_private_msg\",\"params\":{\"user_id\":\""
        + EscapeJsonString(m_setting_data.onebot_private_target)
        + "\",\"message\":\"" + EscapeJsonString(message)
        + "\"},\"echo\":\"amp-manager-alert\"}";
    DWORD send_result = WinHttpWebSocketSend(
        websocket,
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()));
    if (send_result != NO_ERROR)
    {
        error = L"OneBot WebSocket send failed (" + std::to_wstring(send_result) + L").";
        return false;
    }

    HANDLE receive_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (receive_done == nullptr)
    {
        error = WinHttpError(L"OneBot response event creation");
        return false;
    }

    std::string response;
    DWORD receive_result = ERROR_GEN_FAILURE;
    HINTERNET websocket_handle = websocket.value;
    std::thread receiver([&response, &receive_result, receive_done, websocket_handle]() {
        std::string current_message;
        while (true)
        {
            char buffer[4096];
            DWORD bytes_read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type{};
            receive_result = WinHttpWebSocketReceive(
                websocket_handle,
                buffer,
                static_cast<DWORD>(sizeof(buffer)),
                &bytes_read,
                &buffer_type);
            if (receive_result != NO_ERROR)
                break;
            if (buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
            {
                receive_result = ERROR_CONNECTION_ABORTED;
                break;
            }
            if (buffer_type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE
                && buffer_type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
            {
                receive_result = ERROR_INVALID_DATA;
                break;
            }

            current_message.append(buffer, bytes_read);
            if (buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
            {
                if (IsOneBotAlertResponse(current_message))
                {
                    response = current_message;
                    break;
                }
                current_message.clear();
            }
        }
        SetEvent(receive_done);
    });

    DWORD wait_result = WaitForSingleObject(receive_done, 10000);
    if (wait_result != WAIT_OBJECT_0)
        WinHttpWebSocketClose(websocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    receiver.join();
    CloseHandle(receive_done);
    if (wait_result == WAIT_TIMEOUT)
    {
        error = L"Timed out waiting for the OneBot response.";
        return false;
    }
    if (wait_result != WAIT_OBJECT_0)
    {
        error = L"Waiting for the OneBot response failed.";
        return false;
    }
    if (receive_result != NO_ERROR)
    {
        error = L"OneBot WebSocket receive failed (" + std::to_wstring(receive_result) + L").";
        return false;
    }

    WinHttpWebSocketClose(websocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);

    yyjson_doc* response_doc = yyjson_read(response.c_str(), response.size(), 0);
    if (response_doc == nullptr)
    {
        error = L"OneBot response is not valid JSON.";
        return false;
    }
    yyjson_val* response_root = yyjson_doc_get_root(response_doc);
    const char* response_status = JsonCString(response_root, "status");
    const int64_t retcode = JsonInt(response_root, "retcode");
    const bool accepted = response_status != nullptr && strcmp(response_status, "ok") == 0 && retcode == 0;
    if (!accepted)
    {
        std::wstring detail = JsonWString(response_root, "message");
        if (detail.empty())
            detail = JsonWString(response_root, "wording");
        error = L"OneBot send_private_msg failed (retcode " + std::to_wstring(retcode) + L")";
        if (!detail.empty())
            error += L": " + detail;
        error += L".";
    }
    yyjson_doc_free(response_doc);
    return accepted;
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
