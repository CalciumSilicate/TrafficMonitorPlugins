#include "pch.h"
#include "LiveWebUIViewerItem.h"
#include "DataManager.h"

const wchar_t* CLiveWebUIViewerItem::GetItemName() const
{
    return L"LiveWebUI readers";
}

const wchar_t* CLiveWebUIViewerItem::GetItemId() const
{
    return L"LiveWebUIReaders";
}

const wchar_t* CLiveWebUIViewerItem::GetItemLableText() const
{
    return L"Readers";
}

const wchar_t* CLiveWebUIViewerItem::GetItemValueText() const
{
    static thread_local std::wstring value;
    value = g_data.GetReadersText();
    return value.c_str();
}

const wchar_t* CLiveWebUIViewerItem::GetItemValueSampleText() const
{
    return L"9999";
}
