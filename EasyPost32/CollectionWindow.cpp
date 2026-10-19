#include "framework.h"
#include "Resource.h"
#include "CollectionWindow.h"

IMPLEMENT_DYNCREATE(CCollectionDoc, CDocument)
IMPLEMENT_DYNCREATE(CCollectionView, CView)
IMPLEMENT_DYNCREATE(CCollectionFrame, CMDIChildWnd)

BOOL CCollectionDoc::OnNewDocument()
{
    return CDocument::OnNewDocument();
}

BOOL CCollectionDoc::OnOpenDocument(LPCTSTR)
{
    AfxMessageBox(L"Opening collection files will be available in Slice 5.");
    return FALSE;
}

BOOL CCollectionDoc::OnSaveDocument(LPCTSTR)
{
    AfxMessageBox(L"Saving collection files will be available in Slice 5.");
    return FALSE;
}

void CCollectionView::OnDraw(CDC* dc)
{
    CRect client;
    GetClientRect(&client);
    client.DeflateRect(16, 16);
    dc->SetBkMode(TRANSPARENT);
    dc->SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
    CFont* previous = dc->SelectObject(CFont::FromHandle(
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))));
    const CString text = GetDocument()->GetTitle() + L"\n\nEmpty collection";
    dc->DrawText(text, client, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
    dc->SelectObject(previous);
}

BEGIN_MESSAGE_MAP(CMainFrame, CMDIFrameWnd)
    ON_WM_CREATE()
END_MESSAGE_MAP()

int CMainFrame::OnCreate(LPCREATESTRUCT create)
{
    if (CMDIFrameWnd::OnCreate(create) == -1)
        return -1;

    // Native text buttons avoid introducing a bitmap/icon system for two commands.
    const UINT buttons[] = { ID_FILE_NEW, ID_WINDOW_CASCADE };
    if (!m_toolbar.CreateEx(this, TBSTYLE_FLAT | TBSTYLE_LIST,
            WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY) ||
        !m_toolbar.SetButtons(buttons, _countof(buttons)))
        return -1;

    m_toolbar.SetButtonInfo(0, ID_FILE_NEW, TBBS_BUTTON, I_IMAGENONE);
    m_toolbar.SetButtonInfo(1, ID_WINDOW_CASCADE, TBBS_BUTTON, I_IMAGENONE);
    m_toolbar.SetButtonText(0, L"New Collection");
    m_toolbar.SetButtonText(1, L"Cascade");
    m_toolbar.SetSizes(CSize(120, 28), CSize(16, 15));

    const UINT indicators[] = { ID_SEPARATOR };
    if (!m_statusBar.Create(this) ||
        !m_statusBar.SetIndicators(indicators, _countof(indicators)))
        return -1;

    return 0;
}
