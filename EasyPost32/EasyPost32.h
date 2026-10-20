#pragma once

#include "Resource.h"
#include "Http/HttpJobs.h"
#include "Core/Storage.h"

class CEasyPost32App : public CWinApp
{
public:
    BOOL InitInstance() override;
    int ExitInstance() override;
    easypost::HttpJobs httpJobs;
    easypost::HistoryStore history;

private:
    afx_msg void OnHistory();
    afx_msg void OnImport(UINT command);
    afx_msg void OnExportPostman();
    afx_msg void OnUpdateExport(CCmdUI* command);
    afx_msg void OnAppAbout();
    DECLARE_MESSAGE_MAP()
    bool m_historyInitialized = false;
};

extern CEasyPost32App theApp;
