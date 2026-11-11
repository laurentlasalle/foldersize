#include "StdAfx.h"
#include "resource.h"

#define WM_NOTIFYICON (WM_APP + 1)
#define WM_KILLWINDOW (WM_APP + 2)

#define APP_NAME _T("Folder Size")
#define SHELL_WINDOW_CLASS_NAME _T("FolderSizeShellWindow")
#define REG_KEY_PATH _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run")

HWND g_hwndMain = NULL;


__inline ULONGLONG MakeULongLong(DWORD dwHigh, DWORD dwLow)
{
	return ((ULONGLONG)dwHigh << 32) | dwLow;
}

static bool GetLogicalFileSize(LPCTSTR pFileName, ULONGLONG& nSize)
{
	// GetFileAttributesEx and CreateFile fail if the file is open by the system (for example, the swap file).
	// GetFileAttributesEx also seems to fail for very large files on Novell Netware networks.
	// FindFirstFile seems to be the only reliable way to get the size.
	WIN32_FIND_DATA FindData;
	HANDLE hFindFile = FindFirstFile(pFileName, &FindData);
	if (hFindFile == INVALID_HANDLE_VALUE)
		return false;
	FindClose(hFindFile);
	nSize = MakeULongLong(FindData.nFileSizeHigh, FindData.nFileSizeLow);
	return true;
}


class ListItem
{
public:
	ListItem(LPCTSTR filename, LPITEMIDLIST pidl, FOLDERINFO2& size, int index, int icon, LPCTSTR name) : m_pidl(pidl), m_size(size), m_index(index), m_icon(icon)
	{
		lstrcpyn(m_filename, filename, MAX_PATH);
		lstrcpyn(m_name, name, MAX_PATH);
	}
	~ListItem()
	{
		CoTaskMemFree(m_pidl);
	}

	LPCTSTR GetFileName() { return m_filename; }
	LPITEMIDLIST GetPidl() { return m_pidl; }
	FOLDERINFO2& GetSize() { return m_size; }
	int GetIndex() { return m_index; }
	int GetIcon() { return m_icon; }
	LPCTSTR GetName() { return m_name; }
	
	void UpdateSize(FOLDERINFO2& size) { m_size = size; }

private:
	LPITEMIDLIST m_pidl;
	FOLDERINFO2 m_size;
	int m_index;
	int m_icon;
	TCHAR m_filename[MAX_PATH];
	TCHAR m_name[MAX_PATH];
};

class RefreshItem
{
public:
	RefreshItem(LPCTSTR filename, FOLDERINFO2& size) : m_size(size) { lstrcpyn(m_filename, filename, MAX_PATH); }

	LPCTSTR GetFileName() { return m_filename; }
	FOLDERINFO2& GetSize() { return m_size; }

private:
	FOLDERINFO2 m_size;
	TCHAR m_filename[MAX_PATH];
};

#define WM_NEWITEMS      (WM_APP + 1)
#define WM_REFRESHITEMS  (WM_APP + 2)


class FolderViewScanner
{
public:
	FolderViewScanner(IWebBrowser2* pWebBrowser, HWND hWndPost);
	~FolderViewScanner();

	std::vector<ListItem*> GetNewItems();
	std::vector<RefreshItem*> GetRefreshItems();

	void Quit();

private:
	static unsigned int __stdcall ThreadProc(void* arg);
	DWORD ThreadProc();

	// The web browser object of the Explorer tab, marshaled for use in the scanner thread.
	IStream* m_pBrowserStream;
	HWND m_hWndPost;

	HANDLE m_hThread;
	HANDLE m_hQuitEvent;

	CRITICAL_SECTION m_cs;
	std::vector<ListItem*> m_newItems;
	std::vector<RefreshItem*> m_refreshItems;
};

FolderViewScanner::FolderViewScanner(IWebBrowser2* pWebBrowser, HWND hWndPost) : m_pBrowserStream(NULL), m_hWndPost(hWndPost), m_hThread(NULL), m_hQuitEvent(NULL)
{
	InitializeCriticalSection(&m_cs);
	m_hQuitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	// Pass the web browser object itself to the scanner thread. It used to be looked up again
	// in the thread by the HWND of the Explorer window, but with File Explorer tabs every tab of
	// a window has the same HWND, so the lookup always found the first tab.
	if (SUCCEEDED(CoMarshalInterThreadInterfaceInStream(IID_IWebBrowser2, pWebBrowser, &m_pBrowserStream)))
	{
		unsigned int threadId;
		m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, &threadId);
		if (!m_hThread)
		{
			m_pBrowserStream->Release();
			m_pBrowserStream = NULL;
		}
	}
}

FolderViewScanner::~FolderViewScanner()
{
	// the thread is deleting itself, so don't wait for it
	if (m_hThread)
		CloseHandle(m_hThread);

	if (m_hQuitEvent)
		CloseHandle(m_hQuitEvent);

	DeleteCriticalSection(&m_cs);
}

std::vector<ListItem*> FolderViewScanner::GetNewItems()
{
	EnterCriticalSection(&m_cs);
	std::vector<ListItem*> items = m_newItems;
	m_newItems.clear();
	LeaveCriticalSection(&m_cs);
	return items;
}

std::vector<RefreshItem*> FolderViewScanner::GetRefreshItems()
{
	EnterCriticalSection(&m_cs);
	std::vector<RefreshItem*> items = m_refreshItems;
	m_refreshItems.clear();
	LeaveCriticalSection(&m_cs);
	return items;
}

void FolderViewScanner::Quit()
{
	SetEvent(m_hQuitEvent);
}

DWORD FolderViewScanner::ThreadProc()
{
	// CoGetInterfaceAndReleaseStream releases the stream, whether it succeeds or not.
	CComPtr<IWebBrowser2> pWebBrowser;
	HRESULT hr = CoGetInterfaceAndReleaseStream(m_pBrowserStream, IID_IWebBrowser2, (void**)&pWebBrowser);
	m_pBrowserStream = NULL;
	if (FAILED(hr))
		return hr;

	// keep checking the state of the quit event

	TCHAR szFolder[MAX_PATH];
	szFolder[0] = '\0';

	IServiceProvider* psp;
	if (SUCCEEDED(pWebBrowser->QueryInterface(IID_IServiceProvider, (void**)&psp)))
	{
		IShellBrowser* psb;
		if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb)))
		{
			IShellView* psv;
			if (SUCCEEDED(psb->QueryActiveShellView(&psv)))
			{
				IFolderView* pfv;
				if (SUCCEEDED(psv->QueryInterface(IID_IFolderView, (void**)&pfv)))
				{
					IShellFolder2* psf2;
					if (SUCCEEDED(pfv->GetFolder(IID_IShellFolder2, (void**)&psf2)))
					{
						IPersistFolder2* ppf2;
						if (SUCCEEDED(psf2->QueryInterface(IID_IPersistFolder2, (void**)&ppf2)))
						{
							LPITEMIDLIST pFolderID;
							if (SUCCEEDED(ppf2->GetCurFolder(&pFolderID)))
							{
								SHGetPathFromIDList(pFolderID, szFolder);
							}
						}

						IEnumIDList* pEnum;
						if (SUCCEEDED(pfv->Items(SVGIO_FLAG_VIEWORDER, IID_IEnumIDList, (void**)&pEnum)))
						{
							int index = 0;
							LPITEMIDLIST pItemID;
							while (WaitForSingleObject(m_hQuitEvent, 0) == WAIT_TIMEOUT && pEnum->Next(1, &pItemID, NULL) == S_OK)
							{
								STRRET strret;
								if (SUCCEEDED(psf2->GetDisplayNameOf(pItemID, SHGDN_FORPARSING, &strret)))
								{
									LPTSTR pPath;
									if (SUCCEEDED(StrRetToStr(&strret, pItemID, &pPath)))
									{
										SHFILEINFO shfi;
										shfi.dwAttributes = SFGAO_FILESYSTEM | SFGAO_FOLDER;
										if (SHGetFileInfo(pPath, 0, &shfi, sizeof(shfi),
											SHGFI_ATTRIBUTES | SHGFI_ATTR_SPECIFIED | SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON)
											&& shfi.dwAttributes & SFGAO_FILESYSTEM)
										{
											FOLDERINFO2 fi;
											// Decide between folder and file from the file system attribute, not from
											// SFGAO_FOLDER: the shell also sets SFGAO_FOLDER on archives it can browse
											// (.zip, and on Windows 11 .7z, .rar, .tar and others). Those are files on
											// disk, which the service never scans, so asking it for their size returned
											// nothing and the size column stayed empty.
											DWORD dwFileAttributes = GetFileAttributes(pPath);
											bool bIsDirectory = (dwFileAttributes != INVALID_FILE_ATTRIBUTES)
												? (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
												: (shfi.dwAttributes & SFGAO_FOLDER) != 0;
											if (bIsDirectory)
											{
												GetInfoForFolder(pPath, fi);
											}
											else
											{
												GetLogicalFileSize(pPath, fi.nLogicalSize);
												fi.giff = GIFF_CLEAN;
											}

											// the ListItem takes ownership of the PIDL
											ListItem* pItem = new ListItem(PathFindFileName(pPath), pItemID, fi, index++, shfi.iIcon, shfi.szDisplayName);
											pItemID = NULL;

											EnterCriticalSection(&m_cs);
											if (m_newItems.empty())
												PostMessage(m_hWndPost, WM_NEWITEMS, 0, 0);
											m_newItems.push_back(pItem);
											LeaveCriticalSection(&m_cs);
										}
										CoTaskMemFree(pPath);
									}
								}
								CoTaskMemFree(pItemID);
							}
							pEnum->Release();
						}
						psf2->Release();
					}
					pfv->Release();
				}
				psv->Release();
			}
			psb->Release();
		}
		psp->Release();
	}

	if (!szFolder[0])
	{
		WaitForSingleObject(m_hQuitEvent, INFINITE);
		return 0;
	}

	// loop on a refresh timer

	while (WaitForSingleObject(m_hQuitEvent, 2000) == WAIT_TIMEOUT)
	{
		// connect to the server and get the folders to update
		// try twice to connect to the pipe
		HANDLE hPipe = CreateFile(TEXT("\\\\.\\pipe\\") PIPE_NAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

		if (hPipe == INVALID_HANDLE_VALUE)
		{
			if (GetLastError() == ERROR_PIPE_BUSY)
			{
				if (WaitNamedPipe(TEXT("\\\\.\\pipe\\") PIPE_NAME, 1000))
				{
					hPipe = CreateFile(TEXT("\\\\.\\pipe\\") PIPE_NAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
				}
			}
		}

		if (hPipe != INVALID_HANDLE_VALUE)
		{
			Strings strsFoldersBrowsed;
			strsFoldersBrowsed.insert(szFolder);
			if (WriteGetUpdatedFoldersRequest(hPipe, strsFoldersBrowsed))
			{
				Strings strsFoldersToUpdate;
				if (ReadStringList(hPipe, strsFoldersToUpdate))
				{
					for (Strings::const_iterator i = strsFoldersToUpdate.begin(); i != strsFoldersToUpdate.end(); i++)
					{
						FOLDERINFO2 fi;
						GetInfoForFolder(i->c_str(), fi);
						RefreshItem* pItem = new RefreshItem(PathFindFileName(i->c_str()), fi);
						EnterCriticalSection(&m_cs);
						if (m_refreshItems.empty())
							PostMessage(m_hWndPost, WM_REFRESHITEMS, 0, 0);
						m_refreshItems.push_back(pItem);
						LeaveCriticalSection(&m_cs);
					}
				}
			}
			CloseHandle(hPipe);
		}
	}

	return 0;
}

unsigned int __stdcall FolderViewScanner::ThreadProc(void* arg)
{
	CoInitialize(NULL);

	FolderViewScanner* p = (FolderViewScanner*)arg;
	unsigned int ret = p->ThreadProc();

	// The client calls Quit, and then forgets about this object.
	// Responsibility for destruction is left to the thread, so if it's stalled,
	// it can clean itself up when it's done.
	delete p;

	CoUninitialize();
	return ret;
}




static void FormatSize(ULONGLONG nSize, LPTSTR pszBuff, UINT uiBufSize)
{
	// Same format as the Size column of File Explorer (for example "2 572 455 KB").
	StrFormatKBSize(nSize, pszBuff, uiBufSize);
}

static bool GetFolderInfoToBuffer(LPCTSTR pszFolder, ULONGLONG FOLDERINFO2::* sizeMember, LPTSTR pszBuffer, DWORD cch)
{
	pszBuffer[0] = _T('\0');
	FOLDERINFO2 nSize;
	SetLastError(0);
	if (!GetInfoForFolder(pszFolder, nSize))
	{
#ifdef _DEBUG
		// FORMAT_MESSAGE_MAX_WIDTH_MASK converts newline chars in the message to spaces, which is better for single-line output
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS|FORMAT_MESSAGE_MAX_WIDTH_MASK,
			NULL, GetLastError(), 0, pszBuffer, cch, NULL);
#else
		return false;
#endif
	}
	else
	{
		FormatSize(nSize.*sizeMember, pszBuffer, cch);

		size_t len = wcslen(pszBuffer);
		if (len < cch - 1 && (nSize.giff == GIFF_DIRTY || nSize.giff == GIFF_SCANNING))
		{
			pszBuffer[len] = nSize.giff == GIFF_DIRTY ? L'~' : L'+';
			pszBuffer[len + 1] = L'\0';
		}
	}
	return true;
}



#define WM_UPDATETABS (WM_APP + 3)

// ---- Dark mode ----
// The window follows the "Choose your app mode" setting of Windows (Light or Dark), and switches
// live when it changes.

// True when Windows apps should use the dark theme.
static bool IsDarkModeEnabled()
{
	DWORD dwLight = 1, cb = sizeof(dwLight);
	if (RegGetValue(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
			_T("AppsUseLightTheme"), RRF_RT_REG_DWORD, NULL, &dwLight, &cb) != ERROR_SUCCESS)
		return false;
	return dwLight == 0;
}

// Popup menus (the tray icon menu) have no documented dark mode. These two functions, exported
// by uxtheme.dll by ordinal only, are what Windows itself and many programs use:
// SetPreferredAppMode(AllowDark) lets menus follow the app mode, and FlushMenuThemes makes
// them pick up a change. If they are missing, menus simply stay light.
enum PreferredAppMode { AppModeDefault = 0, AppModeAllowDark = 1 };
typedef PreferredAppMode (WINAPI *PFN_SetPreferredAppMode)(PreferredAppMode mode);
typedef void (WINAPI *PFN_FlushMenuThemes)();

static void AllowDarkMenus()
{
	HMODULE hUxTheme = GetModuleHandle(_T("uxtheme.dll"));
	if (!hUxTheme)
		hUxTheme = LoadLibraryEx(_T("uxtheme.dll"), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!hUxTheme)
		return;
	PFN_SetPreferredAppMode pfnSetPreferredAppMode = (PFN_SetPreferredAppMode)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
	PFN_FlushMenuThemes pfnFlushMenuThemes = (PFN_FlushMenuThemes)GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136));
	if (pfnSetPreferredAppMode)
		pfnSetPreferredAppMode(AppModeAllowDark);
	if (pfnFlushMenuThemes)
		pfnFlushMenuThemes();
}

// Colors of the list in dark mode, matching the dark File Explorer.
static const COLORREF DARK_BACKGROUND = RGB(0x19, 0x19, 0x19);
static const COLORREF DARK_TEXT = RGB(0xFF, 0xFF, 0xFF);

// Subclass of the list view: the column header sends its custom draw notifications to the
// list view, not to our window, so the list view is subclassed to give the header a light
// text color in dark mode (the dark header theme keeps dark text otherwise).
static LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	const bool* pbDark = (const bool*)dwRefData;
	if (uMsg == WM_NOTIFY && *pbDark)
	{
		LPNMHDR pnmh = (LPNMHDR)lParam;
		if (pnmh->hwndFrom == ListView_GetHeader(hwnd) && pnmh->code == NM_CUSTOMDRAW)
		{
			LPNMCUSTOMDRAW pcd = (LPNMCUSTOMDRAW)lParam;
			if (pcd->dwDrawStage == CDDS_PREPAINT)
				return CDRF_NOTIFYITEMDRAW;
			if (pcd->dwDrawStage == CDDS_ITEMPREPAINT)
			{
				SetTextColor(pcd->hdc, DARK_TEXT);
				return CDRF_DEFAULT;
			}
		}
	}
	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// Returns the window of the tab currently in front in a File Explorer window. Each tab has
// its own ShellTabWindowClass child window, and only the one in front is visible. On versions
// of Windows without tabs there is only one, which is then always the active one.
static HWND GetActiveTab(HWND hwndExplorer)
{
	HWND hwndFirst = FindWindowEx(hwndExplorer, NULL, _T("ShellTabWindowClass"), NULL);
	for (HWND hwnd = hwndFirst; hwnd; hwnd = FindWindowEx(hwndExplorer, hwnd, _T("ShellTabWindowClass"), NULL))
	{
		if (::IsWindowVisible(hwnd))
			return hwnd;
	}
	return hwndFirst;
}

// Returns the ShellTabWindowClass window that is hwnd or one of its parents, or NULL if there
// is none. When NULL, the window can't tell tabs apart and behaves as before tabs existed:
// it is always shown.
static HWND FindTabWindow(HWND hwnd)
{
	for (; hwnd; hwnd = ::GetParent(hwnd))
	{
		TCHAR szClass[32];
		if (GetClassName(hwnd, szClass, 32) && lstrcmp(szClass, _T("ShellTabWindowClass")) == 0)
			return hwnd;
	}
	return NULL;
}

class FSWindow :
	public CWindowImpl<FSWindow, CWindow, CWinTraits<WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX, WS_EX_TOOLWINDOW> >,
	public IDispEventSimpleImpl<1, FSWindow, &DIID_DWebBrowserEvents2>
{
public:
	FSWindow(IWebBrowser2* pWebBrowser);
	~FSWindow();

	DECLARE_WND_CLASS_EX(NULL, 0, COLOR_WINDOW);

BEGIN_MSG_MAP(FSWindow)
	MESSAGE_HANDLER(WM_CREATE, OnCreate)
	MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
	MESSAGE_HANDLER(WM_SIZE, OnSize)
	MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
	MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
	MESSAGE_HANDLER(WM_NEWITEMS, OnNewItems)
	MESSAGE_HANDLER(WM_REFRESHITEMS, OnRefreshItems)
	MESSAGE_HANDLER(WM_CLOSE, OnClose)
	NOTIFY_HANDLER(1, LVN_ITEMACTIVATE, OnItemActivate)
	NOTIFY_HANDLER(1, LVN_COLUMNCLICK, OnColumnClick)
	NOTIFY_HANDLER(1, LVN_DELETEALLITEMS, OnDeleteAllItems)
END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnNewItems(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnRefreshItems(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnItemActivate(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnColumnClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnDeleteAllItems(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);

static _ATL_FUNC_INFO NavigateCompleteInfo;
static _ATL_FUNC_INFO QuitInfo;
BEGIN_SINK_MAP(FSWindow)
	SINK_ENTRY_INFO(1, DIID_DWebBrowserEvents2, DISPID_NAVIGATECOMPLETE2, OnNavigateComplete, &NavigateCompleteInfo)
	SINK_ENTRY_INFO(1, DIID_DWebBrowserEvents2, DISPID_ONQUIT, OnQuit, &QuitInfo)
END_SINK_MAP()

	void __stdcall OnNavigateComplete(IDispatch *pDisp, VARIANT *URL);
	void __stdcall OnQuit();

	HWND GetExplorerWindow() const { return m_hwndExplorer; }
	bool IsTab(HWND hwndTab) const { return m_hwndTab == NULL || m_hwndTab == hwndTab; }
	void UpdateTabVisibility(HWND hwndActiveTab, const RECT* prcPrevious);

private:
	void CreateIfUnminimizedFolderView();
	void AdjustSizeForList();
	void UpdateFontForDpi();
	void ApplyTheme();

	HWND m_lv;
	HFONT m_hFont;
	HWND m_hwndExplorer;  // top-level File Explorer window (shared by all its tabs)
	HWND m_hwndTab;       // the tab's own window (ShellTabWindowClass), NULL if unknown
	bool m_bDark;         // dark mode currently applied (read by ListViewSubclassProc)
	CComPtr<IWebBrowser2> m_pWebBrowser;
	TCHAR m_szFolder[MAX_PATH * 4];
	FolderViewScanner* m_pScanner;
	std::map<std::wstring, ListItem*> m_nameMap;
};

FSWindow::FSWindow(IWebBrowser2* pWebBrowser) : m_lv(NULL), m_hFont(NULL), m_hwndExplorer(NULL), m_hwndTab(NULL), m_bDark(false), m_pWebBrowser(pWebBrowser), m_pScanner(NULL)
{
	DispEventAdvise(m_pWebBrowser);
	CreateIfUnminimizedFolderView();
}

FSWindow::~FSWindow()
{
	if (m_hWnd)
		DestroyWindow();
	if (m_pScanner)
		m_pScanner->Quit();
	DispEventUnadvise(m_pWebBrowser);
}

LRESULT FSWindow::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	RECT rc;
	GetClientRect(&rc);
	m_lv = CreateWindow(WC_LISTVIEW, NULL, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHAREIMAGELISTS | LVS_SINGLESEL,
		0, 0, rc.right-rc.left, rc.bottom-rc.top,
		m_hWnd, (HMENU)1, (HINSTANCE)GetWindowLongPtr(GWLP_HINSTANCE), NULL);
	if (!m_lv)
		return -1;

	ListView_SetExtendedListViewStyle(m_lv, LVS_EX_FULLROWSELECT | LVS_EX_AUTOSIZECOLUMNS | LVS_EX_DOUBLEBUFFER);

	SetWindowSubclass(m_lv, ListViewSubclassProc, 0, (DWORD_PTR)&m_bDark);
	ApplyTheme();
	UpdateFontForDpi();

	LVCOLUMN column;
	column.mask = LVCF_FMT | LVCF_TEXT;
	column.fmt = LVCFMT_LEFT;
	column.pszText = _T("Name");
	ListView_InsertColumn(m_lv, 0, &column);
	column.fmt = LVCFMT_RIGHT;
	column.pszText = _T("Size");
	ListView_InsertColumn(m_lv, 1, &column);

	SHFILEINFO shfi;
	HIMAGELIST himl = (HIMAGELIST)SHGetFileInfo(_T(""), FILE_ATTRIBUTE_DIRECTORY, &shfi, sizeof(shfi), SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
	if (himl == NULL)
		return -1;
	ListView_SetImageList(m_lv, himl, LVSIL_SMALL);

	AdjustSizeForList();

	m_pScanner = new FolderViewScanner(m_pWebBrowser, m_hWnd);

	return 0;
}

LRESULT FSWindow::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	if (m_lv)
		RemoveWindowSubclass(m_lv, ListViewSubclassProc, 0);
	if (m_hFont)
	{
		DeleteObject(m_hFont);
		m_hFont = NULL;
	}
	return 0;
}

// The program is per-monitor DPI aware (see Window.manifest): Windows doesn't scale it, so it
// must draw at the DPI of the monitor the window is on. Controls created with CreateWindow keep
// the font they were given, so give the list the system font scaled for the window's DPI.
void FSWindow::UpdateFontForDpi()
{
	UINT dpi = GetDpiForWindow(m_hWnd);
	LOGFONT lf;
	if (!SystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0, dpi))
		return;
	HFONT hFont = CreateFontIndirect(&lf);
	if (!hFont)
		return;
	::SendMessage(m_lv, WM_SETFONT, (WPARAM)hFont, TRUE);
	if (m_hFont)
		DeleteObject(m_hFont);
	m_hFont = hFont;
}

// Sent when the window moves to a monitor with a different scale factor, or when the scale
// factor of its monitor changes in Windows settings.
LRESULT FSWindow::OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// Windows suggests a new size and position that keeps the window the same physical size.
	RECT* prcNew = (RECT*)lParam;
	SetWindowPos(NULL, prcNew->left, prcNew->top, prcNew->right - prcNew->left, prcNew->bottom - prcNew->top,
		SWP_NOZORDER | SWP_NOACTIVATE);
	UpdateFontForDpi();
	// The new font changes the size of the rows and columns, so fit the window to the list again.
	AdjustSizeForList();
	return 0;
}

// Applies the light or dark theme to the window, following the Windows app mode.
void FSWindow::ApplyTheme()
{
	m_bDark = IsDarkModeEnabled();

	// Title bar (documented: DWMWA_USE_IMMERSIVE_DARK_MODE, value 20)
	BOOL bDarkTitle = m_bDark;
	DwmSetWindowAttribute(m_hWnd, 20, &bDarkTitle, sizeof(bDarkTitle));

	// List, scroll bars and column header
	SetWindowTheme(m_lv, m_bDark ? L"DarkMode_Explorer" : L"Explorer", NULL);
	SetWindowTheme(ListView_GetHeader(m_lv), m_bDark ? L"DarkMode_ItemsView" : NULL, NULL);
	COLORREF background = m_bDark ? DARK_BACKGROUND : GetSysColor(COLOR_WINDOW);
	COLORREF text = m_bDark ? DARK_TEXT : GetSysColor(COLOR_WINDOWTEXT);
	ListView_SetBkColor(m_lv, background);
	ListView_SetTextBkColor(m_lv, background);
	ListView_SetTextColor(m_lv, text);

	::RedrawWindow(m_lv, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_ERASE);
}

// Windows broadcasts "ImmersiveColorSet" when the app mode (light/dark) changes.
LRESULT FSWindow::OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	if (lParam && lstrcmp((LPCTSTR)lParam, _T("ImmersiveColorSet")) == 0)
		ApplyTheme();
	bHandled = FALSE;
	return 0;
}

LRESULT FSWindow::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	::MoveWindow(m_lv, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
	return 0;
}

static void FormatFolderInfoBuffer(FOLDERINFO2& fi, LPTSTR buffer, UINT cch)
{
	FormatSize(fi.nLogicalSize, buffer, cch);

	size_t len = wcslen(buffer);
	if (len < cch - 1 && (fi.giff == GIFF_DIRTY || fi.giff == GIFF_SCANNING))
	{
		buffer[len] = fi.giff == GIFF_DIRTY ? L'~' : L'+';
		buffer[len + 1] = L'\0';
	}
}

LRESULT FSWindow::OnNewItems(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	std::vector<ListItem*> items = m_pScanner->GetNewItems();
	if (!items.empty())
	{
		for (size_t i=0; i<items.size(); i++)
		{
			LVITEM item = {0};
			item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
			item.iItem = INT_MAX;
			item.iSubItem = 0;
			item.pszText = (LPTSTR)items[i]->GetName();
			item.iImage = items[i]->GetIcon();
			item.lParam = (LPARAM)items[i];
			item.iItem = ListView_InsertItem(m_lv, &item);
			item.mask = LVIF_TEXT;
			item.iSubItem = 1;
			TCHAR buffer[50];
			FormatFolderInfoBuffer(items[i]->GetSize(), buffer, 50);
			item.pszText = buffer;
			ListView_SetItem(m_lv, &item);

			m_nameMap.insert(pair<wstring, ListItem*>(items[i]->GetFileName(), items[i]));
		}
		AdjustSizeForList();
	}
	return 0;
}

LRESULT FSWindow::OnRefreshItems(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	std::vector<RefreshItem*> items = m_pScanner->GetRefreshItems();
	if (!items.empty())
	{
		for (size_t i=0; i<items.size(); i++)
		{
			map<wstring, ListItem*>::iterator mapFind = m_nameMap.find(items[i]->GetFileName());
			if (mapFind != m_nameMap.end())
			{
				mapFind->second->UpdateSize(items[i]->GetSize());
				LVFINDINFO lvfi;
				lvfi.flags = LVFI_PARAM;
				lvfi.lParam = (LPARAM)mapFind->second;
				int iFind = ListView_FindItem(m_lv, -1, &lvfi);
				if (iFind >= 0)
				{
					LVITEM lvi;
					lvi.mask = LVIF_TEXT;
					lvi.iItem = iFind;
					lvi.iSubItem = 1;
					TCHAR buffer[50];
					FormatFolderInfoBuffer(items[i]->GetSize(), buffer, 50);
					lvi.pszText = buffer;
					ListView_SetItem(m_lv, &lvi);
				}
			}
			delete items[i];
		}
		AdjustSizeForList();
	}
	return 0;
}

LRESULT FSWindow::OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	::PostMessage(g_hwndMain, WM_KILLWINDOW, 0, (LPARAM)this);
	return 0;
}

int CALLBACK compareIndex(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	return ((ListItem*)lParam1)->GetIndex() - ((ListItem*)lParam2)->GetIndex();
}

int CALLBACK compareSize(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	ULONGLONG size1 = ((ListItem*)lParam1)->GetSize().nLogicalSize;
	ULONGLONG size2 = ((ListItem*)lParam2)->GetSize().nLogicalSize;
	return size1 == size2 ? 0 : size1 > size2 ? -1 : 1;
}

LRESULT FSWindow::OnItemActivate(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	int i = ListView_GetNextItem(m_lv, -1, LVNI_SELECTED);
	if (i < 0)
		return 0;
	LVITEM lvi;
	lvi.mask = LVIF_PARAM;
	lvi.iItem = i;
	lvi.iSubItem = 0;
	ListView_GetItem(m_lv, &lvi);
	ListItem* pItem = (ListItem*)lvi.lParam;

	IServiceProvider* psp;
	if (SUCCEEDED(m_pWebBrowser->QueryInterface(IID_IServiceProvider, (void**)&psp)))
	{
		IShellBrowser* psb;
		if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb)))
		{
			psb->BrowseObject(pItem->GetPidl(), SBSP_DEFBROWSER | SBSP_RELATIVE);
			psb->Release();
		}
		psp->Release();
	}
	return 0;
}

LRESULT FSWindow::OnColumnClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)pnmh;
	PFNLVCOMPARE compareFunc = pnmlv->iSubItem == 0 ? compareIndex : compareSize;
	ListView_SortItems(m_lv, compareFunc, 0);
	return 0;
}

LRESULT FSWindow::OnDeleteAllItems(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
{
	for (map<wstring, ListItem*>::iterator itr = m_nameMap.begin(); itr != m_nameMap.end(); itr++)
		delete itr->second;
	m_nameMap.clear();
	return TRUE;
}

void FSWindow::CreateIfUnminimizedFolderView()
{
	// if the web browser is a shell view, create the window
	IServiceProvider* psp;
	if (SUCCEEDED(m_pWebBrowser->QueryInterface(IID_IServiceProvider, (void**)&psp)))
	{
		IShellBrowser* psb;
		if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb)))
		{
			// Find the tab's own window: the shell browser's window, or the ShellTabWindowClass
			// window that contains it.
			HWND hwndBrowser;
			m_hwndTab = SUCCEEDED(psb->GetWindow(&hwndBrowser)) ? FindTabWindow(hwndBrowser) : NULL;

			IShellView* psv;
			if (SUCCEEDED(psb->QueryActiveShellView(&psv)))
			{
				IFolderView* pfv;
				if (SUCCEEDED(psv->QueryInterface(IID_IFolderView, (void**)&pfv)))
				{
					IShellFolder2* psf2;
					if (SUCCEEDED(pfv->GetFolder(IID_IShellFolder2, (void**)&psf2)))
					{
						IPersistFolder2* ppf2;
						if (SUCCEEDED(psf2->QueryInterface(IID_IPersistFolder2, (void**)&ppf2)))
						{
							LPITEMIDLIST pFolderID;
							if (SUCCEEDED(ppf2->GetCurFolder(&pFolderID)))
							{
								TCHAR szFolder[MAX_PATH];
								if (SHGetPathFromIDList(pFolderID, szFolder))
								{
									HWND hwndExplorer;
									if (SUCCEEDED(m_pWebBrowser->get_HWND((SHANDLE_PTR*)&hwndExplorer)))
									{
										m_hwndExplorer = hwndExplorer;
										WINDOWPLACEMENT wp = { sizeof(wp) };
										::GetWindowPlacement(hwndExplorer, &wp);
										if (wp.showCmd != SW_SHOWMINIMIZED)
										{
											RECT rc;
											::GetWindowRect(hwndExplorer, &rc);
											rc.left = rc.right;
											if (Create(hwndExplorer, rc, APP_NAME))
											{
												// Only the window of the tab in front is shown. The window
												// manager shows and hides the windows as tabs are switched.
												if (m_hwndTab == NULL || m_hwndTab == GetActiveTab(hwndExplorer))
													ShowWindow(SW_SHOWNOACTIVATE);
												::PostMessage(g_hwndMain, WM_UPDATETABS, (WPARAM)hwndExplorer, 0);
											}
										}
									}
								}
								CoTaskMemFree(pFolderID);
							}
							ppf2->Release();
						}
						psf2->Release();
					}
					pfv->Release();
				}
				psv->Release();
			}
			psb->Release();
		}
		psp->Release();
	}
}

void FSWindow::AdjustSizeForList()
{
	DWORD dwExLvStyle = ListView_GetExtendedListViewStyle(m_lv);
	ListView_SetExtendedListViewStyle(m_lv, dwExLvStyle & ~LVS_EX_AUTOSIZECOLUMNS);

	ListView_SetColumnWidth(m_lv, 0, LVSCW_AUTOSIZE);
	ListView_SetColumnWidth(m_lv, 1, LVSCW_AUTOSIZE);

	// Use the measurements for the DPI of the monitor the window is on.
	UINT dpi = GetDpiForWindow(m_hWnd);

	// get the original window position and coordinates of the monitor it's on
	RECT rc;
	GetWindowRect(&rc);
	HMONITOR hMonitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof(mi) };
	GetMonitorInfo(hMonitor, &mi);
	bool isTouchingRightSideOfMonitor = rc.right == mi.rcWork.right;

	// now get the size the list wants to be and the size it is
	DWORD approx = ListView_ApproximateViewRect(m_lv, -1, -1, -1);
	::GetWindowRect(m_lv, &rc);
	int approxWidth = LOWORD(approx) + 4;
	int approxHeight = HIWORD(approx);

	if (isTouchingRightSideOfMonitor)
		rc.left = rc.right - approxWidth;
	else
		rc.right = rc.left + approxWidth;
	rc.bottom = rc.top + approxHeight;
	AdjustWindowRectExForDpi(&rc, GetWindowLong(GWL_STYLE), FALSE, GetWindowLong(GWL_EXSTYLE), dpi);

	// if the title bar somehow got too high for the screen, align it with the top of the monitor
	if (rc.top < mi.rcWork.top)
	{
		rc.bottom += mi.rcWork.top - rc.top;
		rc.top += mi.rcWork.top - rc.top;
	}
	// if we're going off the bottom, shift the window up if there's space
	if (rc.bottom > mi.rcWork.bottom && rc.top > mi.rcWork.top)
	{
		int shift = min(rc.top - mi.rcWork.top, rc.bottom - mi.rcWork.bottom);
		rc.top -= shift;
		rc.bottom -= shift;
	}
	// if we're still going off the bottom, trim it at the bottom and get ready for a scroll bar
	if (rc.bottom > mi.rcWork.bottom)
	{
		rc.bottom = mi.rcWork.bottom;
		rc.right += GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
	}
	// if we're bumping out past the right of the monitor, bump it left instead
	if (rc.right > mi.rcWork.right)
	{
		rc.left -= rc.right - mi.rcWork.right;
		rc.right = mi.rcWork.right;
	}
	MoveWindow(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);

	ListView_SetExtendedListViewStyle(m_lv, dwExLvStyle);
}


// Shows this window if its tab is the one in front, and hides it otherwise. prcPrevious is
// the position of the window that was shown for the previous tab, if any, so the size
// window stays where the user put it when switching tabs.
void FSWindow::UpdateTabVisibility(HWND hwndActiveTab, const RECT* prcPrevious)
{
	if (m_hWnd == NULL)
		return;
	bool bActive = IsTab(hwndActiveTab);
	if (bActive && !IsWindowVisible())
	{
		if (prcPrevious)
			SetWindowPos(NULL, prcPrevious->left, prcPrevious->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		ShowWindow(SW_SHOWNOACTIVATE);
		AdjustSizeForList();
	}
	else if (!bActive && IsWindowVisible())
	{
		ShowWindow(SW_HIDE);
	}
}

_ATL_FUNC_INFO FSWindow::NavigateCompleteInfo = { CC_STDCALL, VT_EMPTY, 2, {VT_DISPATCH, VT_BYREF | VT_VARIANT} };
_ATL_FUNC_INFO FSWindow::QuitInfo = { CC_STDCALL, VT_EMPTY, 0, NULL };

void FSWindow::OnNavigateComplete(IDispatch* pDisp, VARIANT* URL)
{
	// there may be no window yet if the explorer window was either navigated to a web page, or was minimized
	if (m_hWnd == NULL)
	{
		CreateIfUnminimizedFolderView();
	}
	else
	{
		m_pScanner->Quit();

		// the notification from the control will clean up the old items
		ListView_DeleteAllItems(m_lv);

		AdjustSizeForList();

		m_pScanner = new FolderViewScanner(m_pWebBrowser, m_hWnd);
	}
}

static FSWindow* pZombie = NULL;

void FSWindow::OnQuit()
{
	::PostMessage(g_hwndMain, WM_KILLWINDOW, 0, (LPARAM)this);
}

class ShellWindowManager :
	public CWindowImpl<ShellWindowManager>,
	public IDispEventSimpleImpl<1, ShellWindowManager, &DIID_DShellWindowsEvents>
{
public:
	ShellWindowManager();

	DECLARE_WND_CLASS(SHELL_WINDOW_CLASS_NAME)

private:
	void Activate();
	void Deactivate();

BEGIN_MSG_MAP(FSWindow)
	MESSAGE_HANDLER(WM_CREATE, OnCreate)
	MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
	MESSAGE_HANDLER(WM_NOTIFYICON, OnAppIconMsg)
	MESSAGE_HANDLER(WM_KILLWINDOW, OnKillWindow)
	MESSAGE_HANDLER(WM_UPDATETABS, OnUpdateTabs)
	COMMAND_ID_HANDLER(ID_SHOW, OnShowPopups)
	COMMAND_ID_HANDLER(ID_START, OnStart)
	COMMAND_ID_HANDLER(ID_SETTINGS, OnSettings)
	COMMAND_ID_HANDLER(ID_EXIT, OnExit)
END_MSG_MAP()

static _ATL_FUNC_INFO ShellWindowsEventInfo;
static _ATL_FUNC_INFO QuitInfo;
BEGIN_SINK_MAP(ShellWindowManager)
	SINK_ENTRY_INFO(1, DIID_DShellWindowsEvents, DISPID_WINDOWREGISTERED, OnWindowRegistered, &ShellWindowsEventInfo)
END_SINK_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnAppIconMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnKillWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnUpdateTabs(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnShowPopups(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	LRESULT OnStart(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	LRESULT OnSettings(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	LRESULT OnExit(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	void __stdcall OnWindowRegistered(long lCookie);

	void AddFSWindow(IWebBrowser2* pwb);
	void WatchTabs(HWND hwndExplorer);
	static void CALLBACK TabEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime);

	HICON m_icon;
	HICON m_gray;
	CComPtr<IShellWindows> m_pShellWindows;
	set<FSWindow*> m_FSWindows;
	// One event hook per process that hosts File Explorer windows, to see tabs being switched.
	map<DWORD, HWINEVENTHOOK> m_tabHooks;
	bool m_bAutoRun;
};

_ATL_FUNC_INFO ShellWindowManager::ShellWindowsEventInfo = { CC_STDCALL, VT_EMPTY, 1, {VT_I4} };

ShellWindowManager::ShellWindowManager() : m_icon(NULL), m_gray(NULL)
{
	g_hwndMain = Create(HWND_MESSAGE);
}

LRESULT ShellWindowManager::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	m_icon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

	ICONINFO ii;
	if (GetIconInfo(m_icon, &ii))
	{
		BITMAP bm;
		if (GetObject(ii.hbmColor, sizeof(bm), &bm))
		{
			if (bm.bmBitsPixel == 32)
			{
				BITMAPINFO bmi = { sizeof(bmi) };
				HDC hdc = CreateCompatibleDC(NULL);
				if (GetDIBits(hdc, ii.hbmColor, 0, 0, NULL, &bmi, DIB_RGB_COLORS))
				{
					bmi.bmiHeader.biCompression = BI_RGB;
					DWORD* buffer = (DWORD*)(new BYTE[bmi.bmiHeader.biSizeImage]);
					int lines = GetDIBits(hdc, ii.hbmColor, 0, bm.bmHeight, buffer, &bmi, DIB_RGB_COLORS);
					if (lines == bm.bmHeight)
					{
						for (int i = 0; i < bm.bmWidth * bm.bmHeight; i++)
						{
							DWORD color = buffer[i];
							int total = ((int)GetRValue(color) + (int)GetBValue(color) + (int)GetGValue(color)) / 3;
							buffer[i] &= 0xFF000000;
							buffer[i] |= RGB(total, total, total);
						}
						if (SetDIBits(hdc, ii.hbmColor, 0, lines, buffer, &bmi, DIB_RGB_COLORS))
						{
							m_gray = CreateIconIndirect(&ii);
						}
					}
				}
				DeleteDC(hdc);
			}
		}
		DeleteObject(ii.hbmColor);
		DeleteObject(ii.hbmMask);
	}

	NOTIFYICONDATA nid = { sizeof(nid) };
	nid.hWnd = m_hWnd;
	nid.uID = 1;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
	nid.uCallbackMessage = WM_NOTIFYICON;
	nid.hIcon = m_icon;
	lstrcpy(nid.szTip, APP_NAME);
	BOOL bRes = Shell_NotifyIcon(NIM_ADD, &nid);

	nid.uVersion = NOTIFYICON_VERSION_4;
	bRes = Shell_NotifyIcon(NIM_SETVERSION, &nid);

	Activate();

	return 0;
}

LRESULT ShellWindowManager::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	if (m_pShellWindows)
		Deactivate();

	NOTIFYICONDATA nid = { sizeof(nid) };
	nid.hWnd = m_hWnd;
	nid.uID = 1;
	BOOL b = Shell_NotifyIcon(NIM_DELETE, &nid);

	if (m_gray)
		DestroyIcon(m_gray);

	PostQuitMessage(0);

	return 0;
}

void ShellWindowManager::Activate()
{
	NOTIFYICONDATA nid = { sizeof(nid) };
	nid.hWnd = m_hWnd;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_SHOWTIP;
	nid.hIcon = m_icon;
	Shell_NotifyIcon(NIM_MODIFY, &nid);

	m_pShellWindows.CoCreateInstance(CLSID_ShellWindows);

	long count;
	if (SUCCEEDED(m_pShellWindows->get_Count(&count)))
	{
		for (long i=0; i<count; i++)
		{
			VARIANT varIndex;
			V_VT(&varIndex) = VT_I4;
			V_I4(&varIndex) = i;
			IDispatch* pDisp;
			if (m_pShellWindows->Item(varIndex, &pDisp) == S_OK)
			{
				IWebBrowser2* pwb;
				if (SUCCEEDED(pDisp->QueryInterface(IID_IWebBrowser2, (void**)&pwb)))
				{
					AddFSWindow(pwb);
				}
				pDisp->Release();
			}
		}
	}

	DispEventAdvise(m_pShellWindows);
}

void ShellWindowManager::Deactivate()
{
	if (m_gray)
	{
		NOTIFYICONDATA nid = { sizeof(nid) };
		nid.hWnd = m_hWnd;
		nid.uID = 1;
		nid.uFlags = NIF_ICON | NIF_SHOWTIP;
		nid.hIcon = m_gray;
		Shell_NotifyIcon(NIM_MODIFY, &nid);
	}

	for (set<FSWindow*>::iterator itr = m_FSWindows.begin(); itr != m_FSWindows.end(); itr++)
		delete *itr;
	m_FSWindows.clear();
	for (map<DWORD, HWINEVENTHOOK>::iterator itr = m_tabHooks.begin(); itr != m_tabHooks.end(); itr++)
		UnhookWinEvent(itr->second);
	m_tabHooks.clear();
	DispEventUnadvise(m_pShellWindows);
	m_pShellWindows.Release();
}

LRESULT ShellWindowManager::OnAppIconMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	switch (LOWORD(lParam))
	{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		{
			PostMessage(WM_COMMAND, ID_SHOW);
			break;
		}
		case WM_CONTEXTMENU:
		{
			SetForegroundWindow(m_hWnd);
			// The message-only window doesn't receive theme change broadcasts, so refresh the
			// menu theme each time the menu opens, in case the app mode changed.
			AllowDarkMenus();
			HMENU hMenu = LoadMenu((HINSTANCE)GetWindowLongPtr(GWLP_HINSTANCE), MAKEINTRESOURCE(IDR_MENU));
			HMENU hSubMenu = GetSubMenu(hMenu, 0);
			MENUITEMINFO mii = { sizeof(mii) };
			mii.fMask = MIIM_STATE;
			mii.fState = MFS_DEFAULT;
			if (m_pShellWindows)
				mii.fState |= MFS_CHECKED;
			SetMenuItemInfo(hSubMenu, ID_SHOW, FALSE, &mii);
			HKEY hKey;
			if (RegCreateKey(HKEY_CURRENT_USER, REG_KEY_PATH, &hKey) == ERROR_SUCCESS)
			{
				if (RegQueryValueEx(hKey, APP_NAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
				{
					mii.fState = MFS_CHECKED;
					SetMenuItemInfo(hSubMenu, ID_START, FALSE, &mii);
				}
				RegCloseKey(hKey);
			}
			m_bAutoRun = mii.fState == MFS_CHECKED;
			TrackPopupMenu(hSubMenu, TPM_RIGHTBUTTON | (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN),
					GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam), 0, m_hWnd, NULL);
			DestroyMenu(hMenu);
			break;
		}
	}
	return 0;
}

LRESULT ShellWindowManager::OnKillWindow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	FSWindow* pWindow = (FSWindow*)lParam;
	m_FSWindows.erase(pWindow);
	delete pWindow;
	return 0;
}

void ShellWindowManager::AddFSWindow(IWebBrowser2* pwb)
{
	m_FSWindows.insert(new FSWindow(pwb));
	HWND hwndExplorer;
	if (SUCCEEDED(pwb->get_HWND((SHANDLE_PTR*)&hwndExplorer)) && hwndExplorer)
		WatchTabs(hwndExplorer);
}

// File Explorer doesn't report tab switches through its automation events, but switching tabs
// shows the window of the new tab and hides the old one. Watch those show/hide events, only in
// the process that hosts the Explorer window, so the program wakes up only for Explorer windows.
void ShellWindowManager::WatchTabs(HWND hwndExplorer)
{
	DWORD dwProcessId = 0;
	GetWindowThreadProcessId(hwndExplorer, &dwProcessId);
	if (dwProcessId == 0 || m_tabHooks.find(dwProcessId) != m_tabHooks.end())
		return;
	HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, NULL, TabEventProc,
		dwProcessId, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
	if (hook)
		m_tabHooks[dwProcessId] = hook;
}

// Called on this program's main thread (out-of-context hook), for windows shown or hidden in
// the watched processes.
void CALLBACK ShellWindowManager::TabEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime)
{
	if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || hwnd == NULL)
		return;
	TCHAR szClass[32];
	if (GetClassName(hwnd, szClass, 32) && lstrcmp(szClass, _T("ShellTabWindowClass")) == 0)
		::PostMessage(g_hwndMain, WM_UPDATETABS, (WPARAM)GetAncestor(hwnd, GA_ROOT), 0);
}

// Shows the size window of the active tab of an Explorer window and hides the others.
LRESULT ShellWindowManager::OnUpdateTabs(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	HWND hwndExplorer = (HWND)wParam;
	if (!::IsWindow(hwndExplorer))
		return 0;
	HWND hwndActiveTab = GetActiveTab(hwndExplorer);

	// Remember where the currently shown window is (the previous tab's), before hiding it.
	RECT rcPrevious;
	bool bHavePrevious = false;
	for (set<FSWindow*>::iterator itr = m_FSWindows.begin(); itr != m_FSWindows.end(); itr++)
	{
		FSWindow* p = *itr;
		if (p->GetExplorerWindow() == hwndExplorer && p->m_hWnd && p->IsWindowVisible() && !p->IsTab(hwndActiveTab))
		{
			p->GetWindowRect(&rcPrevious);
			bHavePrevious = true;
			break;
		}
	}
	// Hide first, then show, so two windows are never shown at the same time.
	for (int pass = 0; pass < 2; pass++)
	{
		for (set<FSWindow*>::iterator itr = m_FSWindows.begin(); itr != m_FSWindows.end(); itr++)
		{
			FSWindow* p = *itr;
			if (p->GetExplorerWindow() != hwndExplorer)
				continue;
			bool bIsActive = p->IsTab(hwndActiveTab);
			if ((pass == 0 && !bIsActive) || (pass == 1 && bIsActive))
				p->UpdateTabVisibility(hwndActiveTab, bHavePrevious ? &rcPrevious : NULL);
		}
	}
	return 0;
}

LRESULT ShellWindowManager::OnShowPopups(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	if (m_pShellWindows)
		Deactivate();
	else
		Activate();
	return 0;
}

static void SetAutoRun()
{
	TCHAR szFile[MAX_PATH];
	DWORD dw = GetModuleFileName(NULL, szFile, MAX_PATH);
	SHSetValue(HKEY_CURRENT_USER, REG_KEY_PATH, APP_NAME, REG_SZ, szFile, dw * sizeof(WCHAR));
}

static void DeleteAutoRun()
{
	SHDeleteValue(HKEY_CURRENT_USER, REG_KEY_PATH, APP_NAME);
}

LRESULT ShellWindowManager::OnStart(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	// m_bAutoRun is the existence of the registry key when the menu was displayed.
	// Put the registry key in the opposite state.
	if (m_bAutoRun)
	{
		DeleteAutoRun();
	}
	else
	{
		SetAutoRun();
	}
	return 0;
}

LRESULT ShellWindowManager::OnSettings(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	// The settings live in the Control Panel item FolderSize.cpl, installed in the same folder
	// as this program. Open it through control.exe, as Control Panel itself does, so it is the
	// same panel whether it is opened from here or from Control Panel.
	TCHAR szCpl[MAX_PATH + 2];
	szCpl[0] = _T('"');
	DWORD dw = GetModuleFileName(NULL, szCpl + 1, MAX_PATH);
	if (dw == 0 || dw >= MAX_PATH)
		return 0;
	PathRemoveFileSpec(szCpl + 1);
	if (!PathAppend(szCpl + 1, _T("FolderSize.cpl")))
		return 0;
	lstrcat(szCpl, _T("\""));
	ShellExecute(NULL, _T("open"), _T("control.exe"), szCpl, NULL, SW_SHOWNORMAL);
	return 0;
}

LRESULT ShellWindowManager::OnExit(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	DestroyWindow();
	return 0;
}

void ShellWindowManager::OnWindowRegistered(long lCookie)
{
	// assume the new web browser is always the last one in the list
	long count;
	if (SUCCEEDED(m_pShellWindows->get_Count(&count)))
	{
		VARIANT varIndex;
		V_VT(&varIndex) = VT_I4;
		V_I4(&varIndex) = count - 1;
		IDispatch* pDisp;
		if (SUCCEEDED(m_pShellWindows->Item(varIndex, &pDisp)))
		{
			IWebBrowser2* pwb;
			if (SUCCEEDED(pDisp->QueryInterface(IID_IWebBrowser2, (void**)&pwb)))
			{
				AddFSWindow(pwb);
			}
			pDisp->Release();
		}
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	// find an already running instance
	HWND hwndExisting = FindWindow(SHELL_WINDOW_CLASS_NAME, NULL);

	int argc;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv)
	{
		for (int i = 1; i < argc; i++)
		{
			if (argv[i][0] == _T('/') || argv[i][0] == _T('-'))
			{
				if (lstrcmpi(argv[i] + 1, _T("install")) == 0)
				{
					SetAutoRun();
				}
				if (lstrcmpi(argv[i] + 1, _T("remove")) == 0)
				{
					if (hwndExisting)
					{
						PostMessage(hwndExisting, WM_CLOSE, 0, 0);
						DWORD dwProcessId;
						if (GetWindowThreadProcessId(hwndExisting, &dwProcessId))
						{
							HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, dwProcessId);
							if (hProcess)
							{
								WaitForSingleObject(hProcess, 8000);
								CloseHandle(hProcess);
							}
						}
					}
					DeleteAutoRun();
					return 0;
				}
			}
		}
		LocalFree(argv);
	}

	if (hwndExisting)
	{
		// send it a message to tell it to activate in some way that it can
		return 0;
	}

	InitCommonControls();
	CoInitialize(NULL);

	{
		ShellWindowManager swm;

		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0))
			DispatchMessage(&msg);
	}

	CoUninitialize();

	return 0;
}