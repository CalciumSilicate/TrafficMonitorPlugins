#include "pch.h"
#include "AMPManagerItem.h"

CAMPManagerItem::CAMPManagerItem(AmpMetricId id)
    : m_metric_id(id)
{
}

void CAMPManagerItem::SetMetricId(AmpMetricId id)
{
    m_metric_id = id;
}

AmpMetricId CAMPManagerItem::GetMetricId() const
{
    return m_metric_id;
}

const wchar_t* CAMPManagerItem::GetItemName() const
{
    return g_data.GetMetricDefinition(m_metric_id).item_name;
}

const wchar_t* CAMPManagerItem::GetItemId() const
{
    return g_data.GetMetricDefinition(m_metric_id).item_id;
}

const wchar_t* CAMPManagerItem::GetItemLableText() const
{
    return g_data.GetMetricDefinition(m_metric_id).label;
}

const wchar_t* CAMPManagerItem::GetItemValueText() const
{
    static thread_local std::wstring value;
    value = g_data.GetMetricValue(m_metric_id);
    return value.c_str();
}

const wchar_t* CAMPManagerItem::GetItemValueSampleText() const
{
    return g_data.GetMetricDefinition(m_metric_id).sample;
}
