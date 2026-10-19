#pragma once
#include "Http/HttpJobs.h"

class CResponseView : public CFormView
{
    DECLARE_DYNCREATE(CResponseView)
public:
    CResponseView();
    void FocusResponse(bool last);
protected:
    void DoDataExchange(CDataExchange* exchange) override;
    void OnInitialUpdate() override;
    void OnUpdate(CView* sender, LPARAM hint, CObject* object) override;
    BOOL PreTranslateMessage(MSG* message) override;
    afx_msg void OnSize(UINT type, int width, int height);
    DECLARE_MESSAGE_MAP()
private:
    void LayoutResponse();
    CEdit m_headers, m_body;
    CFont m_sourceFont;
    std::shared_ptr<easypost::HttpJob> m_displayedJob;
    std::string m_displayedError;
    bool m_pending = false, m_cancelling = false, m_initialized = false, m_layoutActive = false;
};
