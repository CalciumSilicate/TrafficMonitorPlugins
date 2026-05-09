#include "pch.h"
#include "AMPManager.h"
#include "OptionsDlg.h"
#include "afxdialogex.h"

namespace
{
    void SetDlgText(CDialog* dlg, int id, const std::wstring& value)
    {
        dlg->SetDlgItemText(id, value.c_str());
    }

    std::wstring GetDlgText(CDialog* dlg, int id)
    {
        CString value;
        dlg->GetDlgItemText(id, value);
        return value.GetString();
    }

    void SetCheck(CDialog* dlg, int id, bool checked)
    {
        dlg->CheckDlgButton(id, checked ? BST_CHECKED : BST_UNCHECKED);
    }

    bool GetCheck(CDialog* dlg, int id)
    {
        return dlg->IsDlgButtonChecked(id) == BST_CHECKED;
    }
}

IMPLEMENT_DYNAMIC(COptionsDlg, CDialog)

COptionsDlg::COptionsDlg(CWnd* pParent)
    : CDialog(IDD_OPTIONS_DIALOG, pParent)
{
}

COptionsDlg::~COptionsDlg()
{
}

void COptionsDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(COptionsDlg, CDialog)
END_MESSAGE_MAP()

BOOL COptionsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    SetDlgText(this, IDC_BASE_URL_EDIT, m_data.base_url);
    SetDlgText(this, IDC_USERNAME_EDIT, m_data.username);
    SetDlgText(this, IDC_PASSWORD_EDIT, m_data.password);
    SetDlgItemInt(IDC_REFRESH_INTERVAL_EDIT, m_data.refresh_interval_sec);

    SetCheck(this, IDC_METRIC_STATUS_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::Status)]);
    SetCheck(this, IDC_METRIC_TODAY_REQUESTS_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayRequests)]);
    SetCheck(this, IDC_METRIC_TODAY_COST_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayCost)]);
    SetCheck(this, IDC_METRIC_BALANCE_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::Balance)]);
    SetCheck(this, IDC_METRIC_SUB_LEFT_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::SubscriptionLeft)]);
    SetCheck(this, IDC_METRIC_SUB_EXPIRES_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::SubscriptionExpires)]);
    SetCheck(this, IDC_METRIC_ADMIN_CONCURRENCY_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminConcurrency)]);
    SetCheck(this, IDC_METRIC_TODAY_REVENUE_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayRevenue)]);
    SetCheck(this, IDC_METRIC_MONTH_REVENUE_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::MonthRevenue)]);

    return TRUE;
}

void COptionsDlg::OnOK()
{
    m_data.base_url = GetDlgText(this, IDC_BASE_URL_EDIT);
    m_data.username = GetDlgText(this, IDC_USERNAME_EDIT);
    m_data.password = GetDlgText(this, IDC_PASSWORD_EDIT);

    BOOL translated = FALSE;
    UINT interval = GetDlgItemInt(IDC_REFRESH_INTERVAL_EDIT, &translated, FALSE);
    m_data.refresh_interval_sec = translated ? static_cast<int>(interval) : 30;
    if (m_data.refresh_interval_sec < 10)
        m_data.refresh_interval_sec = 10;

    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::Status)] = GetCheck(this, IDC_METRIC_STATUS_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayRequests)] = GetCheck(this, IDC_METRIC_TODAY_REQUESTS_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayCost)] = GetCheck(this, IDC_METRIC_TODAY_COST_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::Balance)] = GetCheck(this, IDC_METRIC_BALANCE_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::SubscriptionLeft)] = GetCheck(this, IDC_METRIC_SUB_LEFT_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::SubscriptionExpires)] = GetCheck(this, IDC_METRIC_SUB_EXPIRES_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminConcurrency)] = GetCheck(this, IDC_METRIC_ADMIN_CONCURRENCY_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayRevenue)] = GetCheck(this, IDC_METRIC_TODAY_REVENUE_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::MonthRevenue)] = GetCheck(this, IDC_METRIC_MONTH_REVENUE_CHECK);

    CDialog::OnOK();
}
