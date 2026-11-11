#include "StdAfx.h"
#include "PropSheet.h"
#include "Resource.h"
#include "Service.h"
#include "../Common/LangStrings.h"

HINSTANCE g_hInstance = NULL;

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		g_hInstance = hInstance;
		DisableThreadLibraryCalls(hInstance);
	}
    return TRUE;
}

// Entry point for rundll32.exe, run by RunElevated (PropSheet.cpp) with administrator rights:
//   rundll32.exe "<path of FolderSize.cpl>",ElevatedCommand <command>
// where <command> is one of:
//   start, stop, pause, continue   controls the service
//   drivetypes <n>                 saves the drive types to scan and tells the service
// The exit code of the process is the error of the command, or 0 if it succeeded.
extern "C" void CALLBACK ElevatedCommandW(HWND hwnd, HINSTANCE hinst, LPWSTR pszCmdLine, int nCmdShow)
{
	DWORD dwError = ERROR_INVALID_PARAMETER;
	if (lstrcmpiW(pszCmdLine, L"start") == 0)
		dwError = ModifyService(MS_START);
	else if (lstrcmpiW(pszCmdLine, L"stop") == 0)
		dwError = ModifyService(MS_STOP);
	else if (lstrcmpiW(pszCmdLine, L"pause") == 0)
		dwError = ModifyService(MS_PAUSE);
	else if (lstrcmpiW(pszCmdLine, L"continue") == 0)
		dwError = ModifyService(MS_CONTINUE);
	else if (StrCmpNIW(pszCmdLine, L"drivetypes ", 11) == 0)
		dwError = SaveDriveTypesAndTellService(StrToIntW(pszCmdLine + 11));
	ExitProcess(dwError);
}

LONG APIENTRY CPlApplet(HWND hwndCPl, UINT uMsg, LPARAM lParam1, LPARAM lParam2)
{
	switch (uMsg)
	{
	case CPL_INIT:
		return TRUE;
	case CPL_GETCOUNT:
		return 1;
	case CPL_INQUIRE:
		if (lParam1 == 0)
		{
			LPCPLINFO pInfo = (LPCPLINFO)lParam2;
			pInfo->idIcon = IDI_ICON;
			pInfo->idName = STR_APP_NAME;  // loaded by Control Panel, in the language of Windows
			pInfo->idInfo = STR_APP_INFO;
			pInfo->lData = NULL;
			return 0;
		}
		break;
/*
	case CPL_NEWINQUIRE:
		if (lParam1 == 0)
		{
			LPNEWCPLINFO pInfo = (LPNEWCPLINFO)lParam2;
			pInfo->dwSize = sizeof(NEWCPLINFO);
			pInfo->lData = NULL;
			LoadString(g_hInstance, STR_APP_NAME, pInfo->szName, countof(pInfo->szName));
			LoadString(g_hInstance, STR_APP_INFO, pInfo->szInfo, countof(pInfo->szInfo));
			pInfo->hIcon = NULL;
			if (SUCCEEDED(CoInitialize(NULL)))
			{
				// get the icon from the shell
				SHFILEINFO shfi;
				if (SHGetFileInfo(TEXT(""), FILE_ATTRIBUTE_NORMAL, &shfi, sizeof(shfi), SHGFI_USEFILEATTRIBUTES | SHGFI_ICON))
				{
					pInfo->hIcon = shfi.hIcon;
				}
				CoUninitialize();
			}
			if (pInfo->hIcon == NULL)
				MessageBox(NULL, L"failed to load icon", NULL, MB_OK);
			return 0;
		}
		break;
*/
	case CPL_DBLCLK:
		if (lParam1 == 0)
		{
			// before doing any GUI, declare to Windows 6+ that we are DPI aware
			HMODULE hUser32 = LoadLibrary(TEXT("user32.dll"));
			typedef BOOL (*SetProcessDPIAwareFunc)();
			SetProcessDPIAwareFunc setDPIAware =
				(SetProcessDPIAwareFunc)GetProcAddress(hUser32, "SetProcessDPIAware");
			if (setDPIAware)
				setDPIAware();
			FreeLibrary(hUser32);

			DoPropertySheet(hwndCPl);
			return 0;
		}
		break;
	}
	return 0;
}