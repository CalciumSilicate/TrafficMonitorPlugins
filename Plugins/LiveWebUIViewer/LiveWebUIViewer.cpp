#include "pch.h"
#include "LiveWebUIViewer.h"
#include "DataManager.h"
#include "OptionsDlg.h"

CLiveWebUIViewer CLiveWebUIViewer::m_instance;

CLiveWebUIViewer::CLiveWebUIViewer()
{
}

CLiveWebUIViewer& CLiveWebUIViewer::Instance()
{
    return m_instance;
}

IPluginItem* CLiveWebUIViewer::GetItem(int index)
{
    if (index == 0)
        return &m_item;
    return nullptr;
}

const wchar_t* CLiveWebUIViewer::GetTooltipInfo()
{
    m_tooltip_info = g_data.BuildTooltip();
    return m_tooltip_info.c_str();
}

void CLiveWebUIViewer::DataRequired()
{
    StartRefreshThread(false);
}

ITMPlugin::OptionReturn CLiveWebUIViewer::ShowOptionsDialog(void* hParent)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CWnd* pParent = CWnd::FromHandle((HWND)hParent);
    COptionsDlg dlg(pParent);
    dlg.m_data = g_data.m_setting_data;
    if (dlg.DoModal() == IDOK)
    {
        g_data.m_setting_data = dlg.m_data;
        g_data.SaveConfig();
        g_data.ClearSession();
        StartRefreshThread(true);
        return ITMPlugin::OR_OPTION_CHANGED;
    }
    return ITMPlugin::OR_OPTION_UNCHANGED;
}

const wchar_t* CLiveWebUIViewer::GetInfo(PluginInfoIndex index)
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
        return L"https://github.com/CalciumSilicate/liveWebUI";
    case TMI_VERSION:
        return L"1.00";
    default:
        break;
    }
    return L"";
}

void CLiveWebUIViewer::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    switch (index)
    {
    case ITMPlugin::EI_CONFIG_DIR:
        g_data.LoadConfig(std::wstring(data));
        StartRefreshThread(true);
        break;
    default:
        break;
    }
}

void* CLiveWebUIViewer::GetPluginIcon()
{
    return nullptr;
}

int CLiveWebUIViewer::GetCommandCount()
{
    return 1;
}

const wchar_t* CLiveWebUIViewer::GetCommandName(int command_index)
{
    if (command_index == 0)
        return g_data.StringRes(IDS_REFRESH_NOW).GetString();
    return L"";
}

void CLiveWebUIViewer::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    if (command_index == 0)
        StartRefreshThread(true);
}

UINT CLiveWebUIViewer::ThreadCallback(LPVOID user_data)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CLiveWebUIViewer* self = reinterpret_cast<CLiveWebUIViewer*>(user_data);
    if (self == nullptr)
        return 0;

    self->m_is_thread_running = true;
    g_data.Refresh();
    self->m_is_thread_running = false;
    return 0;
}

void CLiveWebUIViewer::StartRefreshThread(bool force)
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

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CLiveWebUIViewer::Instance();
}
