#include "StdAfx.h"
#include <shlobj.h>   // for IShellWindows
#include <olectl.h>
#include <tchar.h>
#include "PropSheet.h"
#include "Resource.h"
#include "Hyperlinks.h"
#include "Service.h"
#include "../Common/Settings.h"

void DisplayError(HWND hwnd, DWORD dwError)
{
	LPTSTR pszMessage;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwError, 0, (LPTSTR)&pszMessage, 0, NULL))
	{
		TCHAR szTitle[256];
		LoadString(g_hInstance, IDS_NAME, szTitle, 256);
		MessageBox(hwnd, pszMessage, szTitle, MB_OK|MB_ICONSTOP);
		LocalFree(pszMessage);
	}
}

INT_PTR CALLBACK DisplayProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		{
			// the property sheet automatically sets its large icon (and it doesn't seem to be changeable),
			// but it doesn't set its small icon, so we'll do it here
			HICON hSmallIcon = (HICON)LoadImage(g_hInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON,
					GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
			SendMessage(GetParent(hwndDlg), WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);

			// load the drive type options
			int DriveTypes = LoadScanDriveTypes();
			CheckDlgButton(hwndDlg, IDC_DISPLAY_LOCAL, DriveTypes & SCANDRIVETYPE_LOCAL ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hwndDlg, IDC_DISPLAY_CD, DriveTypes & SCANDRIVETYPE_CD ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hwndDlg, IDC_DISPLAY_REMOVABLE, DriveTypes & SCANDRIVETYPE_REMOVABLE ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hwndDlg, IDC_DISPLAY_NETWORK, DriveTypes & SCANDRIVETYPE_NETWORK ? BST_CHECKED : BST_UNCHECKED);
			return TRUE;
		}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_DISPLAY_LOCAL:
		case IDC_DISPLAY_CD:
		case IDC_DISPLAY_REMOVABLE:
		case IDC_DISPLAY_NETWORK:
			PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
			break;
		}
		break;
	case WM_NOTIFY:
		{
			LPNMHDR pnmhdr = (LPNMHDR)lParam;
			switch (pnmhdr->code)
			{
			case PSN_APPLY:
				// read the settings and write them to the registry
				int DriveTypes = 0;
				if (IsDlgButtonChecked(hwndDlg, IDC_DISPLAY_LOCAL) == BST_CHECKED)
					DriveTypes |= SCANDRIVETYPE_LOCAL;
				if (IsDlgButtonChecked(hwndDlg, IDC_DISPLAY_CD) == BST_CHECKED)
					DriveTypes |= SCANDRIVETYPE_CD;
				if (IsDlgButtonChecked(hwndDlg, IDC_DISPLAY_REMOVABLE) == BST_CHECKED)
					DriveTypes |= SCANDRIVETYPE_REMOVABLE;
				if (IsDlgButtonChecked(hwndDlg, IDC_DISPLAY_NETWORK) == BST_CHECKED)
					DriveTypes |= SCANDRIVETYPE_NETWORK;

				// Saving drive types requires writing to an administrator's registry location and controlling a service.
				// So only do this if the setting has actually changed.
				if (LoadScanDriveTypes() != DriveTypes)
				{
					DWORD dwError = SaveScanDriveTypes(DriveTypes);
					if (!dwError)
						dwError = ModifyService(MS_PARAMCHANGE);
					if (dwError)
					{
						DisplayError(hwndDlg, dwError);
						SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_INVALID);
					}
				}

				return TRUE;
			}
			break;
		}
	}
	return FALSE;
}

typedef struct
{
	UINT nNameID;
	BOOL bEnableStart;
	BOOL bEnableStop;
	BOOL bEnablePause;
	BOOL bEnableContinue;
} SERVER_STATE;

SERVER_STATE g_ServerStateLookup[] =
{
	{IDS_SERVICE_UNKNOWN, FALSE, FALSE, FALSE, FALSE},
	{IDS_SERVICE_STOPPED, TRUE, FALSE, FALSE, FALSE},
	{IDS_SERVICE_START_PENDING, FALSE, TRUE, FALSE, FALSE},
	{IDS_SERVICE_STOP_PENDING, FALSE, FALSE, FALSE, FALSE},
	{IDS_SERVICE_RUNNING, FALSE, TRUE, TRUE, FALSE},
	{IDS_SERVICE_CONTINUE_PENDING, FALSE, FALSE, FALSE, FALSE},
	{IDS_SERVICE_PAUSE_PENDING, FALSE, FALSE, FALSE, FALSE},
	{IDS_SERVICE_PAUSED, FALSE, TRUE, FALSE, TRUE}
};

void UpdateServiceStatus(HWND hwndDlg)
{
	DWORD dwStatus = GetServiceStatus();

	TCHAR szStatus[256];
	LoadString(g_hInstance, g_ServerStateLookup[dwStatus].nNameID, szStatus, countof(szStatus));
	SetDlgItemText(hwndDlg, IDC_SERVICE_STATUS, szStatus);
	EnableWindow(GetDlgItem(hwndDlg, IDC_SERVICE_START), g_ServerStateLookup[dwStatus].bEnableStart);
	EnableWindow(GetDlgItem(hwndDlg, IDC_SERVICE_STOP), g_ServerStateLookup[dwStatus].bEnableStop);
	EnableWindow(GetDlgItem(hwndDlg, IDC_SERVICE_PAUSE), g_ServerStateLookup[dwStatus].bEnablePause);
	EnableWindow(GetDlgItem(hwndDlg, IDC_SERVICE_RESUME), g_ServerStateLookup[dwStatus].bEnableContinue);
}

void ModifyServiceAndShowResult(HWND hwndDlg, MODIFY_SERVICE ms)
{
	DWORD dwError = ModifyService(ms);
	if (dwError)
		DisplayError(hwndDlg, dwError);
	UpdateServiceStatus(hwndDlg);
}


void SetBiggerFont(HWND hwndCtrl)
{
	HFONT hFont = (HFONT)SendMessage(hwndCtrl, WM_GETFONT, 0, 0);
	if (hFont != NULL)
	{
		LOGFONT lf;
		GetObject(hFont, sizeof(LOGFONT), &lf);
		lf.lfHeight = (lf.lfHeight * 3) / 2;
		lf.lfWidth = (lf.lfWidth * 3) / 2;
		HFONT hBigFont = CreateFontIndirect(&lf);
		if (hBigFont != NULL)
		{
			SendMessage(hwndCtrl, WM_SETFONT, (WPARAM)hBigFont, TRUE);
		}
	}
}

void ReleaseBiggerFont(HWND hwndCtrl)
{
	HFONT hFont = (HFONT)SendMessage(hwndCtrl, WM_GETFONT, 0, 0);
	if (hFont != NULL)
	{
		DeleteObject(hFont);
	}
}

INT_PTR CALLBACK ServiceProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		{
			// set the info icon
			HICON hIcon = LoadIcon(NULL, IDI_INFORMATION);
			SendMessage(GetDlgItem(hwndDlg, IDC_SERVICE_INFOICON), STM_SETICON, (WPARAM)hIcon, 0);
			return TRUE;
		}

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_SERVICE_START:
			ModifyServiceAndShowResult(hwndDlg, MS_START);
			break;
		case IDC_SERVICE_STOP:
			ModifyServiceAndShowResult(hwndDlg, MS_STOP);
			break;
		case IDC_SERVICE_PAUSE:
			ModifyServiceAndShowResult(hwndDlg, MS_PAUSE);
			break;
		case IDC_SERVICE_RESUME:
			ModifyServiceAndShowResult(hwndDlg, MS_CONTINUE);
			break;
		}
		break;

	case WM_NOTIFY:
		{
			LPNMHDR pnmhdr = (LPNMHDR)lParam;
			switch (pnmhdr->code)
			{
			case PSN_SETACTIVE:
				UpdateServiceStatus(hwndDlg);
				SetTimer(hwndDlg, 1, 1500, NULL);
				break;
			case PSN_KILLACTIVE:
				KillTimer(hwndDlg, 1);
				break;
			}
			break;
		}

	case WM_TIMER:
		if (wParam == 1)
			UpdateServiceStatus(hwndDlg);
	}

	return FALSE;
}

INT_PTR CALLBACK CreditsProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		{
			// let's use the biggest and best icon available, instead of the default 32 pixel size
			HICON hIcon = (HICON)LoadImage(g_hInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 48, 48, 0);
			SendDlgItemMessage(hwndDlg, IDC_FOLDERSIZEICON, STM_SETICON, (WPARAM)hIcon, 0);

			SetDlgItemText(hwndDlg, IDC_TRANSLATORS,
				TEXT("Bruce Pimenta (German)\n")
				TEXT("Calle (Swedish)\n")
				TEXT("Frederik Uyttersprot & Huub van Dooren (Dutch)\n")
				TEXT("J.J.Ghisa (Italian)\n")
				TEXT("Josep Lladonosa Capell (Catalan)\n")
				TEXT("Keblo (Japanese)\n")
				TEXT("Wayland Tai (Chinese)\n")
				TEXT("Limerick Kepler (French)\n")
				TEXT("Miguel Maratá (Portuguese)\n")
				TEXT("Игорь Петрович (Russian)\n")
				TEXT("Piotr Kwiliński (Polish)\n")
				TEXT("Victor Pereyra (Spanish)\n")
				TEXT("Tamás Dávid (Hungarian)")
			);
			//     LTEXT           "Bruce Pimenta (German)\nCalle (Swedish)\nFrederik Uyttersprot & Huub van Dooren (Dutch)\nJ.J.Ghisa (Italian)\nJosep Lladonosa Capell (Catalan)\nKeblo (Japanese)\nWayland Tai (Chinese)\nLimerick Kepler (French)\nMiguel Maratá (Portuguese)\nИгорь Петрович (Russian)\nPiotr Kwiliński (Polish)\nVictor Pereyra (Spanish)\n",

			return TRUE;
		}
	case WM_COMMAND:
		EndDialog(hwndDlg, LOWORD(wParam));
		return TRUE;
	}
	return FALSE;
}

INT_PTR CALLBACK AboutProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		{
			SetBiggerFont(GetDlgItem(hwndDlg, IDC_TITLE));

			ConvertStaticToHyperlink(hwndDlg, IDC_TITLE);
			ConvertStaticToHyperlink(hwndDlg, IDC_HYPER_GPL);
			ConvertStaticToHyperlink(hwndDlg, IDC_HYPER_GNU);
			ConvertStaticToHyperlink(hwndDlg, IDC_PIC_BRIO);

			return TRUE;
		}

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_TITLE:
			ShellExecute(hwndDlg, TEXT("open"), TEXT("http://foldersize.sourceforge.net"), NULL, NULL, SW_SHOWNORMAL);
			return TRUE;
		case IDC_CREDITS:
			DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_CREDITS), hwndDlg, CreditsProc);
			return TRUE;
		case IDC_HYPER_GPL:
			ShellExecute(hwndDlg, TEXT("open"), TEXT("http://www.gnu.org/copyleft/gpl.html"), NULL, NULL, SW_SHOWNORMAL);
			return TRUE;
		case IDC_HYPER_GNU:
			ShellExecute(hwndDlg, TEXT("open"), TEXT("http://www.gnu.org"), NULL, NULL, SW_SHOWNORMAL);
			return TRUE;
		case IDC_PIC_BRIO:
			ShellExecute(hwndDlg, TEXT("open"), TEXT("mailto:brio1337@users.sourceforge.net"), NULL, NULL, SW_SHOWNORMAL);
			return TRUE;
		}
		break;

	case WM_DESTROY:
		{
			ReleaseBiggerFont(GetDlgItem(hwndDlg, IDC_TITLE));
			return TRUE;
		}
	}
	return FALSE;
}


void DoPropertySheet(HWND hwndParent)
{
	PROPSHEETHEADER psh;
	PROPSHEETPAGE psp[3];
	int i=0;

	psp[i].dwSize = sizeof(PROPSHEETPAGE);
	psp[i].dwFlags = PSP_DEFAULT;
	psp[i].hInstance = g_hInstance;
	psp[i].pszTemplate = MAKEINTRESOURCE(IDD_DISPLAY);
	psp[i].pfnDlgProc = DisplayProc;
	psp[i].lParam = 0;
	i++;

	psp[i].dwSize = sizeof(PROPSHEETPAGE);
	psp[i].dwFlags = PSP_DEFAULT;
	psp[i].hInstance = g_hInstance;
	psp[i].pszTemplate = MAKEINTRESOURCE(IDD_SERVICE);
	psp[i].pfnDlgProc = ServiceProc;
	psp[i].lParam = 0;
	i++;

	psp[i].dwSize = sizeof(PROPSHEETPAGE);
	psp[i].dwFlags = PSP_DEFAULT;
	psp[i].hInstance = g_hInstance;
	psp[i].pszTemplate = MAKEINTRESOURCE(IDD_ABOUT);
	psp[i].pfnDlgProc = AboutProc;
	psp[i].lParam = 0;
	i++;

	psh.dwSize = sizeof(PROPSHEETHEADER);
	psh.dwFlags = PSH_NOCONTEXTHELP | PSH_PROPSHEETPAGE | PSH_PROPTITLE;
	psh.hwndParent = hwndParent;
	psh.hInstance = g_hInstance;
	psh.pszCaption = MAKEINTRESOURCE(IDS_NAME);
	psh.nPages = i;
	psh.nStartPage = 0;
	psh.ppsp = psp;

	PropertySheet(&psh);
}