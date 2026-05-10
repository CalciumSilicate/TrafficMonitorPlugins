#pragma once

#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include "resource.h"

#define g_data CDataManager::Instance()

struct SettingData
{
    std::wstring base_url{ L"http://assx.top:42110" };
    std::wstring channel_slug{ L"cal" };
    std::wstring password;
    int refresh_interval_sec{ 30 };
};

struct RuntimeData
{
    bool login_ok{};
    bool channel_found{};
    std::wstring channel_slug;
    std::wstring channel_label;
    std::wstring state{ L"--" };
    bool online{};
    long long readers{};
    long long hls_readers{};
    long long webrtc_readers{};
    std::wstring last_error;
    std::wstring last_refresh_time;
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
    void ClearSession();
    RuntimeData GetRuntimeData() const;
    std::wstring GetReadersText() const;
    std::wstring BuildTooltip() const;

    SettingData m_setting_data;

private:
    bool Login(std::wstring& error);
    bool GetJson(const std::wstring& path, std::string& result, int& status_code, std::wstring& error);
    bool PostJson(const std::wstring& path, const std::string& body, std::string& result, int& status_code, std::wstring& error);
    bool RequestJson(const wchar_t* method, const std::wstring& path, const std::string& body, bool with_session, std::string& result, int& status_code, std::wstring& error);
    bool ParseChannels(const std::string& json, RuntimeData& data, std::wstring& error) const;
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
    std::wstring m_session_cookie;
};
