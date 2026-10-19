#pragma once

#include "Resource.h"
#include "Http/HttpJobs.h"

class CEasyPost32App : public CWinApp
{
public:
    BOOL InitInstance() override;
    int ExitInstance() override;
    easypost::HttpJobs httpJobs;

private:
    afx_msg void OnUpdateFileOpen(CCmdUI* command);
    afx_msg void OnAppAbout();
    DECLARE_MESSAGE_MAP()
};

extern CEasyPost32App theApp;
