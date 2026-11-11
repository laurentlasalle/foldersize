#include "StdAfx.h"
#include "Language.h"
#include "Settings.h"
#include "LangList.h"

// Module (exe or cpl) whose resources hold the string tables: the one this code is linked in.
extern "C" IMAGE_DOS_HEADER __ImageBase;
#define THIS_MODULE ((HMODULE)&__ImageBase)

static LANGID g_uiLanguage = 0;

int GetLanguageCount()
{
	return (int)(sizeof(g_languages) / sizeof(g_languages[0]));
}

const LanguageInfo& GetLanguageInfo(int index)
{
	return g_languages[index];
}

// Picks the available language closest to a Windows language: the same language and region,
// else the same language (Chinese: traditional for Taiwan, Hong Kong and Macao), else English.
static LANGID MatchLanguage(LANGID wanted)
{
	for (int i = 0; i < GetLanguageCount(); i++)
		if (g_languages[i].id == wanted)
			return wanted;
	WORD primary = PRIMARYLANGID(wanted);
	if (primary == LANG_CHINESE)
	{
		WORD sub = SUBLANGID(wanted);
		bool traditional = sub == SUBLANG_CHINESE_TRADITIONAL || sub == SUBLANG_CHINESE_HONGKONG || sub == SUBLANG_CHINESE_MACAU;
		return MAKELANGID(LANG_CHINESE, traditional ? SUBLANG_CHINESE_TRADITIONAL : SUBLANG_CHINESE_SIMPLIFIED);
	}
	for (int i = 0; i < GetLanguageCount(); i++)
		if (PRIMARYLANGID(g_languages[i].id) == primary)
			return g_languages[i].id;
	return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
}

void RefreshUiLanguage()
{
	// The setting holds a language code ("fr"); empty means the language of Windows.
	std::wstring code = LoadLanguageSetting();
	for (int i = 0; i < GetLanguageCount(); i++)
	{
		if (!code.empty() && code == g_languages[i].code)
		{
			g_uiLanguage = g_languages[i].id;
			return;
		}
	}
	g_uiLanguage = MatchLanguage(GetUserDefaultUILanguage());
}

LANGID GetUiLanguage()
{
	if (g_uiLanguage == 0)
		RefreshUiLanguage();
	return g_uiLanguage;
}

// Reads a text from the string table of one language. String tables are stored in blocks of
// 16 texts; each text is its length followed by its characters (not null-terminated).
static bool LoadStringForLanguage(UINT id, LANGID language, std::wstring& text)
{
	HRSRC hRes = FindResourceEx(THIS_MODULE, RT_STRING, MAKEINTRESOURCE((id >> 4) + 1), language);
	if (!hRes)
		return false;
	const WCHAR* p = (const WCHAR*)LockResource(LoadResource(THIS_MODULE, hRes));
	if (!p)
		return false;
	for (UINT i = 0; i < (id & 15); i++)
		p += 1 + *p;
	if (*p == 0)
		return false;
	text.assign(p + 1, *p);
	return true;
}

std::wstring Tr(UINT id)
{
	std::wstring text;
	if (!LoadStringForLanguage(id, GetUiLanguage(), text))
		LoadStringForLanguage(id, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), text);
	return text;
}
