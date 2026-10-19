#pragma once

#include "Resource.h"

class CEasyPost32App : public CWinApp
{
public:
    BOOL InitInstance() override;

private:
    afx_msg void OnUpdateFileOpen(CCmdUI* command);
    afx_msg void OnAppAbout();
    DECLARE_MESSAGE_MAP()
};
