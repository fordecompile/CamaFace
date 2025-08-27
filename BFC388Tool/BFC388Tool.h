#pragma once
#include "stdafx.h"

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
