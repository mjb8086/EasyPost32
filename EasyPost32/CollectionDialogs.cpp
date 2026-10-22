#include "framework.h"
#include "Resource.h"
#include "CollectionDialogs.h"
#include "RequestEditor.h"
#include "EasyPost32.h"
#include "RequestComposer.h"
#include <algorithm>

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
    ON_BN_CLICKED(IDC_HISTORY_RESEND, &CHistoryDialog::OnResend)
    ON_BN_CLICKED(IDC_HISTORY_CANCEL, &CHistoryDialog::OnCancelSend)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_HISTORY_LIST, &CHistoryDialog::OnSelect)
    ON_NOTIFY(TCN_SELCHANGE, IDC_HISTORY_TABS, &CHistoryDialog::OnTab)
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
END_MESSAGE_MAP()
BOOL CHistoryDialog::OnInitDialog()
{
    CDialog::OnInitDialog();
    m_list.SubclassDlgItem(IDC_HISTORY_LIST, this);
    m_tabs.SubclassDlgItem(IDC_HISTORY_TABS, this);
    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
    const int dpi = static_cast<int>(GetDpiForWindow(m_hWnd));
    const wchar_t* columns[]{ L"Time (UTC)", L"Method", L"Endpoint", L"Result", L"Time (ms)", L"Details" };
    const int widths[]{ 174, 70, 270, 100, 90, 120 };
    for (int index = 0; index < 6; ++index) m_list.InsertColumn(index, columns[index], LVCFMT_LEFT, MulDiv(widths[index], dpi, 96));
    for (const auto* caption : { L"Request headers", L"Request body", L"Response headers", L"Response body" })
        m_tabs.InsertItem(m_tabs.GetItemCount(), caption);
    m_tabs.SetCurSel(0);
    m_font.CreatePointFont(100, L"Consolas");
    GetDlgItem(IDC_HISTORY_DETAILS)->SetFont(&m_font);
    for (int id : { IDC_HISTORY_DETAILS, IDC_HISTORY_URL, IDC_HISTORY_SUMMARY })
        static_cast<CEdit*>(GetDlgItem(id))->SetLimitText(0);
    SetDlgItemText(IDC_HISTORY_PROGRESS, L"");
    Refresh();
    Layout();
    if (!SetTimer(1, 100, nullptr)) AfxThrowResourceException();
    return TRUE;
}
void CHistoryDialog::Refresh()
{
    m_rebuilding = true;
    m_list.SetRedraw(FALSE);
    m_list.DeleteAllItems();
    CheckDlgButton(IDC_HISTORY_RECORD, theApp.history.enabled ? BST_CHECKED : BST_UNCHECKED);
    const auto& entries = theApp.history.Entries();
    int selected = -1;
    for (auto item = entries.rbegin(); item != entries.rend(); ++item)
    {
        const int row = m_list.InsertItem(m_list.GetItemCount(), FromUtf8(item->timestamp));
        m_list.SetItemData(row, static_cast<DWORD_PTR>(item->id)); // x64; IDs are never persisted.
        m_list.SetItemText(row, 1, FromUtf8(item->method));
        m_list.SetItemText(row, 2, FromUtf8(item->url));
        m_list.SetItemText(row, 3, FromUtf8(item->transportError ? "Transport error" : "HTTP " + std::to_string(item->status)));
        m_list.SetItemText(row, 4, FromUtf8(std::to_string(item->elapsedMs)));
        m_list.SetItemText(row, 5, item->details ? L"This session" : L"Metadata only");
        if (item->id == m_selectedId) selected = row;
    }
    if (selected < 0 && !entries.empty() && !m_unsavedResult) selected = 0;
    if (selected >= 0)
    {
        m_selectedId = static_cast<std::uint64_t>(m_list.GetItemData(selected));
        m_list.SetItemState(selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        m_list.EnsureVisible(selected, FALSE);
    }
    else m_selectedId = 0;
    m_list.SetRedraw(TRUE);
    m_list.Invalidate();
    m_revision = theApp.history.Revision();
    m_rebuilding = false;
    ShowDetails();
}
void CHistoryDialog::OnRecord() { theApp.history.enabled = IsDlgButtonChecked(IDC_HISTORY_RECORD) == BST_CHECKED; }
void CHistoryDialog::OnClear()
{
    try
    {
        theApp.history.Clear();
        CancelPending();
        m_unsavedResult.reset();
        SetDlgItemText(IDC_HISTORY_PROGRESS, L"");
        Refresh();
    }
    catch (const std::exception& error) { AfxMessageBox(FromUtf8(error.what())); }
}
void CHistoryDialog::OnSelect(NMHDR* header, LRESULT* result)
{
    *result = 0;
    const auto* change = reinterpret_cast<NMLISTVIEW*>(header);
    if (m_rebuilding || !(change->uChanged & LVIF_STATE)) return;
    const int row = m_list.GetNextItem(-1, LVNI_SELECTED);
    m_selectedId = row >= 0 ? static_cast<std::uint64_t>(m_list.GetItemData(row)) : 0;
    if (row >= 0) m_unsavedResult.reset();
    ShowDetails();
}
void CHistoryDialog::OnTab(NMHDR*, LRESULT* result) { *result = 0; ShowDetails(); }
namespace
{
// Read-only display conversion never changes stored or resent bytes.
CString HistoryDisplay(const std::string& source)
{
    std::string display;
    display.reserve(source.size());
    for (size_t index = 0; index < source.size(); ++index)
    {
        if (source[index] == '\n' && (index == 0 || source[index - 1] != '\r')) display += '\r';
        display += source[index];
    }
    return FromUtf8(display);
}
}
void CHistoryDialog::ShowDetails()
{
    m_selected = m_selectedId ? nullptr : m_unsavedResult;
    for (const auto& entry : theApp.history.Entries())
        if (entry.id == m_selectedId) { m_selected = entry.details; break; }
    GetDlgItem(IDC_HISTORY_RESEND)->EnableWindow(m_selected != nullptr && !m_job);
    GetDlgItem(IDC_HISTORY_CANCEL)->EnableWindow(m_job != nullptr);
    SetDlgItemText(IDC_HISTORY_URL, m_selected ? FromUtf8(m_selected->request.url) : CString{});
    if (!m_selected)
    {
        SetDlgItemText(IDC_HISTORY_SUMMARY, m_selectedId ?
            L"Metadata only. Full details were not retained, were released at the memory limit, or belong to a previous session." :
            L"No history. Enable recording, then send a request from a collection.");
        SetDlgItemText(IDC_HISTORY_DETAILS, L"");
        return;
    }
    const auto& details = *m_selected;
    const auto& response = details.response;
    std::string summary = details.request.method + " | " + (response.status ?
        "HTTP " + std::to_string(response.status) + " " + response.statusText : "No HTTP response");
    if (response.error) summary += " | Transport error: " + response.error->message;
    summary += "\r\n" + std::to_string(response.elapsed.count()) + " ms | received " + std::to_string(response.downloadedBytes) +
        " bytes | sent " + std::to_string(response.uploadedBytes) + " bytes";
    if (!details.request.verifyTls) summary += " | WARNING: TLS validation DISABLED for this request and its resend.";
    SetDlgItemText(IDC_HISTORY_SUMMARY, FromUtf8(summary));
    std::string source;
    switch (m_tabs.GetCurSel())
    {
    case 0: source = details.request.headersText; break;
    case 1: source = details.request.body; break;
    case 2: source = response.rawHeaders; break;
    default:
        source = response.bodyIsText ? response.body : "[Binary or non-UTF-8 response: " +
            std::to_string(response.body.size()) + " captured bytes. Text display suppressed.]";
    }
    SetDlgItemText(IDC_HISTORY_DETAILS, HistoryDisplay(source));
}
void CHistoryDialog::OnResend()
{
    if (!m_selected || m_job) return;
    easypost::HttpRequest request;
    std::string error;
    if (!ComposeRequest(m_selected->request, request, error)) { AfxMessageBox(FromUtf8(error)); return; }
    try
    {
        easypost::HttpOptions options;
        options.verifyTls = m_selected->request.verifyTls;
        m_metadata = { easypost::UtcTimestamp(), request.method, "[endpoint omitted]" };
        for (const auto& entry : theApp.history.Entries())
            if (entry.id == m_selectedId) { m_metadata.url = entry.url; break; }
        m_resending = m_selected;
        m_job = theApp.httpJobs.Start(std::move(request), options);
        SetDlgItemText(IDC_HISTORY_PROGRESS, L"Resending... a new row will appear when complete.");
        ShowDetails();
    }
    catch (const std::exception&)
    {
        m_resending.reset();
        AfxMessageBox(L"Unable to start the history HTTP worker.");
    }
}
void CHistoryDialog::OnCancelSend()
{
    if (m_job)
    {
        m_job->Cancel();
        GetDlgItem(IDC_HISTORY_CANCEL)->EnableWindow(FALSE);
        SetDlgItemText(IDC_HISTORY_PROGRESS, L"Cancelling resend...");
    }
}
void CHistoryDialog::OnTimer(UINT_PTR timer)
{
    if (timer == 1)
    {
        if (m_job && m_job->Done())
        {
            try
            {
                const auto& response = m_job->Response();
                m_metadata.status = response.status;
                m_metadata.elapsedMs = response.elapsed.count();
                m_metadata.transportError = response.error.has_value();
                m_metadata.details = std::make_shared<const easypost::HistoryDetails>(easypost::HistoryDetails{ m_resending->request, response });
                theApp.history.Record(m_metadata, true); // This Resend is an explicit one-result recording choice.
                m_unsavedResult.reset();
                m_selectedId = theApp.history.Entries().back().id;
                SetDlgItemText(IDC_HISTORY_PROGRESS, L"Resend complete; new result is at the top.");
            }
            catch (const std::exception&)
            {
                theApp.history.enabled = false;
                m_unsavedResult = m_metadata.details;
                m_selectedId = 0;
                SetDlgItemText(IDC_HISTORY_PROGRESS, L"History save failed; showing the unsaved resend result.");
            }
            m_metadata = {};
            m_job.reset();
            m_resending.reset();
            Refresh();
        }
        if (m_revision != theApp.history.Revision()) Refresh();
        CheckDlgButton(IDC_HISTORY_RECORD, theApp.history.enabled ? BST_CHECKED : BST_UNCHECKED);
        theApp.httpJobs.Reap();
    }
    CDialog::OnTimer(timer);
}
void CHistoryDialog::CancelPending()
{
    if (m_job) m_job->Cancel();
    m_job.reset();
    m_resending.reset();
    m_metadata = {};
}
void CHistoryDialog::OnDestroy()
{
    KillTimer(1);
    CancelPending();
    m_selected.reset();
    m_unsavedResult.reset();
    CDialog::OnDestroy();
}
void CHistoryDialog::OnSize(UINT type, int width, int height) { CDialog::OnSize(type, width, height); Layout(); }
void CHistoryDialog::OnGetMinMaxInfo(MINMAXINFO* info)
{
    CDialog::OnGetMinMaxInfo(info);
    const int dpi = GetSafeHwnd() ? static_cast<int>(GetDpiForWindow(m_hWnd)) : 96;
    info->ptMinTrackSize = { MulDiv(850, dpi, 96), MulDiv(600, dpi, 96) };
}
void CHistoryDialog::Layout()
{
    if (!m_list.GetSafeHwnd()) return;
    CRect client; GetClientRect(client);
    const int dpi = static_cast<int>(GetDpiForWindow(m_hWnd));
    auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const int margin = scale(12), width = client.Width() - 2 * margin;
    const int tableHeight = (std::max)(scale(130), (client.Height() - scale(270)) / 2);
    const int top = scale(78) + tableHeight + scale(8);
    auto place = [this](int id, int x, int y, int w, int h) { GetDlgItem(id)->MoveWindow(x, y, w, h); };
    place(IDC_HISTORY_RECORD, margin, scale(8), width, scale(24));
    place(IDC_HISTORY_NOTICE, margin, scale(34), width, scale(38));
    place(IDC_HISTORY_LIST, margin, scale(78), width, tableHeight);
    place(IDC_HISTORY_URL_LABEL, margin, top + scale(4), scale(66), scale(22));
    place(IDC_HISTORY_URL, margin + scale(70), top, width - scale(70), scale(26));
    place(IDC_HISTORY_SUMMARY, margin, top + scale(32), width, scale(48));
    place(IDC_HISTORY_TABS, margin, top + scale(86), width, scale(30));
    place(IDC_HISTORY_DETAILS, margin, top + scale(122), width, client.Height() - top - scale(174));
    const int bottom = client.Height() - scale(38);
    place(IDC_HISTORY_CLEAR, margin, bottom, scale(110), scale(26));
    place(IDC_HISTORY_RESEND, margin + scale(120), bottom, scale(140), scale(26));
    place(IDC_HISTORY_CANCEL, margin + scale(270), bottom, scale(110), scale(26));
    place(IDC_HISTORY_PROGRESS, margin + scale(390), bottom + scale(4), width - scale(480), scale(24));
    place(IDOK, client.Width() - margin - scale(78), bottom, scale(78), scale(26));
}
