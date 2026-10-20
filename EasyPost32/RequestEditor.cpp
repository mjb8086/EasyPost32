#include "framework.h"
#include "Resource.h"
#include "CollectionWindow.h"
#include "RequestEditor.h"
#include "ResponseView.h"
#include <algorithm>

CString FromUtf8(const std::string& text)
{
    if (text.empty())
        return {};
    const int length = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    CString result;
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.GetBuffer(count), count);
    result.ReleaseBuffer(count);
    return result;
}

std::string ToUtf8(const CString& text)
{
    if (text.IsEmpty())
        return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.GetString(), text.GetLength(),
        nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.GetString(), text.GetLength(),
        result.data(), count, nullptr, nullptr);
    return result;
}

IMPLEMENT_DYNCREATE(CRequestEditor, CFormView)

CRequestEditor::CRequestEditor() : CFormView(IDD_REQUEST_EDITOR) {}

BEGIN_MESSAGE_MAP(CRequestEditor, CFormView)
    ON_WM_SIZE()
    ON_CBN_SELCHANGE(IDC_REQUEST_METHOD, &CRequestEditor::OnDraftChanged)
    ON_EN_CHANGE(IDC_REQUEST_URL, &CRequestEditor::OnDraftChanged)
    ON_EN_CHANGE(IDC_REQUEST_HEADERS, &CRequestEditor::OnDraftChanged)
    ON_EN_CHANGE(IDC_REQUEST_BODY, &CRequestEditor::OnDraftChanged)
    ON_BN_CLICKED(IDC_VERIFY_TLS, &CRequestEditor::OnDraftChanged)
    ON_BN_CLICKED(IDC_REQUEST_SEND, &CRequestEditor::OnSend)
    ON_BN_CLICKED(IDC_REQUEST_CANCEL, &CRequestEditor::OnCancel)
    ON_WM_TIMER()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

void CRequestEditor::DoDataExchange(CDataExchange* exchange)
{
    CFormView::DoDataExchange(exchange);
    DDX_Control(exchange, IDC_REQUEST_METHOD, m_method);
    DDX_Control(exchange, IDC_REQUEST_URL, m_url);
    DDX_Control(exchange, IDC_REQUEST_HEADERS, m_headers);
    DDX_Control(exchange, IDC_REQUEST_BODY, m_body);
}

void CRequestEditor::OnInitialUpdate()
{
    CFormView::OnInitialUpdate();
    for (const auto* method : { L"GET", L"POST", L"PUT", L"PATCH", L"DELETE", L"HEAD", L"OPTIONS" })
        m_method.AddString(method);
    m_url.SetLimitText(0);
    m_headers.SetLimitText(0);
    m_body.SetLimitText(0);
    m_sourceFont.CreatePointFont(100, L"Consolas");
    m_headers.SetFont(&m_sourceFont);
    m_body.SetFont(&m_sourceFont);
    OnUpdate(nullptr, 0, nullptr);
    LayoutEditor();
    // UI-thread completion dispatcher. Workers never post to a child HWND;
    // destroying the view removes this timer and cannot leave a late callback.
    if (!SetTimer(1, 100, nullptr))
        AfxThrowResourceException();
}

void CRequestEditor::OnUpdate(CView*, LPARAM hint, CObject*)
{
    if (!m_method.GetSafeHwnd())
        return;
    if (hint == 1) { UpdateActions(); return; }
    m_loading = true;
    const auto& request = static_cast<CCollectionDoc*>(GetDocument())->CurrentRequest();
    m_method.SelectString(-1, FromUtf8(request.method));
    m_url.SetWindowText(FromUtf8(request.url));
    m_headers.SetWindowText(FromUtf8(request.headersText));
    m_body.SetWindowText(FromUtf8(request.body));
    CheckDlgButton(IDC_VERIFY_TLS, request.verifyTls ? BST_CHECKED : BST_UNCHECKED);
    m_loading = false;
    UpdateActions();
}

void CRequestEditor::UpdateActions()
{
    auto* document = static_cast<CCollectionDoc*>(GetDocument());
    const bool hasRequest = document->HasRequest();
    for (int id : { IDC_REQUEST_METHOD, IDC_REQUEST_URL, IDC_REQUEST_HEADERS, IDC_REQUEST_BODY })
        GetDlgItem(id)->EnableWindow(hasRequest);
    const auto& run = document->CurrentRun();
    GetDlgItem(IDC_REQUEST_SEND)->EnableWindow(!run.Pending() && !document->CurrentRequest().url.empty());
    GetDlgItem(IDC_REQUEST_CANCEL)->EnableWindow(run.Pending() && !run.cancelling);
    GetDlgItem(IDC_VERIFY_TLS)->EnableWindow(hasRequest && !run.Pending());
    SetDlgItemText(IDC_EDITOR_NOTICE, !hasRequest ? L"Select a request in the collection tree." : document->CurrentRequest().verifyTls ?
        L"TLS verification enabled. Saved headers, bodies and ordinary variables are plaintext." :
        L"WARNING: TLS certificate and hostname verification DISABLED. Saved request text is plaintext.");
}

void CRequestEditor::OnSend() { static_cast<CCollectionDoc*>(GetDocument())->Send(); }
void CRequestEditor::OnCancel() { static_cast<CCollectionDoc*>(GetDocument())->Cancel(); }
void CRequestEditor::OnTimer(UINT_PTR timer)
{
    if (timer == 1) static_cast<CCollectionDoc*>(GetDocument())->PollCompletions();
    CFormView::OnTimer(timer);
}
void CRequestEditor::OnDestroy()
{
    KillTimer(1);
    CFormView::OnDestroy();
}

void CRequestEditor::OnDraftChanged()
{
    if (m_loading || !m_method.GetSafeHwnd() || m_method.GetCurSel() == CB_ERR)
        return;
    auto* document = static_cast<CCollectionDoc*>(GetDocument());
    if (!document->HasRequest()) return;
    auto& request = document->CurrentRequest();
    const auto previous = request;
    CString text;
    m_method.GetLBText(m_method.GetCurSel(), text);
    request.method = ToUtf8(text);
    m_url.GetWindowText(text);
    request.url = ToUtf8(text);
    m_headers.GetWindowText(text);
    request.headersText = ToUtf8(text);
    m_body.GetWindowText(text);
    request.body = ToUtf8(text);
    request.verifyTls = IsDlgButtonChecked(IDC_VERIFY_TLS) == BST_CHECKED;
    UpdateActions();
    if (request == previous) return;
    document->Changed(this); // Refresh method labels without reloading this editor.
}

void CRequestEditor::OnSize(UINT type, int width, int height)
{
    CFormView::OnSize(type, width, height);
    LayoutEditor();
}

void CRequestEditor::LayoutEditor()
{
    if (!m_method.GetSafeHwnd() || m_layoutActive)
        return;
    m_layoutActive = true;
    const int dpi = static_cast<int>(GetDpiForWindow(m_hWnd));
    auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    CRect client;
    GetClientRect(client);
    const int width = (std::max)(client.Width(), scale(460));
    const int height = (std::max)(client.Height(), scale(360));
    SetScrollSizes(MM_TEXT, CSize(width, height));
    const CPoint scroll = GetScrollPosition();
    auto place = [this, scroll](int id, int x, int y, int cx, int cy)
    {
        GetDlgItem(id)->MoveWindow(x - scroll.x, y - scroll.y, cx, cy);
    };
    const int margin = scale(10), gap = scale(8), label = scale(20), row = scale(28);
    const int methodWidth = scale(90), sendWidth = scale(76);
    const int urlX = margin + methodWidth + gap;
    const int cancelX = width - margin - sendWidth;
    const int sendX = cancelX - gap - sendWidth;
    place(IDC_METHOD_LABEL, margin, margin, methodWidth, label);
    place(IDC_URL_LABEL, urlX, margin, sendX - gap - urlX, label);
    // Combo's window height includes its drop-down list, unlike an edit control.
    place(IDC_REQUEST_METHOD, margin, margin + label, methodWidth, scale(220));
    place(IDC_REQUEST_URL, urlX, margin + label, sendX - gap - urlX, row);
    place(IDC_REQUEST_SEND, sendX, margin + label, sendWidth, row);
    place(IDC_REQUEST_CANCEL, cancelX, margin + label, sendWidth, row);
    place(IDC_VERIFY_TLS, margin, scale(62), width - 2 * margin, scale(22));
    place(IDC_EDITOR_NOTICE, margin, scale(88), width - 2 * margin, scale(36));
    const int headersY = scale(130);
    const int headersHeight = (std::max)(scale(64), (height - scale(212)) / 3);
    place(IDC_HEADERS_LABEL, margin, headersY - label, width - 2 * margin, label);
    place(IDC_REQUEST_HEADERS, margin, headersY, width - 2 * margin, headersHeight);
    const int bodyY = headersY + headersHeight + gap + label;
    place(IDC_BODY_LABEL, margin, bodyY - label, width - 2 * margin, label);
    place(IDC_REQUEST_BODY, margin, bodyY, width - 2 * margin, height - bodyY - margin);
    m_layoutActive = false;
}

void CRequestEditor::FocusEditor(bool last)
{
    GetParentFrame()->SetActiveView(this);
    if (last)
        m_body.SetFocus();
    else
        m_method.SetFocus();
}

BOOL CRequestEditor::PreTranslateMessage(MSG* message)
{
    if (message->message == WM_KEYDOWN && message->wParam == VK_TAB &&
        GetKeyState(VK_CONTROL) >= 0)
    {
        const bool backwards = GetKeyState(VK_SHIFT) < 0;
        if ((!backwards && message->hwnd == m_body.m_hWnd) ||
            (backwards && message->hwnd == m_method.m_hWnd))
        {
            auto* frame = static_cast<CCollectionFrame*>(GetParentFrame());
            if (backwards) { frame->SetActiveView(frame->Tree()); frame->Tree()->SetFocus(); }
            else static_cast<CResponseView*>(frame->Response())->FocusResponse(false);
            return TRUE;
        }
    }
    return CFormView::PreTranslateMessage(message);
}
