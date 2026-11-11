#!/usr/bin/env python3
"""Builds the translations of Folder Size Redux.

The texts of the program are translated with gettext files, as in many other projects:

  Lang/folder-size-redux.pot   the English texts (template)
  Lang/<language>.po           one translation per language (edit with Poedit or any .po editor)

Windows programs can't read .po files, so this script converts them into Windows resources,
which are compiled into the programs:

  Common/LangStrings.h   numeric IDs of the texts (STR_...), used by the C++ code
  Common/LangStrings.rc  one string table per language, included by the .rc file of each program
  Common/LangList.h      list of the available languages, for the Language setting

Run it after changing a .po or .pot file, and commit the generated files with them:

  python FolderSize/Lang/build_lang.py

The build on GitHub runs it with --check, which fails if the generated files are out of date.

Missing, empty or fuzzy translations are replaced by the English text, so every language has
every text.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
COMMON = os.path.join(HERE, '..', 'Common')
TEMPLATE = 'folder-size-redux.pot'
FIRST_ID = 1000

# Language code (name of the .po file) -> Windows language and sublanguage, and the name of the
# language in that language (shown in the Language setting). Add a line here for a new language.
LANGUAGES = [
    ('en',    'LANG_ENGLISH',   'SUBLANG_ENGLISH_US',          'English'),
    ('ca',    'LANG_CATALAN',   'SUBLANG_CATALAN_CATALAN',     'Català'),
    ('de',    'LANG_GERMAN',    'SUBLANG_GERMAN',              'Deutsch'),
    ('es',    'LANG_SPANISH',   'SUBLANG_SPANISH_MODERN',      'Español'),
    ('fr',    'LANG_FRENCH',    'SUBLANG_FRENCH',              'Français'),
    ('hu',    'LANG_HUNGARIAN', 'SUBLANG_HUNGARIAN_HUNGARY',   'Magyar'),
    ('it',    'LANG_ITALIAN',   'SUBLANG_ITALIAN',             'Italiano'),
    ('ja',    'LANG_JAPANESE',  'SUBLANG_JAPANESE_JAPAN',      '日本語'),
    ('nl',    'LANG_DUTCH',     'SUBLANG_DUTCH',               'Nederlands'),
    ('pl',    'LANG_POLISH',    'SUBLANG_POLISH_POLAND',       'Polski'),
    ('ru',    'LANG_RUSSIAN',   'SUBLANG_RUSSIAN_RUSSIA',      'Русский'),
    ('sv',    'LANG_SWEDISH',   'SUBLANG_SWEDISH',             'Svenska'),
    ('zh-cn', 'LANG_CHINESE',   'SUBLANG_CHINESE_SIMPLIFIED',  '简体中文'),
    ('zh-tw', 'LANG_CHINESE',   'SUBLANG_CHINESE_TRADITIONAL', '繁體中文'),
]


def unescape_po(s):
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            n = s[i + 1]
            out.append({'n': '\n', 't': '\t', '\\': '\\', '"': '"'}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def read_po(path):
    """Returns the entries of a .po file: dicts with ctxt, id, str and fuzzy. Entries are
    separated by blank lines. The header entry (empty msgid) is skipped."""
    with open(path, encoding='utf-8') as f:
        blocks = re.split(r'\n\s*\n', f.read())
    entries = []
    for block in blocks:
        entry = {'fuzzy': False}
        field = None
        for raw in block.split('\n'):
            line = raw.strip()
            if not line:
                continue
            if line.startswith('#,'):
                entry['fuzzy'] = 'fuzzy' in line
                continue
            if line.startswith('#'):
                continue
            m = re.match(r'(msgctxt|msgid|msgstr)\s+"(.*)"$', line)
            if m:
                field = {'msgctxt': 'ctxt', 'msgid': 'id', 'msgstr': 'str'}[m.group(1)]
                entry[field] = unescape_po(m.group(2))
                continue
            m = re.match(r'"(.*)"$', line)
            if m and field:
                entry[field] += unescape_po(m.group(1))
                continue
            raise SystemExit('%s: cannot read line: %s' % (path, raw))
        if entry.get('id'):
            entries.append(entry)
    return entries


def rc_string(s):
    s = s.replace('\\', '\\\\').replace('"', '""').replace('\n', '\\n').replace('\t', '\\t')
    return '"' + s + '"'


def c_wide_string(s):
    """C++ wide string literal using only ASCII: other characters become \\x escapes, each in
    its own literal (a \\x escape would otherwise also take the hexadecimal digits after it)."""
    parts = []
    plain = ''
    for ch in s:
        if 32 <= ord(ch) < 127 and ch not in '\\"':
            plain += ch
        else:
            if plain:
                parts.append('L"%s"' % plain)
                plain = ''
            parts.append('L"\\x%04x"' % ord(ch))
    if plain or not parts:
        parts.append('L"%s"' % plain)
    return ' '.join(parts)


def macro(ctxt):
    return 'STR_' + re.sub(r'[^A-Za-z0-9]', '_', ctxt).upper()


def generate():
    template = read_po(os.path.join(HERE, TEMPLATE))
    keys = [e['ctxt'] for e in template]
    english = {e['ctxt']: e['id'] for e in template}
    if len(set(keys)) != len(keys):
        raise SystemExit('duplicate msgctxt in the template')

    header = ['// Generated by Lang/build_lang.py from Lang/folder-size-redux.pot. Do not edit.',
              '#pragma once', '']
    for n, key in enumerate(keys):
        header.append('#define %-40s %d  // %s' % (macro(key), FIRST_ID + n, english[key].replace('\n', ' ')))
    header.append('')

    rc = ['// Generated by Lang/build_lang.py from the .po files in Lang. Do not edit.',
          '// One string table per language; texts that are not translated are in English.',
          '#pragma code_page(65001)', '']
    langlist = ['// Generated by Lang/build_lang.py. Do not edit.',
                '// Languages available for the Language setting: code, Windows language, name.',
                'static const LanguageInfo g_languages[] =', '{']
    report = []
    for code, lang, sublang, native in LANGUAGES:
        translated = {}
        if code != 'en':
            path = os.path.join(HERE, code + '.po')
            if not os.path.exists(path):
                raise SystemExit('missing ' + path)
            for e in read_po(path):
                if e.get('ctxt') in english and e.get('str') and not e['fuzzy']:
                    if e['id'] != english[e['ctxt']]:
                        continue  # translation of an older English text: treat as untranslated
                    translated[e['ctxt']] = e['str']
        rc.append('LANGUAGE %s, %s' % (lang, sublang))
        rc.append('STRINGTABLE')
        rc.append('BEGIN')
        for key in keys:
            rc.append('    %-40s %s' % (macro(key), rc_string(translated.get(key, english[key]))))
        rc.append('END')
        rc.append('')
        langlist.append('    { L"%s", MAKELANGID(%s, %s), %s },' % (code, lang, sublang, c_wide_string(native)))
        if code != 'en':
            report.append('%s %d/%d' % (code, len(translated), len(keys)))
    langlist.append('};')
    langlist.append('')

    return {
        os.path.join(COMMON, 'LangStrings.h'): '\n'.join(header),
        os.path.join(COMMON, 'LangStrings.rc'): '\n'.join(rc),
        os.path.join(COMMON, 'LangList.h'): '\n'.join(langlist),
    }, report


def main():
    check = '--check' in sys.argv
    files, report = generate()
    stale = []
    for path, text in files.items():
        data = text.replace('\n', '\r\n').encode('utf-8')
        old = open(path, 'rb').read() if os.path.exists(path) else None
        # compare without line endings: Git may check the files out with LF or CRLF
        if old is None or old.replace(b'\r\n', b'\n') != data.replace(b'\r\n', b'\n'):
            stale.append(os.path.relpath(path))
            if not check:
                with open(path, 'wb') as f:
                    f.write(data)
    if check and stale:
        raise SystemExit('Out of date, run Lang/build_lang.py and commit: ' + ', '.join(stale))
    print('Translated texts: ' + ', '.join(report))


if __name__ == '__main__':
    main()
