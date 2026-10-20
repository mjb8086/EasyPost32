#pragma once

#include "RequestDraft.h"
#include "Http/HttpJobs.h"
#include "Core/Collection.h"
#include "Core/Storage.h"
#include <map>
#include <vector>

class CCollectionDoc : public CDocument
{
    DECLARE_DYNCREATE(CCollectionDoc)
public:
    ~CCollectionDoc() override;
    BOOL OnNewDocument() override;
    BOOL OnOpenDocument(LPCTSTR path) override;
    BOOL OnSaveDocument(LPCTSTR path) override;
    BOOL DoSave(LPCTSTR path, BOOL replace = TRUE) override;
    void AdoptImport(easypost::Collection collection);

    easypost::Collection& Collection() { return m_collection; }
    bool HasRequest();
    RequestDraft& CurrentRequest();
    const std::string& SelectedId() const { return m_selectedId; }
    void SelectItem(const std::string& id);
    void Changed(CView* sender = nullptr, LPARAM hint = 0);
    void EditItem(UINT command);
    struct RequestRun
    {
        std::shared_ptr<easypost::HttpJob> job;
        std::string preparationError;
        bool completionDispatched = false;
        bool cancelling = false;
        bool recordHistory = false;
        easypost::HistoryEntry history;
        bool Pending() const { return job && !completionDispatched; }
    };
    const RequestRun& CurrentRun() const;
    void Send();
    void Cancel();
    void PollCompletions();

private:
    easypost::Collection m_collection;
    std::string m_selectedId;
    RequestDraft m_noRequest;
    std::map<std::string, RequestRun> m_runs;
};

class CCollectionView : public CTreeView
{
    DECLARE_DYNCREATE(CCollectionView)
protected:
    void OnInitialUpdate() override;
    void OnUpdate(CView* sender, LPARAM hint, CObject* object) override;
    BOOL PreTranslateMessage(MSG* message) override;
    afx_msg void OnSelectionChanged(NMHDR* header, LRESULT* result);
    DECLARE_MESSAGE_MAP()

private:
    HTREEITEM m_root = nullptr;
    std::map<HTREEITEM, std::string> m_ids;
    bool m_rebuilding = false;
    void InsertItems(const std::vector<easypost::CollectionItem>& items, HTREEITEM parent);
};

// Preserve the user's request/response proportion when its MDI child resizes.
class CResponseSplitter : public CSplitterWnd
{
protected:
    afx_msg void OnSize(UINT type, int width, int height);
    DECLARE_MESSAGE_MAP()
private:
    int m_previousHeight = 0;
};

class CCollectionFrame : public CMDIChildWnd
{
    DECLARE_DYNCREATE(CCollectionFrame)
public:
    CView* Tree() { return static_cast<CView*>(m_splitter.GetPane(0, 0)); }
    CView* Editor() { return static_cast<CView*>(m_rightSplitter.GetPane(0, 0)); }
    CView* Response() { return static_cast<CView*>(m_rightSplitter.GetPane(1, 0)); }
protected:
    BOOL OnCreateClient(LPCREATESTRUCT create, CCreateContext* context) override;
    afx_msg void OnSend();
    afx_msg void OnCancel();
    afx_msg void OnUpdateSend(CCmdUI* command);
    afx_msg void OnUpdateCancel(CCmdUI* command);
    afx_msg void OnNextPane();
    afx_msg void OnPreviousPane();
    afx_msg void OnItemCommand(UINT command);
    afx_msg void OnUpdateItemCommand(CCmdUI* command);
    afx_msg void OnVariables();
    DECLARE_MESSAGE_MAP()

private:
    CSplitterWnd m_splitter;
    CResponseSplitter m_rightSplitter;
};

class CMainFrame : public CMDIFrameWnd
{
protected:
    afx_msg int OnCreate(LPCREATESTRUCT create);
    DECLARE_MESSAGE_MAP()

private:
    CToolBar m_toolbar;
    CStatusBar m_statusBar;
};
