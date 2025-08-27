#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN            // Exclude rarely-used stuff from Windows headers
#endif

#include "targetver.h"

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS      // some CString constructors will be explicit

// MFC core and standard components
#include <afxwin.h>
#include <afxext.h>
#include <afxcmn.h>
#include <afxdlgs.h>
#include <afxcontrolbars.h>

// Support for GDI+
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// Windows
#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")

// C runtime
#include <stdint.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <memory>

using namespace Gdiplus;
