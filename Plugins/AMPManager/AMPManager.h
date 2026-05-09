#pragma once

#include "AMPManagerItem.h"
#include "PluginInterface.h"
#include <array>
#include <mutex>
#include <string>
#include <vector>

class CAMPManager : public ITMPlugin
{
private:
    CAMPManager();

public:
    static CAMPManager& Instance();

    virtual IPluginItem* GetItem(int index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual void* GetPluginIcon() override;
    virtual int GetCommandCount() override;
    virtual const wchar_t* GetCommandName(int command_index) override;
    virtual void OnPluginCommand(int command_index, void* hWnd, void* para) override;

private:
    static UINT ThreadCallback(LPVOID user_data);
    void StartRefreshThread(bool force);
    void RebuildItems();

private:
    static CAMPManager m_instance;
    std::array<CAMPManagerItem, static_cast<size_t>(AmpMetricId::Count)> m_items;
    std::vector<IPluginItem*> m_visible_items;
    std::mutex m_items_mutex;
    std::wstring m_tooltip_info;
    bool m_is_thread_running{};
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
