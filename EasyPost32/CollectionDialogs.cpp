#include "framework.h"
#include "Resource.h"
#include "CollectionDialogs.h"
#include "RequestEditor.h"
#include "EasyPost32.h"

CCompatibilityDialog::CCompatibilityDialog(const CString& caption, const std::vector<std::string>& notes)
    : CDialog(IDD_COMPATIBILITY), m_caption(caption)
{
    m_text = L"Review the supported mapping before continuing.\r\n\r\n";
    for (const auto& note : notes) m_text += L"- " + FromUtf8(note) + L"\r\n\r\n";
    m_text += L"Ordinary variables, explicit headers, URLs and body text may contain plaintext credentials. "
        L"Review them before saving or sharing. Marked secret variable values are never written.\r\n\r\n"
        L"Continue applies this mapping; Cancel leaves your documents and destination file unchanged.";
}
BOOL CCompatibilityDialog::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetWindowText(m_caption);
    static_cast<CEdit*>(GetDlgItem(IDC_COMPATIBILITY_TEXT))->SetLimitText(0);
    SetDlgItemText(IDC_COMPATIBILITY_TEXT, m_text);
    return TRUE;
}

CNameDialog::CNameDialog(CString initial) : CDialog(IDD_ITEM_NAME), name(std::move(initial)) {}
BOOL CNameDialog::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetDlgItemText(IDC_ITEM_NAME, name);
    static_cast<CEdit*>(GetDlgItem(IDC_ITEM_NAME))->SetLimitText(256);
    static_cast<CEdit*>(GetDlgItem(IDC_ITEM_NAME))->SetSel(0, -1);
    GetDlgItem(IDC_ITEM_NAME)->SetFocus();
    return FALSE;
}
void CNameDialog::OnOK()
{
    GetDlgItemText(IDC_ITEM_NAME, name);
    name.Trim();
    if (name.IsEmpty()) { AfxMessageBox(L"Enter a nonempty name."); return; }
    CDialog::OnOK();
}

CVariablesDialog::CVariablesDialog(const std::vector<easypost::Variable>& values) : CDialog(IDD_VARIABLES), variables(values) {}
BEGIN_MESSAGE_MAP(CVariablesDialog, CDialog)
    ON_LBN_SELCHANGE(IDC_VARIABLE_LIST, &CVariablesDialog::OnSelect)
    ON_BN_CLICKED(IDC_VARIABLE_APPLY, &CVariablesDialog::OnApply)
    ON_BN_CLICKED(IDC_VARIABLE_DELETE, &CVariablesDialog::OnDelete)
    ON_BN_CLICKED(IDC_VARIABLE_SECRET, &CVariablesDialog::OnSecret)
    ON_BN_CLICKED(IDC_VARIABLE_NEW, &CVariablesDialog::OnNew)
END_MESSAGE_MAP()
BOOL CVariablesDialog::OnInitDialog()
{
    CDialog::OnInitDialog();
    static_cast<CEdit*>(GetDlgItem(IDC_VARIABLE_NAME))->SetLimitText(64);
    static_cast<CEdit*>(GetDlgItem(IDC_VARIABLE_VALUE))->SetLimitText(1024 * 1024);
    Refresh();
    return TRUE;
}
void CVariablesDialog::Refresh(int selection)
{
    auto* list = static_cast<CListBox*>(GetDlgItem(IDC_VARIABLE_LIST));
    list->ResetContent();
    for (const auto& var : variables)
        list->AddString(FromUtf8(var.name + (var.secret ? "  [secret; value not saved]" : "")));
    list->SetCurSel(selection);
    OnSelect();
}
void CVariablesDialog::OnSelect()
{
    const int index = static_cast<CListBox*>(GetDlgItem(IDC_VARIABLE_LIST))->GetCurSel();
    const auto var = index == LB_ERR ? easypost::Variable{} : variables.at(static_cast<size_t>(index));
    SetDlgItemText(IDC_VARIABLE_NAME, FromUtf8(var.name));
    CheckDlgButton(IDC_VARIABLE_SECRET, var.secret ? BST_CHECKED : BST_UNCHECKED);
    OnSecret();
    SetDlgItemText(IDC_VARIABLE_VALUE, FromUtf8(var.value));
    GetDlgItem(IDC_VARIABLE_DELETE)->EnableWindow(index != LB_ERR);
}
void CVariablesDialog::OnSecret()
{
    static_cast<CEdit*>(GetDlgItem(IDC_VARIABLE_VALUE))->SetPasswordChar(
        IsDlgButtonChecked(IDC_VARIABLE_SECRET) == BST_CHECKED ? L'\x25cf' : L'\0');
    GetDlgItem(IDC_VARIABLE_VALUE)->Invalidate();
}
void CVariablesDialog::OnNew() { Refresh(); GetDlgItem(IDC_VARIABLE_NAME)->SetFocus(); }
void CVariablesDialog::OnApply()
{
    m_applyFailed = true;
    CString name, value;
    GetDlgItemText(IDC_VARIABLE_NAME, name);
    GetDlgItemText(IDC_VARIABLE_VALUE, value);
    const int index = static_cast<CListBox*>(GetDlgItem(IDC_VARIABLE_LIST))->GetCurSel();
    auto updated = variables;
    easypost::Variable var{ ToUtf8(name), ToUtf8(value), IsDlgButtonChecked(IDC_VARIABLE_SECRET) == BST_CHECKED };
    if (index == LB_ERR) updated.push_back(std::move(var)); else updated.at(static_cast<size_t>(index)) = std::move(var);
    try { easypost::ValidateVariables(updated); }
    catch (const std::exception& error) { AfxMessageBox(FromUtf8(error.what())); return; }
    variables = std::move(updated);
    Refresh(index == LB_ERR ? static_cast<int>(variables.size()) - 1 : index);
    m_applyFailed = false;
}
void CVariablesDialog::OnDelete()
{
    const int index = static_cast<CListBox*>(GetDlgItem(IDC_VARIABLE_LIST))->GetCurSel();
    if (index != LB_ERR) { variables.erase(variables.begin() + index); Refresh(); }
}
void CVariablesDialog::OnOK()
{
    CString name;
    GetDlgItemText(IDC_VARIABLE_NAME, name);
    if (!name.IsEmpty()) { OnApply(); if (m_applyFailed) return; }
    CDialog::OnOK();
}

CHistoryDialog::CHistoryDialog() : CDialog(IDD_HISTORY) {}
BEGIN_MESSAGE_MAP(CHistoryDialog, CDialog)
    ON_BN_CLICKED(IDC_HISTORY_RECORD, &CHistoryDialog::OnRecord)
    ON_BN_CLICKED(IDC_HISTORY_CLEAR, &CHistoryDialog::OnClear)
END_MESSAGE_MAP()
BOOL CHistoryDialog::OnInitDialog()
{
    CDialog::OnInitDialog();
    static_cast<CEdit*>(GetDlgItem(IDC_HISTORY_TEXT))->SetLimitText(0);
    Refresh();
    return TRUE;
}
void CHistoryDialog::Refresh()
{
    CheckDlgButton(IDC_HISTORY_RECORD, theApp.history.enabled ? BST_CHECKED : BST_UNCHECKED);
    std::string text;
    const auto& entries = theApp.history.Entries();
    for (auto item = entries.rbegin(); item != entries.rend(); ++item)
        text += item->timestamp + "  " + item->method + "  " + item->url + "  HTTP " + std::to_string(item->status) +
            "  " + std::to_string(item->elapsedMs) + " ms" + (item->transportError ? "  [transport error]" : "") + "\r\n";
    SetDlgItemText(IDC_HISTORY_TEXT, FromUtf8(text));
}
void CHistoryDialog::OnRecord() { theApp.history.enabled = IsDlgButtonChecked(IDC_HISTORY_RECORD) == BST_CHECKED; }
void CHistoryDialog::OnClear()
{
    try { theApp.history.Clear(); Refresh(); }
    catch (const std::exception& error) { AfxMessageBox(FromUtf8(error.what())); }
}
