#pragma once

#include <string>

// Explicit UTF-8 conversions are confined to the Windows UI boundary.
CString FromUtf8(const std::string& text);

class CRequestEditor : public CFormView
{
    DECLARE_DYNCREATE(CRequestEditor)
public:
    CRequestEditor();
    void FocusEditor(bool last);

protected:
    void DoDataExchange(CDataExchange* exchange) override;
    void OnInitialUpdate() override;
    void OnUpdate(CView* sender, LPARAM hint, CObject* object) override;
    BOOL PreTranslateMessage(MSG* message) override;
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnDraftChanged();
    DECLARE_MESSAGE_MAP()

private:
    void LayoutEditor();
    CComboBox m_method;
    CEdit m_url;
    CEdit m_headers;
    CEdit m_body;
    CFont m_sourceFont;
    bool m_loading = false;
    bool m_layoutActive = false;
};
