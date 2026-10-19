// Application lifetime and collection document registration.
#include "framework.h"
#include "EasyPost32.h"
#include "CollectionWindow.h"

BEGIN_MESSAGE_MAP(CEasyPost32App, CWinApp)
    ON_COMMAND(ID_FILE_NEW, &CWinApp::OnFileNew)
    ON_UPDATE_COMMAND_UI(ID_FILE_OPEN, &CEasyPost32App::OnUpdateFileOpen)
    ON_COMMAND(ID_APP_ABOUT, &CEasyPost32App::OnAppAbout)
END_MESSAGE_MAP()

CEasyPost32App theApp;

BOOL CEasyPost32App::InitInstance()
{
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
    if (!InitCommonControlsEx(&controls) || !CWinApp::InitInstance())
        return FALSE;

    // No shell registration, recent-file list, online setup or startup requests.
    auto* collectionTemplate = new CMultiDocTemplate(
        IDR_COLLECTION, RUNTIME_CLASS(CCollectionDoc),
        RUNTIME_CLASS(CCollectionFrame), RUNTIME_CLASS(CCollectionView));
    AddDocTemplate(collectionTemplate);

    auto* frame = new CMainFrame;
    m_pMainWnd = frame;
    if (!frame->LoadFrame(IDR_MAINFRAME))
    {
        m_pMainWnd = nullptr;
        return FALSE;
    }

    frame->ShowWindow(m_nCmdShow);
    frame->UpdateWindow();
    OnFileNew();
    return TRUE;
}

void CEasyPost32App::OnUpdateFileOpen(CCmdUI* command)
{
    command->Enable(FALSE); // Local collection files begin in Slice 5.
}

void CEasyPost32App::OnAppAbout()
{
    CDialog about(IDD_ABOUTBOX);
    about.DoModal();
}
