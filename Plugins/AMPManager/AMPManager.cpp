#include "pch.h"
#include "AMPManager.h"
#include "DataManager.h"
#include "OptionsDlg.h"

CAMPManager CAMPManager::m_instance;

CAMPManager::CAMPManager()
{
    for (size_t i = 0; i < m_items.size(); ++i)
        m_items[i].SetMetricId(static_cast<AmpMetricId>(i));
    RebuildItems();
}

CAMPManager& CAMPManager::Instance()
{
    return m_instance;
}

IPluginItem* CAMPManager::GetItem(int index)
{
    RebuildItems();
    std::lock_guard<std::mutex> lock(m_items_mutex);
    if (index >= 0 && index < static_cast<int>(m_visible_items.size()))
        return m_visible_items[index];
    return nullptr;
}

const wchar_t* CAMPManager::GetTooltipInfo()
{
    m_tooltip_info = g_data.BuildTooltip();
    return m_tooltip_info.c_str();
}

void CAMPManager::DataRequired()
{
    StartRefreshThread(false);
}

ITMPlugin::OptionReturn CAMPManager::ShowOptionsDialog(void* hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd* pParent = CWnd::FromHandle((HWND)hParent);
    COptionsDlg dlg(pParent);
    dlg.m_data = g_data.m_setting_data;
    if (dlg.DoModal() == IDOK)
    {
        g_data.m_setting_data = dlg.m_data;
        g_data.SaveConfig();
        g_data.ClearToken();
        g_data.ResetOneBotNotification();
        g_data.RestartOneBotCommandListener();
        RebuildItems();
        StartRefreshThread(true);
        return ITMPlugin::OR_OPTION_CHANGED;
    }
    return ITMPlugin::OR_OPTION_UNCHANGED;
}

const wchar_t* CAMPManager::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:
        return g_data.StringRes(IDS_PLUGIN_NAME).GetString();
    case TMI_DESCRIPTION:
        return g_data.StringRes(IDS_PLUGIN_DESCRIPTION).GetString();
    case TMI_AUTHOR:
        return L"CalciumSilicate";
    case TMI_COPYRIGHT:
        return L"Copyright (C) by CalciumSilicate 2026";
    case ITMPlugin::TMI_URL:
        return L"https://github.com/CalciumSilicate/TrafficMonitorPlugins";
    case TMI_VERSION:
        return L"1.09";
    default:
        break;
    }
    return L"";
}

void CAMPManager::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    switch (index)
    {
    case ITMPlugin::EI_CONFIG_DIR:
        g_data.LoadConfig(std::wstring(data));
        RebuildItems();
        StartRefreshThread(true);
        break;
    default:
        break;
    }
}

void* CAMPManager::GetPluginIcon()
{
    return nullptr;
}

int CAMPManager::GetCommandCount()
{
    return 1;
}

const wchar_t* CAMPManager::GetCommandName(int command_index)
{
    if (command_index == 0)
        return g_data.StringRes(IDS_REFRESH_NOW).GetString();
    return L"";
}

void CAMPManager::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    if (command_index == 0)
        StartRefreshThread(true);
}

UINT CAMPManager::ThreadCallback(LPVOID user_data)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CAMPManager* self = reinterpret_cast<CAMPManager*>(user_data);
    if (self == nullptr)
        return 0;

    self->m_is_thread_running = true;
    g_data.Refresh();
    self->RebuildItems();
    self->m_is_thread_running = false;
    return 0;
}

void CAMPManager::StartRefreshThread(bool force)
{
    static time_t last_req_time{};
    time_t now = time(nullptr);
    int interval = g_data.m_setting_data.refresh_interval_sec;
    if (interval < 10)
        interval = 10;

    if (!force && last_req_time != 0 && now - last_req_time < interval)
        return;
    if (m_is_thread_running)
        return;
    last_req_time = now;
    AfxBeginThread(ThreadCallback, this);
}

void CAMPManager::RebuildItems()
{
    std::lock_guard<std::mutex> lock(m_items_mutex);
    m_visible_items.clear();
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        AmpMetricId id = static_cast<AmpMetricId>(i);
        if (g_data.IsMetricVisible(id))
            m_visible_items.push_back(&m_items[i]);
    }
    if (m_visible_items.empty())
        m_visible_items.push_back(&m_items[0]);
}

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CAMPManager::Instance();
}
