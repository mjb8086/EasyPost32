#include "framework.h"
#include "Resource.h"
#include "CollectionWindow.h"
#include "RequestEditor.h"
#include "ResponseView.h"
#include "RequestComposer.h"
#include "EasyPost32.h"

IMPLEMENT_DYNCREATE(CCollectionDoc, CDocument)
IMPLEMENT_DYNCREATE(CCollectionView, CTreeView)
IMPLEMENT_DYNCREATE(CCollectionFrame, CMDIChildWnd)

CCollectionDoc::~CCollectionDoc()
{
    for (auto& run : m_runs)
        if (run.job) run.job->Cancel();
}

BOOL CCollectionDoc::OnNewDocument()
{
    if (!CDocument::OnNewDocument())
        return FALSE;

    m_requests = { RequestDraft{ "Request 1" }, RequestDraft{ "Request 2" } };
    m_selectedRequest = 0;
    m_runs.resize(m_requests.size());
    return TRUE;
}

void CCollectionDoc::Send()
{
    auto& run = m_runs.at(m_selectedRequest);
    if (run.Pending() || CurrentRequest().url.empty()) return;
    easypost::HttpRequest request;
    std::string error;
    if (!ComposeRequest(CurrentRequest(), request, error))
    {
        run = {};
        run.preparationError = std::move(error);
    }
    else
    {
        try
        {
            easypost::HttpOptions options;
            options.verifyTls = CurrentRequest().verifyTls;
            auto job = theApp.httpJobs.Start(std::move(request), options);
            run = {};
            run.job = std::move(job);
        }
        catch (...)
        {
            run = {};
            run.preparationError = "Unable to start the HTTP worker.";
        }
    }
    UpdateAllViews(nullptr, 1);
}

void CCollectionDoc::Cancel()
{
    auto& run = m_runs.at(m_selectedRequest);
    if (run.Pending())
    {
        run.cancelling = true;
        run.job->Cancel();
        UpdateAllViews(nullptr, 1);
    }
}

void CCollectionDoc::PollCompletions()
{
    bool changed = false;
    for (auto& run : m_runs)
        if (run.Pending() && run.job->Done())
        {
            run.completionDispatched = true;
            changed = true;
        }
    theApp.httpJobs.Reap();
    if (changed) UpdateAllViews(nullptr, 1);
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
        auto* frame = static_cast<CCollectionFrame*>(GetParentFrame());
        if (GetKeyState(VK_SHIFT) < 0)
            static_cast<CResponseView*>(frame->Response())->FocusResponse(true);
        else
            static_cast<CRequestEditor*>(frame->Editor())->FocusEditor(false);
        return TRUE;
    }
    return CTreeView::PreTranslateMessage(message);
}

BOOL CCollectionFrame::OnCreateClient(LPCREATESTRUCT, CCreateContext* context)
{
    const int treeWidth = MulDiv(220, static_cast<int>(GetDpiForWindow(m_hWnd)), 96);
    CRect client;
    GetClientRect(client);
    const int requestHeight = client.Height() * 3 / 5;
    return m_splitter.CreateStatic(this, 1, 2) &&
        m_splitter.CreateView(0, 0, RUNTIME_CLASS(CCollectionView), CSize(treeWidth, 100), context) &&
        m_rightSplitter.CreateStatic(&m_splitter, 2, 1, WS_CHILD | WS_VISIBLE,
            m_splitter.IdFromRowCol(0, 1)) &&
        m_rightSplitter.CreateView(0, 0, RUNTIME_CLASS(CRequestEditor),
            CSize(400, requestHeight), context) &&
        m_rightSplitter.CreateView(1, 0, RUNTIME_CLASS(CResponseView), CSize(400, 200), context);
}

BEGIN_MESSAGE_MAP(CResponseSplitter, CSplitterWnd)
    ON_WM_SIZE()
END_MESSAGE_MAP()

void CResponseSplitter::OnSize(UINT type, int width, int height)
{
    if (m_nRows == 2 && m_previousHeight > 0 && height > 0)
    {
        int rowHeight = 0, minimum = 0;
        GetRowInfo(0, rowHeight, minimum);
        SetRowInfo(0, MulDiv(rowHeight, height, m_previousHeight), minimum);
    }
    m_previousHeight = height;
    CSplitterWnd::OnSize(type, width, height);
}

BEGIN_MESSAGE_MAP(CCollectionFrame, CMDIChildWnd)
    ON_COMMAND(ID_REQUEST_SEND, &CCollectionFrame::OnSend)
    ON_COMMAND(ID_REQUEST_CANCEL, &CCollectionFrame::OnCancel)
    ON_UPDATE_COMMAND_UI(ID_REQUEST_SEND, &CCollectionFrame::OnUpdateSend)
    ON_UPDATE_COMMAND_UI(ID_REQUEST_CANCEL, &CCollectionFrame::OnUpdateCancel)
    ON_COMMAND(ID_COLLECTION_NEXT_PANE, &CCollectionFrame::OnNextPane)
    ON_COMMAND(ID_COLLECTION_PREV_PANE, &CCollectionFrame::OnPreviousPane)
END_MESSAGE_MAP()

void CCollectionFrame::OnSend() { static_cast<CCollectionDoc*>(GetActiveDocument())->Send(); }
void CCollectionFrame::OnCancel() { static_cast<CCollectionDoc*>(GetActiveDocument())->Cancel(); }
void CCollectionFrame::OnUpdateSend(CCmdUI* command)
{
    auto* document = static_cast<CCollectionDoc*>(GetActiveDocument());
    command->Enable(document && !document->CurrentRequest().url.empty() && !document->CurrentRun().Pending());
}
void CCollectionFrame::OnUpdateCancel(CCmdUI* command)
{
    auto* document = static_cast<CCollectionDoc*>(GetActiveDocument());
    command->Enable(document && document->CurrentRun().Pending() && !document->CurrentRun().cancelling);
}
void CCollectionFrame::OnNextPane()
{
    if (GetActiveView() == Tree()) static_cast<CRequestEditor*>(Editor())->FocusEditor(false);
    else if (GetActiveView() == Editor()) static_cast<CResponseView*>(Response())->FocusResponse(false);
    else { SetActiveView(Tree()); Tree()->SetFocus(); }
}
void CCollectionFrame::OnPreviousPane()
{
    if (GetActiveView() == Tree()) static_cast<CResponseView*>(Response())->FocusResponse(false);
    else if (GetActiveView() == Response()) static_cast<CRequestEditor*>(Editor())->FocusEditor(false);
    else { SetActiveView(Tree()); Tree()->SetFocus(); }
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
