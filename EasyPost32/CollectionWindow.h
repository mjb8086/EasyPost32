#pragma once

// Minimal MFC document/view scaffold. Each document owns its own child/view.
class CCollectionDoc : public CDocument
{
    DECLARE_DYNCREATE(CCollectionDoc)
public:
    BOOL OnNewDocument() override;
    BOOL OnOpenDocument(LPCTSTR path) override;
    BOOL OnSaveDocument(LPCTSTR path) override;
};

class CCollectionView : public CView
{
    DECLARE_DYNCREATE(CCollectionView)
protected:
    void OnDraw(CDC* dc) override;
};

class CCollectionFrame : public CMDIChildWnd
{
    DECLARE_DYNCREATE(CCollectionFrame)
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
