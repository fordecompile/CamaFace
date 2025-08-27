#include "stdafx.h"
#include "BFC388Tool.h"
#include "BFC388ToolDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CBFC388ToolApp theApp;

CBFC388ToolApp::CBFC388ToolApp() : m_gdiplusToken(0) {}

BOOL CBFC388ToolApp::InitInstance()
{
    CWinApp::InitInstance();
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        AfxMessageBox(L"Failed to initialize GDI+.");
        return FALSE;
    }

    // Init common controls
    INITCOMMONCONTROLSEX InitCtrls;
    InitCtrls.dwSize = sizeof(InitCtrls);
    InitCtrls.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&InitCtrls);

    // Enable visual styles
    CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

    // Create dialog
    CBFC388ToolDlg dlg;
    m_pMainWnd = &dlg;
    INT_PTR nResponse = dlg.DoModal();
    UNREFERENCED_PARAMETER(nResponse);
    return FALSE;
}

int CBFC388ToolApp::ExitInstance()
{
    if (m_gdiplusToken) {
        GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
    return CWinApp::ExitInstance();
}
