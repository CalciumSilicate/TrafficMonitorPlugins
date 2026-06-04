#include "pch.h"
#include "AMPManagerItem.h"
#include <algorithm>

namespace
{
    constexpr DWORD ErrorBlinkIntervalMs = 200;

    bool IsErrorStatus(const std::wstring& value)
    {
        return value == L"ERR" || value == L"FATAL";
    }
}

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

bool CAMPManagerItem::IsCustomDraw() const
{
    return m_metric_id == AmpMetricId::Status;
}

int CAMPManagerItem::GetItemWidthEx(void* hDC) const
{
    if (!IsCustomDraw())
        return 0;

    CDC* pDC = CDC::FromHandle((HDC)hDC);
    CString label(GetItemLableText());
    CString value(GetItemValueText());
    CString sample(GetItemValueSampleText());

    int value_width = (std::max)(pDC->GetTextExtent(value).cx, pDC->GetTextExtent(sample).cx);
    return pDC->GetTextExtent(label).cx + g_data.DPI(6) + value_width;
}

void CAMPManagerItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    if (!IsCustomDraw())
        return;

    CDC* pDC = CDC::FromHandle((HDC)hDC);
    CRect rect(CPoint(x, y), CSize(w, h));
    CRect rc_label{ rect };
    CRect rc_value{ rect };

    CString label(GetItemLableText());
    std::wstring value(GetItemValueText());
    rc_label.right = rc_label.left + pDC->GetTextExtent(label).cx;
    rc_value.left = rc_label.right + g_data.DPI(6);

    COLORREF default_color = dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    pDC->SetTextColor(default_color);
    pDC->DrawText(label, rc_label, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (IsErrorStatus(value))
    {
        bool blink_on = (GetTickCount() / ErrorBlinkIntervalMs) % 2 == 0;
        COLORREF error_color = dark_mode ? RGB(255, 121, 120) : RGB(195, 0, 0);
        pDC->SetTextColor(blink_on ? error_color : default_color);
    }
    else
    {
        pDC->SetTextColor(default_color);
    }

    pDC->DrawText(value.c_str(), rc_value, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
