#pragma once
#include "Core/Collection.h"
#include "Core/Storage.h"
#include "Http/HttpJobs.h"

class CCompatibilityDialog : public CDialog
{
public:
    CCompatibilityDialog(const CString& caption, const std::vector<std::string>& notes);
protected:
    BOOL OnInitDialog() override;
private:
    CString m_caption, m_text;
};

class CNameDialog : public CDialog
{
public:
    explicit CNameDialog(CString initial);
    CString name;
protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
};
class CVariablesDialog : public CDialog
{
public:
    explicit CVariablesDialog(const std::vector<easypost::Variable>& values);
    std::vector<easypost::Variable> variables;
protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    afx_msg void OnSelect();
    afx_msg void OnApply();
    afx_msg void OnDelete();
    afx_msg void OnSecret();
    afx_msg void OnNew();
    DECLARE_MESSAGE_MAP()
private:
    void Refresh(int selection = -1);
    bool m_applyFailed = false;
};
class CHistoryDialog : public CDialog
{
public:
    CHistoryDialog();
protected:
    BOOL OnInitDialog() override;
    afx_msg void OnRecord();
    afx_msg void OnClear();
    afx_msg void OnSelect(NMHDR* header, LRESULT* result);
    afx_msg void OnTab(NMHDR* header, LRESULT* result);
    afx_msg void OnResend();
    afx_msg void OnCancelSend();
    afx_msg void OnTimer(UINT_PTR timer);
    afx_msg void OnDestroy();
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
    DECLARE_MESSAGE_MAP()
private:
    void Refresh();
    void ShowDetails();
    void Layout();
    void CancelPending();
    CListCtrl m_list;
    CTabCtrl m_tabs;
    CFont m_font;
    bool m_rebuilding = false;
    std::uint64_t m_revision = 0, m_selectedId = 0;
    std::shared_ptr<const easypost::HistoryDetails> m_selected, m_resending;
    std::shared_ptr<const easypost::HistoryDetails> m_unsavedResult;
    std::shared_ptr<easypost::HttpJob> m_job;
    easypost::HistoryEntry m_metadata;
};
