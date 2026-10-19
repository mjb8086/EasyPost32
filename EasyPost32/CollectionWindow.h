#pragma once

#include "RequestDraft.h"
#include "Http/HttpJobs.h"
#include <vector>

class CCollectionDoc : public CDocument
{
    DECLARE_DYNCREATE(CCollectionDoc)
public:
    ~CCollectionDoc() override;
    BOOL OnNewDocument() override;
    BOOL OnOpenDocument(LPCTSTR path) override;
    BOOL OnSaveDocument(LPCTSTR path) override;

    const std::vector<RequestDraft>& Requests() const { return m_requests; }
    RequestDraft& CurrentRequest() { return m_requests.at(m_selectedRequest); }
    void SelectRequest(size_t index);
    struct RequestRun
    {
        std::shared_ptr<easypost::HttpJob> job;
        std::string preparationError;
        bool completionDispatched = false;
        bool cancelling = false;
        bool Pending() const { return job && !completionDispatched; }
    };
    const RequestRun& CurrentRun() const { return m_runs.at(m_selectedRequest); }
    void Send();
    void Cancel();
    void PollCompletions();

private:
    std::vector<RequestDraft> m_requests;
    size_t m_selectedRequest = 0;
    std::vector<RequestRun> m_runs;
};

class CCollectionView : public CTreeView
{
    DECLARE_DYNCREATE(CCollectionView)
protected:
    void OnInitialUpdate() override;
    void OnUpdate(CView* sender, LPARAM hint, CObject* object) override;
    BOOL PreTranslateMessage(MSG* message) override;
    afx_msg void OnSelectionChanging(NMHDR* header, LRESULT* result);
    afx_msg void OnSelectionChanged(NMHDR* header, LRESULT* result);
    DECLARE_MESSAGE_MAP()

private:
    HTREEITEM m_root = nullptr;
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
