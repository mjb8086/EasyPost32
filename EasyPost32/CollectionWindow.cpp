#include "framework.h"
#include "Resource.h"
#include "CollectionWindow.h"
#include "RequestEditor.h"
#include "ResponseView.h"
#include "RequestComposer.h"
#include "EasyPost32.h"
#include "CollectionDialogs.h"
#include <algorithm>
#include <afxdlgs.h>

IMPLEMENT_DYNCREATE(CCollectionDoc, CDocument)
IMPLEMENT_DYNCREATE(CCollectionView, CTreeView)
IMPLEMENT_DYNCREATE(CCollectionFrame, CMDIChildWnd)

CCollectionDoc::~CCollectionDoc()
{
    for (auto& [id, run] : m_runs)
        if (run.job) run.job->Cancel();
}

BOOL CCollectionDoc::OnNewDocument()
{
    if (!CDocument::OnNewDocument())
        return FALSE;

    m_collection = easypost::NewCollection(ToUtf8(GetTitle()));
    m_selectedId = easypost::FirstRequestId(m_collection.items);
    SetModifiedFlag(FALSE);
    return TRUE;
}

void CCollectionDoc::Send()
{
    if (!HasRequest()) return;
    auto& run = m_runs[m_selectedId];
    if (run.Pending() || CurrentRequest().url.empty()) return;
    easypost::HttpRequest request;
    std::string error;
    RequestDraft resolved;
    try { resolved = easypost::ResolveDraft(CurrentRequest(), m_collection.variables); }
    catch (const std::exception& failure) { error = failure.what(); }
    if (!error.empty() || !ComposeRequest(resolved, request, error))
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
            run.recordHistory = theApp.history.enabled;
            if (run.recordHistory)
                run.history = { easypost::UtcTimestamp(), resolved.method,
                    easypost::HistoryUrl(resolved.url, m_collection.variables) };
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
    auto& run = m_runs[m_selectedId];
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
    for (auto& [id, run] : m_runs)
        if (run.Pending() && run.job->Done())
        {
            run.completionDispatched = true;
            changed = true;
            if (run.recordHistory && theApp.history.enabled)
            {
                const auto& response = run.job->Response();
                run.history.status = response.status;
                run.history.elapsedMs = response.elapsed.count();
                run.history.transportError = response.error.has_value();
                try { theApp.history.Record(run.history); }
                catch (const std::exception&)
                {
                    theApp.history.enabled = false;
                    AfxMessageBox(L"Unable to save local history. Recording has been disabled; your response is still available.");
                }
            }
        }
    theApp.httpJobs.Reap();
    if (changed) UpdateAllViews(nullptr, 1);
}

bool CCollectionDoc::HasRequest()
{
    const auto* item = easypost::FindItem(m_collection.items, m_selectedId);
    return item && item->request.has_value();
}
RequestDraft& CCollectionDoc::CurrentRequest()
{
    auto* item = easypost::FindItem(m_collection.items, m_selectedId);
    return item && item->request ? *item->request : m_noRequest;
}
const CCollectionDoc::RequestRun& CCollectionDoc::CurrentRun() const
{
    static const RequestRun empty;
    const auto found = m_runs.find(m_selectedId);
    return found == m_runs.end() ? empty : found->second;
}
void CCollectionDoc::SelectItem(const std::string& id)
{
    if (id == m_selectedId) return;
    m_selectedId = id;
    UpdateAllViews(nullptr);
}
void CCollectionDoc::Changed(CView* sender, LPARAM hint)
{
    SetModifiedFlag(TRUE);
    UpdateAllViews(sender, hint);
}
void CCollectionDoc::AdoptImport(easypost::Collection collection)
{
    for (auto& [id, run] : m_runs) if (run.job) run.job->Cancel();
    m_runs.clear();
    m_collection = std::move(collection);
    m_selectedId = easypost::FirstRequestId(m_collection.items);
    SetTitle(FromUtf8(m_collection.name));
    Changed(nullptr, 2); // Replace the initial blank tree and its old item IDs.
}
BOOL CCollectionDoc::OnOpenDocument(LPCTSTR path)
{
    try
    {
        auto candidate = easypost::LoadCollection(path);
        for (auto& [id, run] : m_runs) if (run.job) run.job->Cancel();
        m_collection = std::move(candidate);
        m_selectedId = easypost::FirstRequestId(m_collection.items);
        m_runs.clear();
        SetModifiedFlag(FALSE);
        return TRUE;
    }
    catch (const std::exception& error) { AfxMessageBox(FromUtf8(error.what())); return FALSE; }
}
BOOL CCollectionDoc::OnSaveDocument(LPCTSTR path)
{
    try { easypost::SaveCollection(path, m_collection); SetModifiedFlag(FALSE); return TRUE; }
    catch (const std::exception& error) { AfxMessageBox(FromUtf8(error.what())); return FALSE; }
}
BOOL CCollectionDoc::DoSave(LPCTSTR path, BOOL replace)
{
    // CDocument::DoSave deletes the destination after a failed Save As. Our
    // atomic writer already owns cleanup; retain the original on every failure.
    CString destination(path);
    if (destination.IsEmpty())
    {
        destination = GetPathName();
        if (destination.IsEmpty()) destination = GetTitle() + L".dp32.json";
        if (!AfxGetApp()->DoPromptFileName(destination, replace ? AFX_IDS_SAVEFILE : AFX_IDS_SAVEFILECOPY,
                OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT, FALSE, GetDocTemplate())) return FALSE;
    }
    CWaitCursor wait;
    if (!OnSaveDocument(destination)) return FALSE;
    if (replace) SetPathName(destination, FALSE);
    return TRUE;
}
void CCollectionDoc::EditItem(UINT command)
{
    auto* selected = easypost::FindItem(m_collection.items, m_selectedId);
    if (command == ID_ITEM_RENAME)
    {
        CNameDialog dialog(FromUtf8(selected ? selected->Name() : m_collection.name));
        if (dialog.DoModal() != IDOK) return;
        const auto name = ToUtf8(dialog.name);
        if (!selected) m_collection.name = name;
        else if (selected->request) selected->request->name = name;
        else selected->folderName = name;
    }
    else if (command == ID_ITEM_NEW_REQUEST || command == ID_ITEM_NEW_FOLDER)
    {
        CNameDialog dialog(command == ID_ITEM_NEW_FOLDER ? L"New folder" : L"New request");
        if (dialog.DoModal() != IDOK) return;
        auto* destination = selected && !selected->request ? &selected->items :
            selected ? easypost::ItemSiblings(m_collection.items, selected->id) : &m_collection.items;
        easypost::CollectionItem item;
        item.id = easypost::NewId();
        if (command == ID_ITEM_NEW_REQUEST) item.request = RequestDraft{ ToUtf8(dialog.name) };
        else item.folderName = ToUtf8(dialog.name);
        m_selectedId = item.id;
        destination->push_back(std::move(item));
    }
    else if (selected)
    {
        auto* siblings = easypost::ItemSiblings(m_collection.items, selected->id);
        auto position = std::find_if(siblings->begin(), siblings->end(), [selected](const auto& item) { return &item == selected; });
        if (command == ID_ITEM_DUPLICATE)
        {
            auto copy = *selected;
            easypost::RenewIds(copy);
            if (copy.request) copy.request->name += " copy"; else copy.folderName += " copy";
            m_selectedId = copy.id;
            siblings->insert(position + 1, std::move(copy));
        }
        else if (command == ID_ITEM_DELETE)
        {
            if (AfxMessageBox(L"Delete the selected item and its contents?", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
            // Cancel jobs below a removed subtree before invalidating its IDs.
            std::erase_if(m_runs, [selected](auto& entry)
            {
                if (entry.first != selected->id && !easypost::FindItem(selected->items, entry.first)) return false;
                if (entry.second.job) entry.second.job->Cancel();
                return true;
            });
            siblings->erase(position);
            m_selectedId = easypost::FirstRequestId(m_collection.items);
        }
        else if (command == ID_ITEM_MOVE_UP && position != siblings->begin()) std::iter_swap(position, position - 1);
        else if (command == ID_ITEM_MOVE_DOWN && position + 1 != siblings->end()) std::iter_swap(position, position + 1);
        else return;
    }
    else return;
    Changed(nullptr, 2);
}

BEGIN_MESSAGE_MAP(CCollectionView, CTreeView)
    ON_NOTIFY_REFLECT(TVN_SELCHANGED, &CCollectionView::OnSelectionChanged)
END_MESSAGE_MAP()

void CCollectionView::OnInitialUpdate()
{
    CTreeView::OnInitialUpdate();
    auto& tree = GetTreeCtrl();
    tree.ModifyStyle(0, TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
        TVS_SHOWSELALWAYS | WS_TABSTOP);
    OnUpdate(nullptr, 0, nullptr);
}

void CCollectionView::InsertItems(const std::vector<easypost::CollectionItem>& items, HTREEITEM parent)
{
    auto& tree = GetTreeCtrl();
    for (const auto& item : items)
    {
        const auto handle = tree.InsertItem(FromUtf8(item.request ? item.request->method + "  " + item.Name() : item.Name()), parent);
        m_ids[handle] = item.id;
        InsertItems(item.items, handle);
        if (!item.request) tree.Expand(handle, TVE_EXPAND);
    }
}
void CCollectionView::OnUpdate(CView*, LPARAM hint, CObject*)
{
    if (!GetSafeHwnd()) return;
    auto* document = static_cast<CCollectionDoc*>(GetDocument());
    auto& tree = GetTreeCtrl();
    if (!m_root || hint == 2)
    {
        m_rebuilding = true;
        tree.DeleteAllItems();
        m_ids.clear();
        m_root = tree.InsertItem(FromUtf8(document->Collection().name));
        m_ids[m_root] = "";
        InsertItems(document->Collection().items, m_root);
        tree.Expand(m_root, TVE_EXPAND);
        for (const auto& [handle, id] : m_ids)
            if (id == document->SelectedId()) { tree.SelectItem(handle); break; }
        m_rebuilding = false;
    }
    else if (hint != 1)
    {
        // Draft edits can only change the selected label. Avoid searching the
        // whole model for every tree node on each keystroke in large collections.
        const auto* item = easypost::FindItem(document->Collection().items, document->SelectedId());
        if (item)
            for (const auto& [handle, id] : m_ids)
                if (id == item->id)
                {
                    tree.SetItemText(handle, FromUtf8(item->request ? item->request->method + "  " + item->Name() : item->Name()));
                    break;
                }
    }
}

void CCollectionView::OnSelectionChanged(NMHDR* header, LRESULT* result)
{
    *result = 0;
    auto* change = reinterpret_cast<NMTREEVIEW*>(header);
    if (!m_rebuilding && m_ids.contains(change->itemNew.hItem))
        static_cast<CCollectionDoc*>(GetDocument())->SelectItem(m_ids.at(change->itemNew.hItem));
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
    ON_COMMAND_RANGE(ID_ITEM_NEW_REQUEST, ID_ITEM_MOVE_DOWN, &CCollectionFrame::OnItemCommand)
    ON_UPDATE_COMMAND_UI_RANGE(ID_ITEM_NEW_REQUEST, ID_ITEM_MOVE_DOWN, &CCollectionFrame::OnUpdateItemCommand)
    ON_COMMAND(ID_COLLECTION_VARIABLES, &CCollectionFrame::OnVariables)
END_MESSAGE_MAP()

void CCollectionFrame::OnItemCommand(UINT command)
{
    static_cast<CCollectionDoc*>(GetActiveDocument())->EditItem(command);
}
void CCollectionFrame::OnUpdateItemCommand(CCmdUI* command)
{
    auto* document = static_cast<CCollectionDoc*>(GetActiveDocument());
    command->Enable(document && (command->m_nID <= ID_ITEM_RENAME || !document->SelectedId().empty()));
}
void CCollectionFrame::OnVariables()
{
    auto* document = static_cast<CCollectionDoc*>(GetActiveDocument());
    CVariablesDialog dialog(document->Collection().variables);
    if (dialog.DoModal() == IDOK && document->Collection().variables != dialog.variables)
    {
        document->Collection().variables = std::move(dialog.variables);
        document->Changed();
    }
}

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
