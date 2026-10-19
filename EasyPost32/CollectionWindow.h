#pragma once

#include "RequestDraft.h"
#include <vector>

class CCollectionDoc : public CDocument
{
    DECLARE_DYNCREATE(CCollectionDoc)
public:
    BOOL OnNewDocument() override;
    BOOL OnOpenDocument(LPCTSTR path) override;
    BOOL OnSaveDocument(LPCTSTR path) override;

    const std::vector<RequestDraft>& Requests() const { return m_requests; }
    RequestDraft& CurrentRequest() { return m_requests.at(m_selectedRequest); }
    void SelectRequest(size_t index);

private:
    std::vector<RequestDraft> m_requests;
    size_t m_selectedRequest = 0;
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

class CCollectionFrame : public CMDIChildWnd
{
    DECLARE_DYNCREATE(CCollectionFrame)
protected:
    BOOL OnCreateClient(LPCREATESTRUCT create, CCreateContext* context) override;

private:
    CSplitterWnd m_splitter;
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
