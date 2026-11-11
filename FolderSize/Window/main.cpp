#include "StdAfx.h"
#include "resource.h"
#include "..\\Common\\Settings.h"

#define WM_NOTIFYICON (WM_APP + 1)
#define WM_KILLWINDOW (WM_APP + 2)

#define APP_NAME _T("Folder Size Redux")
// Name of the value under the Run key for "Start on Windows startup". Kept from the original
// Folder Size so the setting carries over when Folder Size Redux replaces it.
#define AUTORUN_VALUE_NAME _T("Folder Size")
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
	ListItem(LPCTSTR filename, LPITEMIDLIST pidl, FOLDERINFO2& size, int index, int icon, LPCTSTR name, bool isFolder) : m_pidl(pidl), m_size(size), m_index(index), m_icon(icon), m_isFolder(isFolder)
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
	bool IsFolder() { return m_isFolder; }
	
	void UpdateSize(FOLDERINFO2& size) { m_size = size; }

private:
	LPITEMIDLIST m_pidl;
	FOLDERINFO2 m_size;
	int m_index;
	bool m_isFolder;
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
// Posted by a scanner when it has listed all the items of the folder. wParam is the scanner's
// generation number, so the window can ignore messages from scanners it has replaced.
#define WM_ENUMDONE      (WM_APP + 4)


class FolderViewScanner
{
public:
	FolderViewScanner(IWebBrowser2* pWebBrowser, HWND hWndPost, UINT generation);
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
	UINT m_generation;

	HANDLE m_hThread;
	HANDLE m_hQuitEvent;

	CRITICAL_SECTION m_cs;
	std::vector<ListItem*> m_newItems;
	std::vector<RefreshItem*> m_refreshItems;
};

FolderViewScanner::FolderViewScanner(IWebBrowser2* pWebBrowser, HWND hWndPost, UINT generation) : m_pBrowserStream(NULL), m_hWndPost(hWndPost), m_generation(generation), m_hThread(NULL), m_hQuitEvent(NULL)
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
											ListItem* pItem = new ListItem(PathFindFileName(pPath), pItemID, fi, index++, shfi.iIcon,
												// Files are shown with their real name, extension included, even when
												// File Explorer hides extensions: a folder and a .zip file with the same
												// name can then be told apart. Folders keep the name File Explorer shows
												// (some system folders show a localized name).
												bIsDirectory ? shfi.szDisplayName : PathFindFileName(pPath), bIsDirectory);
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

	if (WaitForSingleObject(m_hQuitEvent, 0) == WAIT_TIMEOUT)
		PostMessage(m_hWndPost, WM_ENUMDONE, m_generation, 0);

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

// Space between the list and the left and right edges of the window, in DIPs (scaled with
// the DPI), so the "Name" header and the icons line up with the window title.
static const int LIST_MARGIN_X_DIP = 3;

// Extra width added to the Size column, in DIPs. Its numbers are right-aligned, so this is the
// space between the column separator and the longest size; it matches the space between the
// left edge and "Name".
static const int SIZE_COLUMN_EXTRA_DIP = 3;

// Space between a column separator and the sort arrow drawn in the header, in DIPs.
static const int SORT_ARROW_MARGIN_DIP = 7;

// Delay before reloading the folder after a change notification, in milliseconds. Changes
// often come in bursts (moving many files); they are handled together after the burst.
static const UINT RELOAD_DELAY_MS = 300;
static const UINT_PTR RELOAD_TIMER_ID = 1;
#define WM_SHELLCHANGE (WM_APP + 5)

// Colors of the list in dark mode, matching the dark File Explorer.
static const COLORREF DARK_BACKGROUND = RGB(0x19, 0x19, 0x19);
static const COLORREF DARK_TEXT = RGB(0xFF, 0xFF, 0xFF);

// What ListViewSubclassProc needs to draw the column header.
struct HeaderDrawState
{
	bool bDark;          // dark mode: light header text
	int sortedColumn;    // column showing a sort arrow: 0 (names, arrow up) or 1 (sizes, arrow down)
};

// Draws the sort arrow of a header item. The header's own arrow is drawn centered at the top of
// the column, as in File Explorer; this one is centered vertically, near the column separator
// on the side opposite to the text: at the right of the name column (left-aligned text) and at
// the left of the size column (right-aligned text). It uses the arrow image of the header's
// theme, so it matches the light or dark style.
static void DrawSortArrow(HWND hwndHeader, HDC hdc, const RECT& rcItem, int column)
{
	UINT dpi = GetDpiForWindow(hwndHeader);
	HTHEME hTheme = OpenThemeDataForDpi(hwndHeader, L"Header", dpi);
	if (!hTheme)
		return;
	int state = column == 0 ? HSAS_SORTEDUP : HSAS_SORTEDDOWN;
	SIZE size;
	if (SUCCEEDED(GetThemePartSize(hTheme, hdc, HP_HEADERSORTARROW, state, NULL, TS_TRUE, &size)))
	{
		int margin = MulDiv(SORT_ARROW_MARGIN_DIP, dpi, 96);
		RECT rc;
		rc.left = column == 0 ? rcItem.right - margin - size.cx : rcItem.left + margin;
		rc.right = rc.left + size.cx;
		rc.top = rcItem.top + (rcItem.bottom - rcItem.top - size.cy) / 2;
		rc.bottom = rc.top + size.cy;
		DrawThemeBackground(hTheme, hdc, HP_HEADERSORTARROW, state, &rc, NULL);
	}
	CloseThemeData(hTheme);
}

// Subclass of the list view: the column header sends its custom draw notifications to the
// list view, not to our window, so the list view is subclassed to draw the header: a light
// text color in dark mode (the dark header theme keeps dark text otherwise), and the sort arrow.
static LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	const HeaderDrawState* pState = (const HeaderDrawState*)dwRefData;
	if (uMsg == WM_NOTIFY)
	{
		LPNMHDR pnmh = (LPNMHDR)lParam;
		if (pnmh->hwndFrom == ListView_GetHeader(hwnd) && pnmh->code == NM_CUSTOMDRAW)
		{
			LPNMCUSTOMDRAW pcd = (LPNMCUSTOMDRAW)lParam;
			if (pcd->dwDrawStage == CDDS_PREPAINT)
				return CDRF_NOTIFYITEMDRAW;
			if (pcd->dwDrawStage == CDDS_ITEMPREPAINT)
			{
				if (pState->bDark)
					SetTextColor(pcd->hdc, DARK_TEXT);
				return CDRF_NOTIFYPOSTPAINT;
			}
			if (pcd->dwDrawStage == CDDS_ITEMPOSTPAINT)
			{
				if ((int)pcd->dwItemSpec == pState->sortedColumn)
					DrawSortArrow(pnmh->hwndFrom, pcd->hdc, pcd->rc, pState->sortedColumn);
				return CDRF_DODEFAULT;
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


static PFNLVCOMPARE GetCompareFunction(int sort);

class FSWindow :
	public CWindowImpl<FSWindow, CWindow, CWinTraits<WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX, WS_EX_TOOLWINDOW> >,
	public IDispEventSimpleImpl<1, FSWindow, &DIID_DWebBrowserEvents2>,
	public IDispEventSimpleImpl<2, FSWindow, &DIID_DShellFolderViewEvents>
{
	typedef IDispEventSimpleImpl<1, FSWindow, &DIID_DWebBrowserEvents2> BrowserEvents;
	typedef IDispEventSimpleImpl<2, FSWindow, &DIID_DShellFolderViewEvents> FolderViewEvents;

public:
	FSWindow(IWebBrowser2* pWebBrowser);
	~FSWindow();

	DECLARE_WND_CLASS_EX(NULL, 0, COLOR_WINDOW);

BEGIN_MSG_MAP(FSWindow)
	MESSAGE_HANDLER(WM_CREATE, OnCreate)
	MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
	MESSAGE_HANDLER(WM_SIZE, OnSize)
	MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
	MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
	MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
	MESSAGE_HANDLER(WM_NEWITEMS, OnNewItems)
	MESSAGE_HANDLER(WM_REFRESHITEMS, OnRefreshItems)
	MESSAGE_HANDLER(WM_ENUMDONE, OnEnumDone)
	MESSAGE_HANDLER(WM_SHELLCHANGE, OnShellChange)
	MESSAGE_HANDLER(WM_TIMER, OnTimer)
	MESSAGE_HANDLER(WM_CLOSE, OnClose)
	NOTIFY_HANDLER(1, LVN_ITEMACTIVATE, OnItemActivate)
	NOTIFY_HANDLER(1, LVN_COLUMNCLICK, OnColumnClick)
	NOTIFY_HANDLER(1, LVN_DELETEALLITEMS, OnDeleteAllItems)
END_MSG_MAP()

	LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnNewItems(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnRefreshItems(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnEnumDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnShellChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	LRESULT OnItemActivate(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnColumnClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnDeleteAllItems(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);

static _ATL_FUNC_INFO NavigateCompleteInfo;
static _ATL_FUNC_INFO QuitInfo;
static _ATL_FUNC_INFO FileListEnumDoneInfo;
BEGIN_SINK_MAP(FSWindow)
	SINK_ENTRY_INFO(1, DIID_DWebBrowserEvents2, DISPID_NAVIGATECOMPLETE2, OnNavigateComplete, &NavigateCompleteInfo)
	SINK_ENTRY_INFO(1, DIID_DWebBrowserEvents2, DISPID_ONQUIT, OnQuit, &QuitInfo)
	SINK_ENTRY_INFO(2, DIID_DShellFolderViewEvents, DISPID_FILELISTENUMDONE, OnFileListEnumDone, &FileListEnumDoneInfo)
END_SINK_MAP()

	void __stdcall OnNavigateComplete(IDispatch *pDisp, VARIANT *URL);
	void __stdcall OnQuit();
	void __stdcall OnFileListEnumDone();

	HWND GetExplorerWindow() const { return m_hwndExplorer; }
	bool IsTab(HWND hwndTab) const { return m_hwndTab == NULL || m_hwndTab == hwndTab; }
	void UpdateTabVisibility(HWND hwndActiveTab, const RECT* prcPrevious);

private:
	void CreateIfUnminimizedFolderView();
	void AdjustSizeForList();
	void UpdateFontForDpi();
	void ApplyTheme();
	int GetListMarginX();
	void ApplySort();
	void StartScanner();
	void Reload();
	void WatchFolderChanges();
	void WatchFolderView();
	void StopWatchingFolderView();
	void StopWatchingFolderChanges();
	COLORREF GetBackgroundColor() const;

	HWND m_lv;
	HFONT m_hFont;
	HWND m_hwndExplorer;  // top-level File Explorer window (shared by all its tabs)
	HWND m_hwndTab;       // the tab's own window (ShellTabWindowClass), NULL if unknown
	bool m_bDark;         // dark mode currently applied
	HeaderDrawState m_headerState;  // read by ListViewSubclassProc
	CComPtr<IWebBrowser2> m_pWebBrowser;
	TCHAR m_szFolder[MAX_PATH * 4];
	FolderViewScanner* m_pScanner;
	std::map<std::wstring, ListItem*> m_nameMap;
	int m_sort;               // current sort order (SORT_* in Settings.h)
	UINT m_scanGeneration;    // number of the current scanner (see WM_ENUMDONE)
	bool m_bReloading;        // reloading the same folder after a change (see Reload)
	set<wstring> m_staleItems; // while reloading: items not found again yet
	CComPtr<IDispatch> m_pFolderView;  // File Explorer's view of the folder, for its EnumDone event
	ULONG m_changeNotifyId;   // registration for change notifications of the folder (0 if none)
};

FSWindow::FSWindow(IWebBrowser2* pWebBrowser) : m_lv(NULL), m_hFont(NULL), m_hwndExplorer(NULL), m_hwndTab(NULL), m_bDark(false), m_headerState(), m_pWebBrowser(pWebBrowser), m_pScanner(NULL), m_sort(LoadDefaultSort()), m_scanGeneration(0), m_bReloading(false), m_changeNotifyId(0)
{
	BrowserEvents::DispEventAdvise(m_pWebBrowser);
	CreateIfUnminimizedFolderView();
}

FSWindow::~FSWindow()
{
	if (m_hWnd)
		DestroyWindow();
	if (m_pScanner)
		m_pScanner->Quit();
	StopWatchingFolderView();
	BrowserEvents::DispEventUnadvise(m_pWebBrowser);
}

LRESULT FSWindow::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	RECT rc;
	GetClientRect(&rc);
	int marginX = GetListMarginX();
	m_lv = CreateWindow(WC_LISTVIEW, NULL, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHAREIMAGELISTS | LVS_SINGLESEL,
		marginX, 0, max(0, (int)(rc.right-rc.left) - 2 * marginX), rc.bottom-rc.top,
		m_hWnd, (HMENU)1, (HINSTANCE)GetWindowLongPtr(GWLP_HINSTANCE), NULL);
	if (!m_lv)
		return -1;

	ListView_SetExtendedListViewStyle(m_lv, LVS_EX_FULLROWSELECT | LVS_EX_AUTOSIZECOLUMNS | LVS_EX_DOUBLEBUFFER);

	SetWindowSubclass(m_lv, ListViewSubclassProc, 0, (DWORD_PTR)&m_headerState);
	ApplyTheme();
	UpdateFontForDpi();

	LVCOLUMN column;
	column.mask = LVCF_FMT | LVCF_TEXT;
	column.fmt = LVCFMT_LEFT;
	column.pszText = _T("Name");
	ListView_InsertColumn(m_lv, 0, &column);
	column.fmt = LVCFMT_RIGHT;
	column.pszText = _T("Size ");  // trailing space: see FormatFolderInfoBuffer
	ListView_InsertColumn(m_lv, 1, &column);

	SHFILEINFO shfi;
	HIMAGELIST himl = (HIMAGELIST)SHGetFileInfo(_T(""), FILE_ATTRIBUTE_DIRECTORY, &shfi, sizeof(shfi), SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
	if (himl == NULL)
		return -1;
	ListView_SetImageList(m_lv, himl, LVSIL_SMALL);

	ApplySort();
	AdjustSizeForList();

	StartScanner();
	WatchFolderChanges();
	WatchFolderView();

	return 0;
}

LRESULT FSWindow::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	StopWatchingFolderChanges();
	KillTimer(RELOAD_TIMER_ID);
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
	m_headerState.bDark = m_bDark;

	// Title bar (documented: DWMWA_USE_IMMERSIVE_DARK_MODE, value 20)
	BOOL bDarkTitle = m_bDark;
	DwmSetWindowAttribute(m_hWnd, 20, &bDarkTitle, sizeof(bDarkTitle));

	// List, scroll bars and column header
	SetWindowTheme(m_lv, m_bDark ? L"DarkMode_Explorer" : L"Explorer", NULL);
	SetWindowTheme(ListView_GetHeader(m_lv), m_bDark ? L"DarkMode_ItemsView" : NULL, NULL);
	COLORREF background = GetBackgroundColor();
	COLORREF text = m_bDark ? DARK_TEXT : GetSysColor(COLOR_WINDOWTEXT);
	ListView_SetBkColor(m_lv, background);
	ListView_SetTextBkColor(m_lv, background);
	ListView_SetTextColor(m_lv, text);

	::RedrawWindow(m_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_ERASE);
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
	int marginX = GetListMarginX();
	::MoveWindow(m_lv, marginX, 0, max(0, (int)LOWORD(lParam) - 2 * marginX), HIWORD(lParam), TRUE);
	return 0;
}

// Sorts the list in the current order and shows it in the column headers: the name header reads
// "Name (mixed)" in that order, and an arrow marks the sorted column.
void FSWindow::ApplySort()
{
	ListView_SortItems(m_lv, GetCompareFunction(m_sort), 0);

	LVCOLUMN column;
	column.mask = LVCF_TEXT;
	column.pszText = m_sort == SORT_NAME_MIXED ? _T("Name (mixed)") : _T("Name");
	ListView_SetColumn(m_lv, 0, &column);

	// The arrow is drawn by ListViewSubclassProc (see DrawSortArrow), not with HDF_SORTUP/DOWN.
	m_headerState.sortedColumn = m_sort == SORT_SIZE ? 1 : 0;
	::InvalidateRect(ListView_GetHeader(m_lv), NULL, TRUE);
}

int FSWindow::GetListMarginX()
{
	return MulDiv(LIST_MARGIN_X_DIP, GetDpiForWindow(m_hWnd), 96);
}

COLORREF FSWindow::GetBackgroundColor() const
{
	return m_bDark ? DARK_BACKGROUND : GetSysColor(COLOR_WINDOW);
}

// Paint the margins on each side of the list with the list's background color, so they look
// like part of the list rather than a frame.
LRESULT FSWindow::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	RECT rc;
	GetClientRect(&rc);
	HBRUSH hBrush = CreateSolidBrush(GetBackgroundColor());
	FillRect((HDC)wParam, &rc, hBrush);
	DeleteObject(hBrush);
	return 1;
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

	// A space at the end keeps the right-aligned sizes away from the right edge of the column
	// (the list leaves almost no margin there). The header text "Size " ends with one too.
	len = wcslen(buffer);
	if (len < cch - 1)
	{
		buffer[len] = L' ';
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
			TCHAR buffer[50];
			FormatFolderInfoBuffer(items[i]->GetSize(), buffer, 50);

			// When reloading the same folder, an item that is already in the list is updated in
			// place, so the scroll position and the selection are kept.
			map<wstring, ListItem*>::iterator existing = m_nameMap.find(items[i]->GetFileName());
			if (m_bReloading && existing != m_nameMap.end())
			{
				LVFINDINFO lvfi;
				lvfi.flags = LVFI_PARAM;
				lvfi.lParam = (LPARAM)existing->second;
				int iFind = ListView_FindItem(m_lv, -1, &lvfi);
				if (iFind >= 0)
				{
					LVITEM item = {0};
					item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
					item.iItem = iFind;
					item.pszText = (LPTSTR)items[i]->GetName();
					item.iImage = items[i]->GetIcon();
					item.lParam = (LPARAM)items[i];
					ListView_SetItem(m_lv, &item);
					ListView_SetItemText(m_lv, iFind, 1, buffer);
					delete existing->second;
					existing->second = items[i];
					m_staleItems.erase(items[i]->GetFileName());
					continue;
				}
			}

			LVITEM item = {0};
			item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
			item.iItem = INT_MAX;
			item.iSubItem = 0;
			item.pszText = (LPTSTR)items[i]->GetName();
			item.iImage = items[i]->GetIcon();
			item.lParam = (LPARAM)items[i];
			item.iItem = ListView_InsertItem(m_lv, &item);
			ListView_SetItemText(m_lv, item.iItem, 1, buffer);

			m_nameMap[items[i]->GetFileName()] = items[i];
		}
		ListView_SortItems(m_lv, GetCompareFunction(m_sort), 0);
		AdjustSizeForList();
	}
	return 0;
}

// The scanner has listed the whole folder. After a reload, remove the items that are gone.
LRESULT FSWindow::OnEnumDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	if ((UINT)wParam != m_scanGeneration)
		return 0;  // from a scanner that was replaced

	// Take the last items first, in case their WM_NEWITEMS hasn't been handled yet.
	BOOL b;
	OnNewItems(WM_NEWITEMS, 0, 0, b);

	if (m_bReloading)
	{
		for (set<wstring>::iterator itr = m_staleItems.begin(); itr != m_staleItems.end(); itr++)
		{
			map<wstring, ListItem*>::iterator gone = m_nameMap.find(*itr);
			if (gone == m_nameMap.end())
				continue;
			LVFINDINFO lvfi;
			lvfi.flags = LVFI_PARAM;
			lvfi.lParam = (LPARAM)gone->second;
			int iFind = ListView_FindItem(m_lv, -1, &lvfi);
			if (iFind >= 0)
				ListView_DeleteItem(m_lv, iFind);
			delete gone->second;
			m_nameMap.erase(gone);
		}
		m_staleItems.clear();
		m_bReloading = false;
		AdjustSizeForList();
	}
	return 0;
}

void FSWindow::StartScanner()
{
	m_pScanner = new FolderViewScanner(m_pWebBrowser, m_hWnd, ++m_scanGeneration);
}

// Reads the folder again after a change (file created, deleted, renamed, moved to the Recycle
// Bin...). Items are matched by file name: existing ones are updated in place, new ones are
// added, and the ones not found again are removed when the scan is done (OnEnumDone).
void FSWindow::Reload()
{
	if (m_pScanner)
		m_pScanner->Quit();
	m_bReloading = true;
	m_staleItems.clear();
	for (map<wstring, ListItem*>::iterator itr = m_nameMap.begin(); itr != m_nameMap.end(); itr++)
		m_staleItems.insert(itr->first);
	StartScanner();
}

// Returns the folder currently shown in a File Explorer tab (to free with CoTaskMemFree).
static LPITEMIDLIST GetBrowserFolder(IWebBrowser2* pWebBrowser)
{
	LPITEMIDLIST pidl = NULL;
	CComQIPtr<IServiceProvider> psp(pWebBrowser);
	CComPtr<IShellBrowser> psb;
	CComPtr<IShellView> psv;
	CComPtr<IFolderView> pfv;
	CComPtr<IPersistFolder2> ppf2;
	if (psp && SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb))
		&& SUCCEEDED(psb->QueryActiveShellView(&psv))
		&& SUCCEEDED(psv->QueryInterface(IID_IFolderView, (void**)&pfv))
		&& SUCCEEDED(pfv->GetFolder(IID_IPersistFolder2, (void**)&ppf2)))
	{
		ppf2->GetCurFolder(&pidl);
	}
	return pidl;
}

// Asks Windows to report changes in the folder shown (only that folder, not its subfolders).
// Sizes of subfolders are kept up to date by the service; this is for items added, removed or
// renamed in the folder itself.
void FSWindow::WatchFolderChanges()
{
	StopWatchingFolderChanges();
	LPITEMIDLIST pidl = GetBrowserFolder(m_pWebBrowser);
	if (!pidl)
		return;
	SHChangeNotifyEntry entry = { pidl, FALSE };
	m_changeNotifyId = SHChangeNotifyRegister(m_hWnd, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
		SHCNE_CREATE | SHCNE_DELETE | SHCNE_MKDIR | SHCNE_RMDIR | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER |
		SHCNE_UPDATEITEM | SHCNE_UPDATEDIR,
		WM_SHELLCHANGE, 1, &entry);
	CoTaskMemFree(pidl);
}

void FSWindow::StopWatchingFolderChanges()
{
	if (m_changeNotifyId)
	{
		SHChangeNotifyDeregister(m_changeNotifyId);
		m_changeNotifyId = 0;
	}
}

LRESULT FSWindow::OnShellChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// With SHCNRF_NewDelivery, the notification must be locked and unlocked. Its details aren't
	// needed: the folder is read again, after a short delay to group bursts of changes.
	PIDLIST_ABSOLUTE* ppidl;
	LONG lEvent;
	HANDLE hLock = SHChangeNotification_Lock((HANDLE)wParam, (DWORD)lParam, &ppidl, &lEvent);
	if (hLock)
		SHChangeNotification_Unlock(hLock);
	SetTimer(RELOAD_TIMER_ID, RELOAD_DELAY_MS);
	return 0;
}

LRESULT FSWindow::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	if (wParam != RELOAD_TIMER_ID)
	{
		bHandled = FALSE;
		return 0;
	}
	KillTimer(RELOAD_TIMER_ID);
	Reload();
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
		if (m_sort == SORT_SIZE)
			ListView_SortItems(m_lv, GetCompareFunction(m_sort), 0);
		AdjustSizeForList();
	}
	return 0;
}

LRESULT FSWindow::OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	::PostMessage(g_hwndMain, WM_KILLWINDOW, 0, (LPARAM)this);
	return 0;
}

// Sort orders of the list (see SORT_* in Settings.h). The window opens with the default sort
// chosen in the control panel. Clicking the name header switches between "Name" and
// "Name (mixed)"; clicking the size header sorts by size. Names sort A to Z and sizes largest
// first; the other directions aren't offered. Names are compared as File Explorer does
// (StrCmpLogicalW: case-insensitive, numbers in numeric order, "Disc 2" before "Disc 10").
static int CompareNames(ListItem* p1, ListItem* p2)
{
	return StrCmpLogicalW(p1->GetName(), p2->GetName());
}

// "Name": folders first, then files, as File Explorer does.
int CALLBACK compareNameFoldersFirst(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	ListItem* p1 = (ListItem*)lParam1;
	ListItem* p2 = (ListItem*)lParam2;
	if (p1->IsFolder() != p2->IsFolder())
		return p1->IsFolder() ? -1 : 1;
	return CompareNames(p1, p2);
}

// "Name (mixed)": folders and files mixed, so "Games" and "Games.zip" are next to each other.
// With the same name, the folder comes first.
int CALLBACK compareNameMixed(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	ListItem* p1 = (ListItem*)lParam1;
	ListItem* p2 = (ListItem*)lParam2;
	int result = CompareNames(p1, p2);
	if (result == 0 && p1->IsFolder() != p2->IsFolder())
		result = p1->IsFolder() ? -1 : 1;
	return result;
}

// "Size": largest first, then by name.
int CALLBACK compareSize(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	ULONGLONG size1 = ((ListItem*)lParam1)->GetSize().nLogicalSize;
	ULONGLONG size2 = ((ListItem*)lParam2)->GetSize().nLogicalSize;
	if (size1 != size2)
		return size1 > size2 ? -1 : 1;
	return compareNameMixed(lParam1, lParam2, lParamSort);
}

static PFNLVCOMPARE GetCompareFunction(int sort)
{
	switch (sort)
	{
	case SORT_NAME_MIXED: return compareNameMixed;
	case SORT_SIZE:       return compareSize;
	default:              return compareNameFoldersFirst;
	}
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
	if (pnmlv->iSubItem == 0)
	{
		// The name header switches between the two name orders. Coming from the size order,
		// it goes back to the default name order.
		if (m_sort == SORT_NAME)
			m_sort = SORT_NAME_MIXED;
		else if (m_sort == SORT_NAME_MIXED)
			m_sort = SORT_NAME;
		else
			m_sort = LoadDefaultSort() == SORT_NAME_MIXED ? SORT_NAME_MIXED : SORT_NAME;
	}
	else if (pnmlv->iSubItem == 1)
	{
		m_sort = SORT_SIZE;
	}
	ApplySort();
	AdjustSizeForList();
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

	// Use the measurements for the DPI of the monitor the window is on.
	UINT dpi = GetDpiForWindow(m_hWnd);

	ListView_SetColumnWidth(m_lv, 0, LVSCW_AUTOSIZE);
	ListView_SetColumnWidth(m_lv, 1, LVSCW_AUTOSIZE);
	ListView_SetColumnWidth(m_lv, 1, ListView_GetColumnWidth(m_lv, 1) + MulDiv(SIZE_COLUMN_EXTRA_DIP, dpi, 96));

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
	// rc is the list; the client area also holds the margins on each side of it.
	int marginX = MulDiv(LIST_MARGIN_X_DIP, dpi, 96);
	rc.left -= marginX;
	rc.right += marginX;
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
_ATL_FUNC_INFO FSWindow::FileListEnumDoneInfo = { CC_STDCALL, VT_EMPTY, 0, NULL };

// File Explorer fills its view of a folder in the background, so when the window reads the
// folder right after a navigation, the view may still be empty or incomplete (for example the
// root of a drive). Explorer's view reports when it has listed the whole folder (EnumDone);
// read the folder again then. The update is done in place (see Reload).
void FSWindow::WatchFolderView()
{
	StopWatchingFolderView();
	CComQIPtr<IServiceProvider> psp(m_pWebBrowser);
	CComPtr<IShellBrowser> psb;
	CComPtr<IShellView> psv;
	if (psp && SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb))
		&& SUCCEEDED(psb->QueryActiveShellView(&psv))
		&& SUCCEEDED(psv->GetItemObject(SVGIO_BACKGROUND, IID_IDispatch, (void**)&m_pFolderView)))
	{
		if (FAILED(FolderViewEvents::DispEventAdvise(m_pFolderView)))
			m_pFolderView.Release();
	}
}

void FSWindow::StopWatchingFolderView()
{
	if (m_pFolderView)
	{
		FolderViewEvents::DispEventUnadvise(m_pFolderView);
		m_pFolderView.Release();
	}
}

void FSWindow::OnFileListEnumDone()
{
	if (m_hWnd)
		Reload();
}

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
		KillTimer(RELOAD_TIMER_ID);
		m_bReloading = false;
		m_staleItems.clear();

		// the notification from the control will clean up the old items
		ListView_DeleteAllItems(m_lv);

		AdjustSizeForList();

		StartScanner();
		WatchFolderChanges();
		WatchFolderView();
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
	void StartPortableScanner();
	void WatchTabs(HWND hwndExplorer);
	static void CALLBACK TabEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime);

	HICON m_icon;
	HICON m_gray;
	// Portable mode: job holding the scanner process started by this program (see
	// StartPortableScanner). Closing it ends the scanner. NULL when the service is installed.
	HANDLE m_hScannerJob;
	CComPtr<IShellWindows> m_pShellWindows;
	set<FSWindow*> m_FSWindows;
	// One event hook per process that hosts File Explorer windows, to see tabs being switched.
	map<DWORD, HWINEVENTHOOK> m_tabHooks;
	bool m_bAutoRun;
};

_ATL_FUNC_INFO ShellWindowManager::ShellWindowsEventInfo = { CC_STDCALL, VT_EMPTY, 1, {VT_I4} };

ShellWindowManager::ShellWindowManager() : m_icon(NULL), m_gray(NULL), m_hScannerJob(NULL)
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

	StartPortableScanner();
	Activate();

	return 0;
}

LRESULT ShellWindowManager::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	// Portable mode: closing the job ends the scanner process.
	if (m_hScannerJob)
	{
		CloseHandle(m_hScannerJob);
		m_hScannerJob = NULL;
	}

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
				if (RegQueryValueEx(hKey, AUTORUN_VALUE_NAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
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

// True when the Folder Size service is installed (it may be stopped). If the service manager
// can't be asked, assume it is installed, so a second scanner is never started by mistake.
static bool IsServiceInstalled()
{
	SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if (!hSCM)
		return true;
	bool bInstalled = true;
	SC_HANDLE hService = OpenService(hSCM, _T("FolderSize"), SERVICE_QUERY_STATUS);
	if (hService)
		CloseServiceHandle(hService);
	else if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
		bInstalled = false;
	CloseServiceHandle(hSCM);
	return bInstalled;
}

// Portable mode (programs copied without the installer): there is no service to scan folders,
// so run FolderSizeSvc.exe from this program's folder as an ordinary background process, with
// its -debug option (no service, no administrator rights needed). It is put in a job that ends
// it when this program exits, even if this program crashes. In this mode the cache is kept in
// memory only while this program runs, and all drive types are scanned.
void ShellWindowManager::StartPortableScanner()
{
	if (m_hScannerJob || IsServiceInstalled())
		return;

	TCHAR szExe[MAX_PATH];
	DWORD dw = GetModuleFileName(NULL, szExe, MAX_PATH);
	if (dw == 0 || dw >= MAX_PATH)
		return;
	PathRemoveFileSpec(szExe);
	if (!PathAppend(szExe, _T("FolderSizeSvc.exe")) || !PathFileExists(szExe))
		return;

	HANDLE hJob = CreateJobObject(NULL, NULL);
	if (!hJob)
		return;
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
	jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

	TCHAR szCmd[MAX_PATH + 16];
	wsprintf(szCmd, _T("\"%s\" -debug"), szExe);
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	if (!CreateProcess(szExe, szCmd, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
	{
		CloseHandle(hJob);
		return;
	}
	if (AssignProcessToJobObject(hJob, pi.hProcess))
	{
		ResumeThread(pi.hThread);
		m_hScannerJob = hJob;
	}
	else
	{
		TerminateProcess(pi.hProcess, 0);
		CloseHandle(hJob);
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
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
	SHSetValue(HKEY_CURRENT_USER, REG_KEY_PATH, AUTORUN_VALUE_NAME, REG_SZ, szFile, dw * sizeof(WCHAR));
}

static void DeleteAutoRun()
{
	SHDeleteValue(HKEY_CURRENT_USER, REG_KEY_PATH, AUTORUN_VALUE_NAME);
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