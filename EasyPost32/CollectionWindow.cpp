#include "framework.h"
#include "Resource.h"
#include "CollectionWindow.h"
#include "RequestEditor.h"

IMPLEMENT_DYNCREATE(CCollectionDoc, CDocument)
IMPLEMENT_DYNCREATE(CCollectionView, CTreeView)
IMPLEMENT_DYNCREATE(CCollectionFrame, CMDIChildWnd)

BOOL CCollectionDoc::OnNewDocument()
{
    if (!CDocument::OnNewDocument())
        return FALSE;

    m_requests = { RequestDraft{ "Request 1" }, RequestDraft{ "Request 2" } };
    m_selectedRequest = 0;
    return TRUE;
}

void CCollectionDoc::SelectRequest(size_t index)
{
    if (index >= m_requests.size() || index == m_selectedRequest)
        return;
    m_selectedRequest = index;
    UpdateAllViews(nullptr);
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

BEGIN_MESSAGE_MAP(CCollectionView, CTreeView)
    ON_NOTIFY_REFLECT(TVN_SELCHANGING, &CCollectionView::OnSelectionChanging)
    ON_NOTIFY_REFLECT(TVN_SELCHANGED, &CCollectionView::OnSelectionChanged)
END_MESSAGE_MAP()

void CCollectionView::OnInitialUpdate()
{
    CTreeView::OnInitialUpdate();
    auto& tree = GetTreeCtrl();
    tree.ModifyStyle(0, TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
        TVS_SHOWSELALWAYS | WS_TABSTOP);
    m_root = tree.InsertItem(GetDocument()->GetTitle());
    const auto& requests = static_cast<CCollectionDoc*>(GetDocument())->Requests();
    for (size_t index = 0; index < requests.size(); ++index)
    {
        HTREEITEM item = tree.InsertItem(L"", m_root);
        tree.SetItemData(item, static_cast<DWORD_PTR>(index));
    }
    OnUpdate(nullptr, 0, nullptr);
    tree.Expand(m_root, TVE_EXPAND);
    tree.SelectItem(tree.GetChildItem(m_root));
}

void CCollectionView::OnUpdate(CView*, LPARAM, CObject*)
{
    if (m_root == nullptr)
        return;
    auto& tree = GetTreeCtrl();
    auto* document = static_cast<CCollectionDoc*>(GetDocument());
    for (HTREEITEM item = tree.GetChildItem(m_root); item; item = tree.GetNextSiblingItem(item))
    {
        const auto& request = document->Requests().at(tree.GetItemData(item));
        tree.SetItemText(item, FromUtf8(request.method + "  " + request.name));
    }
}

void CCollectionView::OnSelectionChanging(NMHDR* header, LRESULT* result)
{
    auto* change = reinterpret_cast<NMTREEVIEW*>(header);
    *result = change->itemNew.hItem == m_root; // The root is a collection label.
}

void CCollectionView::OnSelectionChanged(NMHDR* header, LRESULT* result)
{
    *result = 0;
    auto* change = reinterpret_cast<NMTREEVIEW*>(header);
    if (change->itemNew.hItem != m_root)
        static_cast<CCollectionDoc*>(GetDocument())->SelectRequest(
            static_cast<size_t>(GetTreeCtrl().GetItemData(change->itemNew.hItem)));
}

BOOL CCollectionView::PreTranslateMessage(MSG* message)
{
    if (message->message == WM_KEYDOWN && message->wParam == VK_TAB &&
        GetKeyState(VK_CONTROL) >= 0)
    {
        auto* splitter = static_cast<CSplitterWnd*>(GetParent());
        auto* editor = static_cast<CRequestEditor*>(splitter->GetPane(0, 1));
        editor->FocusEditor(GetKeyState(VK_SHIFT) < 0);
        return TRUE;
    }
    return CTreeView::PreTranslateMessage(message);
}

BOOL CCollectionFrame::OnCreateClient(LPCREATESTRUCT, CCreateContext* context)
{
    const int treeWidth = MulDiv(220, static_cast<int>(GetDpiForWindow(m_hWnd)), 96);
    return m_splitter.CreateStatic(this, 1, 2) &&
        m_splitter.CreateView(0, 0, RUNTIME_CLASS(CCollectionView), CSize(treeWidth, 100), context) &&
        m_splitter.CreateView(0, 1, RUNTIME_CLASS(CRequestEditor), CSize(400, 100), context);
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
