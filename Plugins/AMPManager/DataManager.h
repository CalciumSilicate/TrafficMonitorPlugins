#pragma once

#include <array>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "resource.h"

#define g_data CDataManager::Instance()

enum class AmpMetricId
{
    Status = 0,
    TodayRequests,
    TodayCost,
    Balance,
    SubscriptionLeft,
    SubscriptionExpires,
    AdminConcurrency,
    TodayRevenue,
    MonthRevenue,
    Count
};

struct MetricDefinition
{
    AmpMetricId id;
    const wchar_t* item_name;
    const wchar_t* item_id;
    const wchar_t* label;
    const wchar_t* sample;
    bool admin_only;
};

struct SettingData
{
    std::wstring base_url{ L"https://api.asxs.top" };
    std::wstring username;
    std::wstring password;
    int refresh_interval_sec{ 30 };
    std::array<bool, static_cast<size_t>(AmpMetricId::Count)> enabled_metrics{};

    SettingData();
};

struct RuntimeData
{
    bool login_ok{};
    bool is_admin{};
    bool has_user_dashboard{};
    bool has_billing_state{};
    bool has_status_dashboard{};
    bool has_admin_dashboard{};
    bool has_purchase_stats{};
    std::wstring username;
    std::wstring last_error;
    std::wstring last_refresh_time;

    std::wstring overall_status{ L"--" };
    int status_total{};
    int status_operational{};
    int status_degraded{};
    int status_error{};
    int status_failed{};

    long long today_requests{};
    std::wstring today_cost_usd{ L"--" };
    std::wstring balance_usd{ L"--" };
    int user_current_concurrency{};
    int user_concurrency_limit{};

    std::wstring subscription_name{ L"--" };
    std::wstring subscription_expires{ L"--" };
    std::wstring subscription_left_usd{ L"--" };

    long long admin_today_requests{};
    std::wstring admin_today_cost_usd{ L"--" };
    int admin_current_concurrency{};
    long long admin_user_count{};

    long long today_revenue_cny_cent{};
    long long month_revenue_cny_cent{};
    long long today_sales_count{};
    long long month_sales_count{};
};

class CDataManager
{
private:
    CDataManager();
    ~CDataManager();

public:
    static CDataManager& Instance();

    void LoadConfig(const std::wstring& config_dir);
    void SaveConfig() const;
    const CString& StringRes(UINT id);
    void DPIFromWindow(CWnd* pWnd);
    int DPI(int pixel);
    float DPIF(float pixel);
    int RDPI(int pixel);
    HICON GetIcon(UINT id);

    void Refresh();
    void ClearToken();
    RuntimeData GetRuntimeData() const;
    std::wstring GetMetricValue(AmpMetricId id) const;
    std::wstring BuildTooltip() const;
    bool IsMetricVisible(AmpMetricId id) const;
    bool IsAdminMetric(AmpMetricId id) const;
    const MetricDefinition& GetMetricDefinition(AmpMetricId id) const;
    const std::vector<MetricDefinition>& GetMetricDefinitions() const;

    SettingData m_setting_data;

private:
    bool Login(std::wstring& error);
    bool GetJson(const std::wstring& path, std::string& result, int& status_code, std::wstring& error);
    bool PostJson(const std::wstring& path, const std::string& body, std::string& result, int& status_code, std::wstring& error);
    bool RequestJson(const wchar_t* method, const std::wstring& path, const std::string& body, bool with_auth, std::string& result, int& status_code, std::wstring& error);
    bool RefreshWithCurrentToken(RuntimeData& data, std::wstring& error);
    bool ParseUserDashboard(const std::string& json, RuntimeData& data, std::wstring& error) const;
    bool ParseBillingState(const std::string& json, RuntimeData& data, std::wstring& error) const;
    bool ParseStatusDashboard(const std::string& json, RuntimeData& data, std::wstring& error) const;
    bool ParseAdminDashboard(const std::string& json, RuntimeData& data, std::wstring& error) const;
    bool ParsePurchaseStats(const std::string& json, RuntimeData& data, std::wstring& error) const;
    std::wstring ProtectPassword(const std::wstring& plain) const;
    std::wstring UnprotectPassword(const std::wstring& encoded) const;
    void SetRuntimeData(const RuntimeData& data);

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    std::map<UINT, CString> m_string_table;
    std::map<UINT, HICON> m_icons;
    int m_dpi{ 96 };

    mutable std::mutex m_runtime_mutex;
    RuntimeData m_runtime_data;
    std::wstring m_token;
    time_t m_last_refresh_time{};

    std::vector<MetricDefinition> m_metric_definitions;
};
