#include "pch.h"
#include "LiveWebUIViewer.h"
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
    SetDlgText(this, IDC_CHANNEL_SLUG_EDIT, m_data.channel_slug);
    SetDlgText(this, IDC_PASSWORD_EDIT, m_data.password);
    SetDlgItemInt(IDC_REFRESH_INTERVAL_EDIT, m_data.refresh_interval_sec);

    return TRUE;
}

void COptionsDlg::OnOK()
{
    m_data.base_url = GetDlgText(this, IDC_BASE_URL_EDIT);
    m_data.channel_slug = GetDlgText(this, IDC_CHANNEL_SLUG_EDIT);
    m_data.password = GetDlgText(this, IDC_PASSWORD_EDIT);

    BOOL translated = FALSE;
    UINT interval = GetDlgItemInt(IDC_REFRESH_INTERVAL_EDIT, &translated, FALSE);
    m_data.refresh_interval_sec = translated ? static_cast<int>(interval) : 30;
    if (m_data.refresh_interval_sec < 10)
        m_data.refresh_interval_sec = 10;

    CDialog::OnOK();
}
