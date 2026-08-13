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
    SetDlgText(this, IDC_BLOCK_WORDS_EDIT, m_data.block_words);
    SetCheck(this, IDC_ONEBOT_ENABLED_CHECK, m_data.onebot_enabled);
    SetDlgText(this, IDC_ONEBOT_WS_URL_EDIT, m_data.onebot_ws_url);
    SetDlgText(this, IDC_ONEBOT_TOKEN_EDIT, m_data.onebot_token);
    SetDlgText(this, IDC_ONEBOT_PRIVATE_TARGET_EDIT, m_data.onebot_private_target);

    SetCheck(this, IDC_METRIC_STATUS_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::Status)]);
    SetCheck(this, IDC_METRIC_TODAY_REQUESTS_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayRequests)]);
    SetCheck(this, IDC_METRIC_TODAY_COST_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayCost)]);
    SetCheck(this, IDC_METRIC_BALANCE_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::Balance)]);
    SetCheck(this, IDC_METRIC_SUB_LEFT_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::SubscriptionLeft)]);
    SetCheck(this, IDC_METRIC_SUB_EXPIRES_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::SubscriptionExpires)]);
    SetCheck(this, IDC_METRIC_USER_CONCURRENCY_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::UserConcurrency)]);
    SetCheck(this, IDC_METRIC_ADMIN_CONCURRENCY_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminConcurrency)]);
    SetCheck(this, IDC_METRIC_ADMIN_REQUESTS_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminRequests)]);
    SetCheck(this, IDC_METRIC_ADMIN_BALANCE_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminBalance)]);
    SetCheck(this, IDC_METRIC_ADMIN_COST_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminTodayCost)]);
    SetCheck(this, IDC_METRIC_ADMIN_SUB_USERS_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminSubscriptionUsers)]);
    SetCheck(this, IDC_METRIC_PURCHASE_TODAY_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::PurchaseToday)]);
    SetCheck(this, IDC_METRIC_PURCHASE_MONTH_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::PurchaseMonth)]);
    SetCheck(this, IDC_METRIC_PURCHASE_TOTAL_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::PurchaseTotal)]);
    SetCheck(this, IDC_METRIC_LATEST_RPM_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::LatestRPM)]);
    SetCheck(this, IDC_METRIC_LATEST_TTFB_CHECK, m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::LatestTTFB)]);

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
    m_data.block_words = GetDlgText(this, IDC_BLOCK_WORDS_EDIT);
    if (m_data.refresh_interval_sec < 10)
        m_data.refresh_interval_sec = 10;

    m_data.onebot_enabled = GetCheck(this, IDC_ONEBOT_ENABLED_CHECK);
    m_data.onebot_ws_url = GetDlgText(this, IDC_ONEBOT_WS_URL_EDIT);
    m_data.onebot_token = GetDlgText(this, IDC_ONEBOT_TOKEN_EDIT);
    m_data.onebot_private_target = GetDlgText(this, IDC_ONEBOT_PRIVATE_TARGET_EDIT);
    if (m_data.onebot_enabled && (m_data.onebot_ws_url.empty() || m_data.onebot_private_target.empty()))
    {
        MessageBox(L"OneBot WS URL and private target are required when OneBot is enabled.", L"AMP Manager", MB_OK | MB_ICONWARNING);
        return;
    }
    if (m_data.onebot_enabled
        && _wcsnicmp(m_data.onebot_ws_url.c_str(), L"ws://", 5) != 0
        && _wcsnicmp(m_data.onebot_ws_url.c_str(), L"wss://", 6) != 0)
    {
        MessageBox(L"OneBot WS URL must start with ws:// or wss://.", L"AMP Manager", MB_OK | MB_ICONWARNING);
        return;
    }

    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::Status)] = GetCheck(this, IDC_METRIC_STATUS_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayRequests)] = GetCheck(this, IDC_METRIC_TODAY_REQUESTS_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::TodayCost)] = GetCheck(this, IDC_METRIC_TODAY_COST_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::Balance)] = GetCheck(this, IDC_METRIC_BALANCE_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::SubscriptionLeft)] = GetCheck(this, IDC_METRIC_SUB_LEFT_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::SubscriptionExpires)] = GetCheck(this, IDC_METRIC_SUB_EXPIRES_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::UserConcurrency)] = GetCheck(this, IDC_METRIC_USER_CONCURRENCY_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminConcurrency)] = GetCheck(this, IDC_METRIC_ADMIN_CONCURRENCY_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminRequests)] = GetCheck(this, IDC_METRIC_ADMIN_REQUESTS_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminBalance)] = GetCheck(this, IDC_METRIC_ADMIN_BALANCE_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminTodayCost)] = GetCheck(this, IDC_METRIC_ADMIN_COST_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::AdminSubscriptionUsers)] = GetCheck(this, IDC_METRIC_ADMIN_SUB_USERS_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::PurchaseToday)] = GetCheck(this, IDC_METRIC_PURCHASE_TODAY_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::PurchaseMonth)] = GetCheck(this, IDC_METRIC_PURCHASE_MONTH_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::PurchaseTotal)] = GetCheck(this, IDC_METRIC_PURCHASE_TOTAL_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::LatestRPM)] = GetCheck(this, IDC_METRIC_LATEST_RPM_CHECK);
    m_data.enabled_metrics[static_cast<size_t>(AmpMetricId::LatestTTFB)] = GetCheck(this, IDC_METRIC_LATEST_TTFB_CHECK);

    CDialog::OnOK();
}
