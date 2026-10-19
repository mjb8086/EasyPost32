#include "framework.h"
#include "Resource.h"
#include "CollectionWindow.h"
#include "RequestEditor.h"
#include "ResponseView.h"
#include <algorithm>

namespace
{
const char* ErrorName(easypost::TransportErrorKind kind)
{
    using enum easypost::TransportErrorKind;
    switch (kind)
    {
    case InvalidRequest: return "invalid request";
    case Initialization: return "initialization";
    case Connection: return "connection";
    case Timeout: return "timeout";
    case Tls: return "TLS";
    case RedirectLimit: return "redirect limit";
    case RedirectBlocked: return "redirect blocked";
    case ResponseTooLarge: return "response too large";
    case HeadersTooLarge: return "headers too large";
    case Cancelled: return "cancelled";
    default: return "transfer";
    }
}
}

IMPLEMENT_DYNCREATE(CResponseView, CFormView)
CResponseView::CResponseView() : CFormView(IDD_RESPONSE_VIEW) {}
BEGIN_MESSAGE_MAP(CResponseView, CFormView)
    ON_WM_SIZE()
END_MESSAGE_MAP()

void CResponseView::DoDataExchange(CDataExchange* exchange)
{
    CFormView::DoDataExchange(exchange);
    DDX_Control(exchange, IDC_RESPONSE_HEADERS, m_headers);
    DDX_Control(exchange, IDC_RESPONSE_BODY, m_body);
}

void CResponseView::OnInitialUpdate()
{
    CFormView::OnInitialUpdate();
    m_sourceFont.CreatePointFont(100, L"Consolas");
    m_headers.SetFont(&m_sourceFont);
    m_body.SetFont(&m_sourceFont);
    m_headers.SetLimitText(0);
    m_body.SetLimitText(0);
    OnUpdate(nullptr, 0, nullptr);
    LayoutResponse();
}

void CResponseView::OnUpdate(CView*, LPARAM, CObject*)
{
    if (!m_body.GetSafeHwnd()) return;
    const auto& run = static_cast<CCollectionDoc*>(GetDocument())->CurrentRun();
    if (m_initialized && run.job == m_displayedJob && run.preparationError == m_displayedError &&
        run.Pending() == m_pending && run.cancelling == m_cancelling) return;
    m_initialized = true;
    m_displayedJob = run.job;
    m_displayedError = run.preparationError;
    m_pending = run.Pending();
    m_cancelling = run.cancelling;
    std::string summary = "Ready. Send a request to view its response.";
    std::string headers, body;
    if (!run.preparationError.empty()) summary = "Request not sent: " + run.preparationError;
    else if (run.Pending()) summary = run.cancelling ? "Cancelling request..." : "Sending request...";
    else if (run.job)
    {
        const auto& response = run.job->Response();
        const std::string httpStatus = response.status ?
            "HTTP " + std::to_string(response.status) + " " + response.statusText : "No HTTP response";
        summary = response.error ?
            "Transport error (" + std::string(ErrorName(response.error->kind)) + "): " + response.error->message +
                "\r\n" + httpStatus + "; captured content may be incomplete." : httpStatus;
        summary += "\r\n" + std::to_string(response.elapsed.count()) + " ms | received " +
            std::to_string(response.downloadedBytes) + " bytes | sent " +
            std::to_string(response.uploadedBytes) + " bytes | redirects " + std::to_string(response.redirects);
        if (!response.tlsVerificationEnabled) summary += "\r\nWARNING: TLS verification was DISABLED for this Send.";
        headers = response.rawHeaders;
        body = response.bodyIsText ? response.body :
            "[Binary or non-UTF-8 response: " + std::to_string(response.body.size()) +
            " captured bytes. Text display suppressed.]";
    }
    SetDlgItemText(IDC_RESPONSE_SUMMARY, FromUtf8(summary));
    m_headers.SetWindowText(FromUtf8(headers));
    m_body.SetWindowText(FromUtf8(body));
}

void CResponseView::OnSize(UINT type, int width, int height)
{
    CFormView::OnSize(type, width, height);
    LayoutResponse();
}

void CResponseView::LayoutResponse()
{
    if (!m_body.GetSafeHwnd() || m_layoutActive) return;
    m_layoutActive = true;
    const int dpi = static_cast<int>(GetDpiForWindow(m_hWnd));
    auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    CRect rect;
    GetClientRect(rect);
    const int width = (std::max)(rect.Width(), scale(460));
    const int height = (std::max)(rect.Height(), scale(180));
    SetScrollSizes(MM_TEXT, CSize(width, height));
    const auto scroll = GetScrollPosition();
    auto place = [this, scroll](int id, int x, int y, int w, int h)
        { GetDlgItem(id)->MoveWindow(x - scroll.x, y - scroll.y, w, h); };
    const int margin = scale(10), gap = scale(8), label = scale(20), top = scale(104);
    const int headerWidth = (width - 2 * margin - gap) / 3;
    const int bodyX = margin + headerWidth + gap;
    place(IDC_RESPONSE_SUMMARY, margin, margin, width - 2 * margin, scale(66));
    place(IDC_RESPONSE_HEADERS_LABEL, margin, top - label, headerWidth, label);
    place(IDC_RESPONSE_BODY_LABEL, bodyX, top - label, width - margin - bodyX, label);
    place(IDC_RESPONSE_HEADERS, margin, top, headerWidth, height - top - margin);
    place(IDC_RESPONSE_BODY, bodyX, top, width - margin - bodyX, height - top - margin);
    m_layoutActive = false;
}

void CResponseView::FocusResponse(bool last)
{
    GetParentFrame()->SetActiveView(this);
    if (last) m_body.SetFocus(); else m_headers.SetFocus();
}

BOOL CResponseView::PreTranslateMessage(MSG* message)
{
    if (message->message == WM_KEYDOWN && message->wParam == VK_TAB && GetKeyState(VK_CONTROL) >= 0)
    {
        const bool backwards = GetKeyState(VK_SHIFT) < 0;
        if ((backwards && message->hwnd == m_headers.m_hWnd) || (!backwards && message->hwnd == m_body.m_hWnd))
        {
            auto* frame = static_cast<CCollectionFrame*>(GetParentFrame());
            if (backwards) static_cast<CRequestEditor*>(frame->Editor())->FocusEditor(true);
            else { frame->SetActiveView(frame->Tree()); frame->Tree()->SetFocus(); }
            return TRUE;
        }
    }
    return CFormView::PreTranslateMessage(message);
}
