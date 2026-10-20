// Application lifetime and collection document registration.
#include "framework.h"
#include "EasyPost32.h"
#include "CollectionWindow.h"
#include "CollectionDialogs.h"
#include "RequestEditor.h"
#include "Core/Interop.h"
#include <afxdlgs.h>

BEGIN_MESSAGE_MAP(CEasyPost32App, CWinApp)
    ON_COMMAND(ID_FILE_NEW, &CWinApp::OnFileNew)
    ON_COMMAND(ID_FILE_OPEN, &CWinApp::OnFileOpen)
    ON_COMMAND(ID_LOCAL_HISTORY, &CEasyPost32App::OnHistory)
    ON_COMMAND_RANGE(ID_IMPORT_POSTMAN, ID_IMPORT_OPENAPI, &CEasyPost32App::OnImport)
    ON_COMMAND(ID_EXPORT_POSTMAN, &CEasyPost32App::OnExportPostman)
    ON_UPDATE_COMMAND_UI(ID_EXPORT_POSTMAN, &CEasyPost32App::OnUpdateExport)
    ON_COMMAND(ID_APP_ABOUT, &CEasyPost32App::OnAppAbout)
END_MESSAGE_MAP()

CEasyPost32App theApp;

int CEasyPost32App::ExitInstance()
{
    httpJobs.Shutdown();
    return CWinApp::ExitInstance();
}

namespace
{
class CCollectionDocManager : public CDocManager
{
    BOOL DoPromptFileName(CString& fileName, UINT title, DWORD flags, BOOL open, CDocTemplate*) override;
};
BOOL CCollectionDocManager::DoPromptFileName(CString& fileName, UINT title, DWORD flags, BOOL open, CDocTemplate*)
{
    // Use the classic native Explorer file chooser, consistent with the MDI UI.
    CFileDialog dialog(open, L"dp32.json", fileName, flags | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_DONTADDTORECENT,
        L"EasyPost32 collections (*.dp32.json)|*.dp32.json|All files (*.*)|*.*||", AfxGetMainWnd(), 0, FALSE);
    CString caption;
    caption.LoadString(title);
    if (!caption.IsEmpty()) dialog.m_ofn.lpstrTitle = caption.GetString();
    if (dialog.DoModal() != IDOK) return FALSE;
    fileName = dialog.GetPathName();
    return TRUE;
}
}

BOOL CEasyPost32App::InitInstance()
{
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
    if (!InitCommonControlsEx(&controls) || !CWinApp::InitInstance())
        return FALSE;

    // No shell registration, recent-file list, online setup or startup requests.
    m_pDocManager = new CCollectionDocManager;
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

void CEasyPost32App::OnHistory()
{
    if (!m_historyInitialized)
    {
        wchar_t directory[32768]{};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", directory, _countof(directory)))
        {
            AfxMessageBox(L"Local application data is unavailable; history cannot be enabled.");
            return;
        }
        history = easypost::HistoryStore(std::filesystem::path(directory) / L"EasyPost32" / L"history.json");
        m_historyInitialized = true;
        try { history.Load(); }
        catch (const std::exception& error) { AfxMessageBox(FromUtf8(error.what())); }
    }
    CHistoryDialog dialog;
    dialog.DoModal();
}

void CEasyPost32App::OnAppAbout()
{
    CDialog about(IDD_ABOUTBOX);
    about.DoModal();
}

void CEasyPost32App::OnImport(UINT command)
{
    CFileDialog file(TRUE, L"json", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT,
        L"JSON files (*.json)|*.json|All files (*.*)|*.*||", m_pMainWnd, 0, FALSE);
    file.m_ofn.lpstrTitle = command == ID_IMPORT_POSTMAN ? L"Import Postman v2.1 JSON" : L"Import OpenAPI 3.0/3.1 JSON";
    if (file.DoModal() != IDOK) return;
    try
    {
        const auto source = easypost::ReadTextFile(file.GetPathName().GetString());
        auto candidate = command == ID_IMPORT_POSTMAN ? easypost::ImportPostman(source) : easypost::ImportOpenApi(source);
        CCompatibilityDialog report(L"Import compatibility report", candidate.compatibilityNotes);
        if (report.DoModal() != IDOK) return;
        auto position = GetFirstDocTemplatePosition();
        auto* docTemplate = GetNextDocTemplate(position);
        auto* document = static_cast<CCollectionDoc*>(docTemplate->OpenDocumentFile(nullptr));
        if (document) document->AdoptImport(std::move(candidate));
    }
    catch (const std::exception& error) { AfxMessageBox(FromUtf8(error.what())); }
}
void CEasyPost32App::OnUpdateExport(CCmdUI* command)
{
    auto* frame = static_cast<CMDIFrameWnd*>(m_pMainWnd);
    command->Enable(frame && frame->MDIGetActive() != nullptr);
}
void CEasyPost32App::OnExportPostman()
{
    auto* child = static_cast<CMDIFrameWnd*>(m_pMainWnd)->MDIGetActive();
    if (!child) return;
    auto* document = static_cast<CCollectionDoc*>(child->GetActiveDocument());
    if (!document) return;
    try
    {
        const auto output = easypost::ExportPostman(document->Collection());
        // Review all inherited/source loss notes BEFORE choosing or writing a destination.
        CCompatibilityDialog report(L"Export compatibility report", output.notes);
        if (report.DoModal() != IDOK) return;
        CFileDialog file(FALSE, L"json", L"collection.postman_collection.json",
            OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT,
            L"Postman v2.1 JSON (*.json)|*.json||", m_pMainWnd, 0, FALSE);
        file.m_ofn.lpstrTitle = L"Export Postman v2.1 JSON";
        if (file.DoModal() == IDOK) easypost::WriteTextAtomic(file.GetPathName().GetString(), output.text);
    }
    catch (const std::exception& error) { AfxMessageBox(FromUtf8(error.what())); }
}
