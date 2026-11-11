#include "StdAfx.h"
#include <shlobj.h>   // for IShellWindows
#include <olectl.h>
#include <tchar.h>
#include "PropSheet.h"
#include "Resource.h"
#include "Hyperlinks.h"
#include "Service.h"
#include "../Common/Settings.h"
#include "../Common/Language.h"

// Pairs of a control and its text, to translate a dialog (see TranslateDialog).
struct DialogText
{
	int nCtrlID;
	UINT nStrID;
};
#define countof_texts(a) (sizeof(a) / sizeof((a)[0]))

// Replaces the English texts of a dialog's controls with their translation. The dialogs have a
// single layout for all languages (FolderSizeCpl.rc); only the texts change.
static void TranslateDialog(HWND hwndDlg, const DialogText* pTexts, size_t nTexts)
{
	for (size_t i = 0; i < nTexts; i++)
		SetDlgItemText(hwndDlg, pTexts[i].nCtrlID, Tr(pTexts[i].nStrID).c_str());
}

static const DialogText g_GeneralTexts[] =
{
	{ IDC_DRIVE_TYPES_GROUP, STR_GENERAL_DRIVE_TYPES },
	{ IDC_DISPLAY_LOCAL, STR_GENERAL_DRIVE_HARD },
	{ IDC_DISPLAY_REMOVABLE, STR_GENERAL_DRIVE_REMOVABLE },
	{ IDC_DISPLAY_CD, STR_GENERAL_DRIVE_CD },
	{ IDC_DISPLAY_NETWORK, STR_GENERAL_DRIVE_NETWORK },
	{ IDC_DEFAULT_SORT_GROUP, STR_GENERAL_DEFAULT_SORT },
	{ IDC_SORT_NAME, STR_SORT_NAME },
	{ IDC_SORT_NAME_MIXED, STR_SORT_NAME_MIXED },
	{ IDC_SORT_SIZE, STR_SORT_SIZE },
	{ IDC_LANGUAGE_GROUP, STR_GENERAL_LANGUAGE },
};

static const DialogText g_ServiceTexts[] =
{
	{ IDC_SERVICE_STATUS_LABEL, STR_SERVICE_STATUS_LABEL },
	{ IDC_SERVICE_START, STR_SERVICE_START },
	{ IDC_SERVICE_STOP, STR_SERVICE_STOP },
	{ IDC_SERVICE_PAUSE, STR_SERVICE_PAUSE },
	{ IDC_SERVICE_RESUME, STR_SERVICE_RESUME },
	{ IDC_SERVICE_START_INFO, STR_SERVICE_START_INFO },
	{ IDC_SERVICE_STOP_INFO, STR_SERVICE_STOP_INFO },
	{ IDC_SERVICE_PAUSE_INFO, STR_SERVICE_PAUSE_INFO },
	{ IDC_SERVICE_RESUME_INFO, STR_SERVICE_RESUME_INFO },
	{ IDC_SERVICE_ADMIN_NOTE, STR_SERVICE_ADMIN_NOTE },
};

static const DialogText g_AboutTexts[] =
{
	{ IDC_ABOUT_ORIGINAL, STR_ABOUT_ORIGINAL },
	{ IDC_CREDITS, STR_ABOUT_CREDITS },
	{ IDC_ABOUT_FORK, STR_ABOUT_FORK },
	{ IDC_LICENSE_GROUP, STR_ABOUT_LICENSE },
	{ IDC_HYPER_GPL, STR_ABOUT_GPL },
	{ IDC_HYPER_GNU, STR_ABOUT_GNU },
};

static const DialogText g_CreditsTexts[] =
{
	{ IDC_CREDITS_INTRO, STR_CREDITS_INTRO },
	{ IDC_CREDITS_TRANSLATION, STR_CREDITS_TRANSLATION },
	{ IDC_CREDITS_GRAPHIC, STR_CREDITS_GRAPHIC },
	{ IDC_CREDITS_CODE, STR_CREDITS_CODE },
	{ IDOK, STR_COMMON_OK },
};

// Shows the shield on a button, for actions that need administrator rights.
// (BCM_SETSHIELD is only declared for _WIN32_WINNT 0x0600 and later, and the panel uses 0x0500.)
#ifndef BCM_SETSHIELD
#define BCM_SETSHIELD 0x160C
#endif

// Whether the panel runs with administrator rights (elevated), so it can control the service and
// change the drive types to scan itself, without asking Windows for approval.
static bool IsElevated()
{
	bool bElevated = false;
	HANDLE hToken;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
	{
		TOKEN_ELEVATION elevation;
		DWORD cb = sizeof(elevation);
		if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cb))
			bElevated = elevation.TokenIsElevated != 0;
		CloseHandle(hToken);
	}
	return bElevated;
}

// Runs a command of the panel (see ElevatedCommandW in FolderSizeCpl.cpp) with administrator
// rights: Windows asks the user for approval (UAC), then rundll32.exe loads the panel in a new
// process that runs the command. Waits for it to finish. Returns the error of the command, 0 if it
// succeeded, or ERROR_CANCELLED if the user declined.
static DWORD RunElevated(HWND hwnd, LPCWSTR pszCommand)
{
	WCHAR szPanel[MAX_PATH];
	if (!GetModuleFileNameW(g_hInstance, szPanel, MAX_PATH))
		return GetLastError();
	WCHAR szRundll[MAX_PATH];
	if (!GetSystemDirectoryW(szRundll, MAX_PATH) || !PathAppendW(szRundll, L"rundll32.exe"))
		return ERROR_PATH_NOT_FOUND;
	WCHAR szParameters[MAX_PATH + 64];
	wsprintfW(szParameters, L"\"%s\",ElevatedCommand %s", szPanel, pszCommand);

	SHELLEXECUTEINFOW sei = {0};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	sei.hwnd = hwnd;
	sei.lpVerb = L"runas";
	sei.lpFile = szRundll;
	sei.lpParameters = szParameters;
	sei.nShow = SW_HIDE;
	if (!ShellExecuteExW(&sei))
		return GetLastError();

	DWORD dwError = ERROR_GEN_FAILURE;
	if (sei.hProcess)
	{
		HCURSOR hPrevCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
		WaitForSingleObject(sei.hProcess, INFINITE);
		SetCursor(hPrevCursor);
		GetExitCodeProcess(sei.hProcess, &dwError);
		CloseHandle(sei.hProcess);
	}
	return dwError;
}

void DisplayError(HWND hwnd, DWORD dwError)
{
	LPTSTR pszMessage;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwError, 0, (LPTSTR)&pszMessage, 0, NULL))
	{
		MessageBox(hwnd, pszMessage, Tr(STR_APP_NAME).c_str(), MB_OK|MB_ICONSTOP);
		LocalFree(pszMessage);
	}
}

INT_PTR CALLBACK GeneralProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
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

			TranslateDialog(hwndDlg, g_GeneralTexts, countof_texts(g_GeneralTexts));

			// languages: the language of Windows first, then each language by its own name
			HWND hwndLanguage = GetDlgItem(hwndDlg, IDC_LANGUAGE);
			SendMessage(hwndLanguage, CB_ADDSTRING, 0, (LPARAM)Tr(STR_GENERAL_LANGUAGE_SYSTEM).c_str());
			std::wstring language = LoadLanguageSetting();
			int selected = 0;
			for (int i = 0; i < GetLanguageCount(); i++)
			{
				SendMessage(hwndLanguage, CB_ADDSTRING, 0, (LPARAM)GetLanguageInfo(i).name);
				if (language == GetLanguageInfo(i).code)
					selected = i + 1;
			}
			SendMessage(hwndLanguage, CB_SETCURSEL, selected, 0);

			// load the drive type options
			int DriveTypes = LoadScanDriveTypes();
			CheckDlgButton(hwndDlg, IDC_DISPLAY_LOCAL, DriveTypes & SCANDRIVETYPE_LOCAL ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hwndDlg, IDC_DISPLAY_CD, DriveTypes & SCANDRIVETYPE_CD ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hwndDlg, IDC_DISPLAY_REMOVABLE, DriveTypes & SCANDRIVETYPE_REMOVABLE ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hwndDlg, IDC_DISPLAY_NETWORK, DriveTypes & SCANDRIVETYPE_NETWORK ? BST_CHECKED : BST_UNCHECKED);

			// load the default sort of the window
			int Sort = LoadDefaultSort();
			CheckRadioButton(hwndDlg, IDC_SORT_NAME, IDC_SORT_SIZE,
				Sort == SORT_SIZE ? IDC_SORT_SIZE : Sort == SORT_NAME_MIXED ? IDC_SORT_NAME_MIXED : IDC_SORT_NAME);
			return TRUE;
		}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_DISPLAY_LOCAL:
		case IDC_DISPLAY_CD:
		case IDC_DISPLAY_REMOVABLE:
		case IDC_DISPLAY_NETWORK:
		case IDC_SORT_NAME:
		case IDC_SORT_NAME_MIXED:
		case IDC_SORT_SIZE:
			PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
			break;
		case IDC_LANGUAGE:
			if (HIWORD(wParam) == CBN_SELCHANGE)
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
				if (IsDlgButtonChecked(hwndDlg, IDC_SORT_SIZE) == BST_CHECKED)
					SaveDefaultSort(SORT_SIZE);
				else if (IsDlgButtonChecked(hwndDlg, IDC_SORT_NAME_MIXED) == BST_CHECKED)
					SaveDefaultSort(SORT_NAME_MIXED);
				else
					SaveDefaultSort(SORT_NAME);

				// language: the first choice is the language of Windows (empty setting)
				{
					int selected = (int)SendDlgItemMessage(hwndDlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
					SaveLanguageSetting(selected > 0 ? GetLanguageInfo(selected - 1).code : L"");
				}

				// Tell the running FolderSize.exe, so its windows pick up the new language. The
				// settings panel itself shows the new language the next time it is opened.
				{
					HWND hwndMain = FindWindow(MAIN_WINDOW_CLASS, NULL);
					if (hwndMain)
						PostMessage(hwndMain, RegisterWindowMessage(SETTINGS_CHANGED_MESSAGE), 0, 0);
				}

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
				// So only do this if the setting has actually changed. Without administrator rights,
				// Windows asks for approval first.
				if (LoadScanDriveTypes() != DriveTypes)
				{
					DWORD dwError;
					if (IsElevated())
					{
						dwError = SaveDriveTypesAndTellService(DriveTypes);
					}
					else
					{
						WCHAR szCommand[32];
						wsprintfW(szCommand, L"drivetypes %d", DriveTypes);
						dwError = RunElevated(hwndDlg, szCommand);
					}
					if (dwError)
					{
						if (dwError != ERROR_CANCELLED)
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
	{STR_SERVICE_STATE_UNKNOWN, FALSE, FALSE, FALSE, FALSE},
	{STR_SERVICE_STATE_STOPPED, TRUE, FALSE, FALSE, FALSE},
	{STR_SERVICE_STATE_STARTING, FALSE, TRUE, FALSE, FALSE},
	{STR_SERVICE_STATE_STOPPING, FALSE, FALSE, FALSE, FALSE},
	{STR_SERVICE_STATE_RUNNING, FALSE, TRUE, TRUE, FALSE},
	{STR_SERVICE_STATE_RESUMING, FALSE, FALSE, FALSE, FALSE},
	{STR_SERVICE_STATE_PAUSING, FALSE, FALSE, FALSE, FALSE},
	{STR_SERVICE_STATE_PAUSED, FALSE, TRUE, FALSE, TRUE}
};

void UpdateServiceStatus(HWND hwndDlg)
{
	DWORD dwStatus = GetServiceStatus();

	SetDlgItemText(hwndDlg, IDC_SERVICE_STATUS, Tr(g_ServerStateLookup[dwStatus].nNameID).c_str());
	EnableWindow(GetDlgItem(hwndDlg, IDC_SERVICE_START), g_ServerStateLookup[dwStatus].bEnableStart);
	EnableWindow(GetDlgItem(hwndDlg, IDC_SERVICE_STOP), g_ServerStateLookup[dwStatus].bEnableStop);
	EnableWindow(GetDlgItem(hwndDlg, IDC_SERVICE_PAUSE), g_ServerStateLookup[dwStatus].bEnablePause);
	EnableWindow(GetDlgItem(hwndDlg, IDC_SERVICE_RESUME), g_ServerStateLookup[dwStatus].bEnableContinue);
}

// Commands of ElevatedCommandW for each MODIFY_SERVICE value (MS_PARAMCHANGE is never run alone).
static LPCWSTR g_ServiceCommands[MS_MAX] = { L"start", L"stop", L"pause", L"continue", L"" };

void ModifyServiceAndShowResult(HWND hwndDlg, MODIFY_SERVICE ms)
{
	// Without administrator rights, Windows asks for approval first (the buttons show the shield).
	DWORD dwError = IsElevated() ? ModifyService(ms) : RunElevated(hwndDlg, g_ServiceCommands[ms]);
	if (dwError && dwError != ERROR_CANCELLED)
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
			TranslateDialog(hwndDlg, g_ServiceTexts, countof_texts(g_ServiceTexts));

			// the buttons need administrator rights: show the shield when Windows will ask for approval
			if (!IsElevated())
			{
				static const int buttons[] = { IDC_SERVICE_START, IDC_SERVICE_STOP, IDC_SERVICE_PAUSE, IDC_SERVICE_RESUME };
				for (int i = 0; i < countof(buttons); i++)
					SendDlgItemMessage(hwndDlg, buttons[i], BCM_SETSHIELD, 0, TRUE);
			}
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
			TranslateDialog(hwndDlg, g_CreditsTexts, countof_texts(g_CreditsTexts));
			SetWindowText(hwndDlg, Tr(STR_CREDITS_TITLE).c_str());

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
			TranslateDialog(hwndDlg, g_AboutTexts, countof_texts(g_AboutTexts));
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
			ShellExecute(hwndDlg, TEXT("open"), TEXT("https://github.com/laurentlasalle/foldersize"), NULL, NULL, SW_SHOWNORMAL);
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

	// translated texts; they must stay valid until PropertySheet returns
	std::wstring titleGeneral = Tr(STR_TAB_GENERAL);
	std::wstring titleService = Tr(STR_TAB_SERVICE);
	std::wstring titleAbout = Tr(STR_TAB_ABOUT);
	std::wstring caption = Tr(STR_APP_NAME);

	psp[i].dwSize = sizeof(PROPSHEETPAGE);
	psp[i].dwFlags = PSP_USETITLE;
	psp[i].hInstance = g_hInstance;
	psp[i].pszTemplate = MAKEINTRESOURCE(IDD_GENERAL);
	psp[i].pfnDlgProc = GeneralProc;
	psp[i].pszTitle = titleGeneral.c_str();
	psp[i].lParam = 0;
	i++;

	psp[i].dwSize = sizeof(PROPSHEETPAGE);
	psp[i].dwFlags = PSP_USETITLE;
	psp[i].hInstance = g_hInstance;
	psp[i].pszTemplate = MAKEINTRESOURCE(IDD_SERVICE);
	psp[i].pfnDlgProc = ServiceProc;
	psp[i].pszTitle = titleService.c_str();
	psp[i].lParam = 0;
	i++;

	psp[i].dwSize = sizeof(PROPSHEETPAGE);
	psp[i].dwFlags = PSP_USETITLE;
	psp[i].hInstance = g_hInstance;
	psp[i].pszTemplate = MAKEINTRESOURCE(IDD_ABOUT);
	psp[i].pfnDlgProc = AboutProc;
	psp[i].pszTitle = titleAbout.c_str();
	psp[i].lParam = 0;
	i++;

	psh.dwSize = sizeof(PROPSHEETHEADER);
	psh.dwFlags = PSH_NOCONTEXTHELP | PSH_PROPSHEETPAGE | PSH_PROPTITLE;
	psh.hwndParent = hwndParent;
	psh.hInstance = g_hInstance;
	psh.pszCaption = caption.c_str();
	psh.nPages = i;
	psh.nStartPage = 0;
	psh.ppsp = psp;

	PropertySheet(&psh);
}