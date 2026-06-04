#pragma once

#include "DataManager.h"
#include "PluginInterface.h"

class CAMPManagerItem : public IPluginItem
{
public:
    CAMPManagerItem() = default;
    explicit CAMPManagerItem(AmpMetricId id);

    void SetMetricId(AmpMetricId id);
    AmpMetricId GetMetricId() const;

    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override;
    virtual int GetItemWidthEx(void* hDC) const override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

private:
    AmpMetricId m_metric_id{ AmpMetricId::Status };
};
