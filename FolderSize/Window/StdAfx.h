// Windows 10 (version 1607 or later) is the minimum: the per-monitor DPI functions
// (GetDpiForWindow, SystemParametersInfoForDpi, ...) need it.
#define _WIN32_WINNT 0x0A00

#include <atlbase.h>
#include <atlcom.h>
#include <atlwin.h>

#include <commctrl.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <shlobj.h>
#include <exdisp.h>
#include <exdispid.h>
#include <uxtheme.h>
#include <dwmapi.h>

#include <tchar.h>
#include <assert.h>

#include <vector>
#include <map>

using namespace std;

#include "..\Common\Pipe.h"
