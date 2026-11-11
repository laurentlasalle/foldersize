#pragma once

// Translated texts of the user interface.
//
// The texts come from the .po files in Lang (one per language), converted by Lang/build_lang.py
// into a string table per language, compiled into each program (LangStrings.rc). The IDs of the
// texts are the STR_... constants of LangStrings.h.
//
// The language is the one chosen in the settings (Language), or by default the language of
// Windows when it is available, else English.

#include <string>
#include "LangStrings.h"

struct LanguageInfo
{
	const wchar_t* code;   // name of the .po file: "fr", "zh-cn"...
	LANGID id;             // Windows language of its string table
	const wchar_t* name;   // name of the language in that language: "Français"
};

// Languages available, in the order of the Language setting (English first).
int GetLanguageCount();
const LanguageInfo& GetLanguageInfo(int index);

// Language used for the texts. Read from the settings the first time, and again after
// RefreshUiLanguage (call it when the settings change).
LANGID GetUiLanguage();
void RefreshUiLanguage();

// Text in the user interface language.
std::wstring Tr(UINT id);

// Registered window message sent by the control panel when settings are applied, so the
// running window can update (for example to change language). It is posted to the main
// (message-only) window of FolderSize.exe, found by its class name, which passes it on to the
// size windows. A broadcast can't be used: it doesn't reach owned windows, and the size windows
// are owned by File Explorer windows.
#define SETTINGS_CHANGED_MESSAGE L"FolderSizeRedux.SettingsChanged"
#define MAIN_WINDOW_CLASS L"FolderSizeShellWindow"  // SHELL_WINDOW_CLASS_NAME in Window/main.cpp
