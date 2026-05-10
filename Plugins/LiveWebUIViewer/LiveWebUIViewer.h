#pragma once

#include "LiveWebUIViewerItem.h"
#include "PluginInterface.h"
#include <ctime>
#include <mutex>
#include <string>

class CLiveWebUIViewer : public ITMPlugin
{
private:
    CLiveWebUIViewer();

public:
    static CLiveWebUIViewer& Instance();

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

private:
    static CLiveWebUIViewer m_instance;
    CLiveWebUIViewerItem m_item;
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
