#pragma once
#include "Core/Collection.h"

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
    DECLARE_MESSAGE_MAP()
private:
    void Refresh();
};
