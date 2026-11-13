#include "StdAfx.h"
#include "Settings.h"


bool LoadDWord(HKEY hKey, LPCTSTR lpValueName, DWORD& dw)
{
	DWORD dwType, dwData, cbData;
	cbData = sizeof(DWORD);
	if (RegQueryValueEx(hKey, lpValueName, NULL, &dwType, (LPBYTE)&dwData, &cbData) == ERROR_SUCCESS)
	{
		if (dwType == REG_DWORD)
		{
			dw = dwData;
			return true;
		}
	}
	return false;
}

void SaveDWord(HKEY hKey, LPCTSTR lpValueName, DWORD dw)
{
	RegSetValueEx(hKey, lpValueName, 0, REG_DWORD, (CONST BYTE*)&dw, sizeof(DWORD));
}

std::wstring LoadLanguageSetting()
{
	WCHAR szCode[32] = L"";
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, USER_SETTINGS_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		DWORD dwType, cb = sizeof(szCode) - sizeof(WCHAR);  // keep room for a terminating null
		if (RegQueryValueExW(hKey, L"Language", NULL, &dwType, (LPBYTE)szCode, &cb) != ERROR_SUCCESS || dwType != REG_SZ)
			szCode[0] = L'\0';
		RegCloseKey(hKey);
	}
	return szCode;
}

void SaveLanguageSetting(const std::wstring& code)
{
	HKEY hKey;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, USER_SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
	{
		RegSetValueExW(hKey, L"Language", 0, REG_SZ, (const BYTE*)code.c_str(), (DWORD)((code.size() + 1) * sizeof(WCHAR)));
		RegCloseKey(hKey);
	}
}

int LoadDefaultSort()
{
	int sort = SORT_NAME;
	HKEY hKey;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, USER_SETTINGS_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		DWORD dw;
		if (LoadDWord(hKey, TEXT("DefaultSort"), dw) && dw <= SORT_SIZE)
			sort = (int)dw;
		RegCloseKey(hKey);
	}
	return sort;
}

int LoadMaxWindowWidth()
{
	int width = MAX_WINDOW_WIDTH_DEFAULT;
	HKEY hKey;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, USER_SETTINGS_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		DWORD dw;
		if (LoadDWord(hKey, TEXT("MaxWindowWidth"), dw) && dw >= MAX_WINDOW_WIDTH_MIN && dw <= MAX_WINDOW_WIDTH_MAX)
			width = (int)dw;
		RegCloseKey(hKey);
	}
	return width;
}

void SaveMaxWindowWidth(int width)
{
	HKEY hKey;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, USER_SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
	{
		SaveDWord(hKey, TEXT("MaxWindowWidth"), (DWORD)width);
		RegCloseKey(hKey);
	}
}

void SaveDefaultSort(int sort)
{
	HKEY hKey;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, USER_SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
	{
		SaveDWord(hKey, TEXT("DefaultSort"), (DWORD)sort);
		RegCloseKey(hKey);
	}
}

int LoadScanDriveTypes()
{
	// they're all on by default, but a 0 in the registry will turn off each type
	int DriveTypes = SCANDRIVETYPE_LOCAL | SCANDRIVETYPE_CD | SCANDRIVETYPE_REMOVABLE | SCANDRIVETYPE_NETWORK;

	HKEY hKey;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, SERVICE_PARAMETERS_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		DWORD dw;
		if (LoadDWord(hKey, TEXT("ScanLocal"), dw) && dw == 0)
			DriveTypes &= ~SCANDRIVETYPE_LOCAL;
		if (LoadDWord(hKey, TEXT("ScanCD"), dw) && dw == 0)
			DriveTypes &= ~SCANDRIVETYPE_CD;
		if (LoadDWord(hKey, TEXT("ScanRemovable"), dw) && dw == 0)
			DriveTypes &= ~SCANDRIVETYPE_REMOVABLE;
		if (LoadDWord(hKey, TEXT("ScanNetwork"), dw) && dw == 0)
			DriveTypes &= ~SCANDRIVETYPE_NETWORK;

		RegCloseKey(hKey);
	}

	return DriveTypes;
}

LONG SaveScanDriveTypes(int DriveTypes)
{
	HKEY hKey;
	LONG error = RegCreateKeyEx(HKEY_LOCAL_MACHINE, SERVICE_PARAMETERS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
	if (error != ERROR_SUCCESS)
		return error;

	SaveDWord(hKey, TEXT("ScanLocal"), DriveTypes & SCANDRIVETYPE_LOCAL ? 1 : 0);
	SaveDWord(hKey, TEXT("ScanCD"), DriveTypes & SCANDRIVETYPE_CD ? 1 : 0);
	SaveDWord(hKey, TEXT("ScanRemovable"), DriveTypes & SCANDRIVETYPE_REMOVABLE ? 1 : 0);
	SaveDWord(hKey, TEXT("ScanNetwork"), DriveTypes & SCANDRIVETYPE_NETWORK ? 1 : 0);
	RegCloseKey(hKey);

	return ERROR_SUCCESS;
}
