#pragma once
#include "stdafx.h"

// Custom messages
#define WM_APP_NOTE     (WM_APP + 100)
#define WM_APP_REPLY    (WM_APP + 101)
#define WM_APP_IMAGE    (WM_APP + 102)

class CBFC388ToolApp : public CWinApp
{
public:
    CBFC388ToolApp();

public:
    virtual BOOL InitInstance();
    virtual int ExitInstance();

private:
    ULONG_PTR m_gdiplusToken;
};

extern CBFC388ToolApp theApp;
