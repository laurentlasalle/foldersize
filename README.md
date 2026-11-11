# Folder Size Redux

Folder Size Redux shows the size of folders and files as you browse them in
File Explorer. When you open a folder, a small companion window appears next
to the Explorer window and lists the size of every folder and file in it. A
background service scans your drives and caches the results, so sizes are
available right away.

Folder Size Redux is a fork of [Folder Size for Windows](https://sourceforge.net/projects/foldersize/)
2.6 by Brio, which has not been updated since 2013. It focuses on Windows 10
and 11: File Explorer tabs, high-DPI screens, dark mode and bug fixes.

Current version: 0.1 (early testing).

## Design goals

Folder Size Redux keeps the original goal: lightweight and designed for
performance. The background service should stay invisible in terms of CPU,
memory and disk usage, and the window should appear instantly. Every change
is weighed against this goal: no heavy frameworks or runtimes, no features
that cost more than they bring.

## Installing

Download `FolderSize64.msi` from the Artifacts section of the latest run on
the [Actions page](https://github.com/laurentlasalle/foldersize/actions) and
run it. It requires the 64-bit edition of Windows 10 or 11. If Folder Size
for Windows 2.6 is installed, the installer replaces it.

The settings are available from the menu of the notification area icon
(Settings...) and from Control Panel.

Controlling the service (Start, Stop, Pause, Resume on the Service tab) and
changing the drive types to scan need administrator rights. The settings
panel runs with the rights of the user, so these actions show the shield
and Windows asks for approval (UAC) when they are used. The approval prompt
names Windows host process (Rundll32), which runs the action from
`FolderSize.cpl`. The other settings don't need administrator rights.

### Portable use

The programs can also run without the installer: copy `FolderSize.exe`,
`FolderSizeSvc.exe` and `FolderSize.cpl` to the same folder and run
`FolderSize.exe`. When the Folder Size service is not installed, it starts
`FolderSizeSvc.exe` as an ordinary background process (no administrator
rights needed) and ends it when it exits. In this mode:

- folder sizes are cached only while `FolderSize.exe` runs, so the sizes of
  large folders take a moment to appear after it starts;
- all drive types are scanned (choosing drive types requires the installed
  service);
- Settings... opens the control panel from its file; it doesn't appear in
  Control Panel.

## History

The full CVS history of the original project (2005–2013) was imported from
SourceForge's read-only CVS snapshot on 2026-11-10. Each CVS change became a
git commit with its original author, date and message. Original
contributors include Brio, Kirill Müller and eisber.

## Building

The project is written in C++ and builds with Visual Studio 2022 for x64.
Open `FolderSize/FolderSize.sln` and build the Release x64 configuration.
The files are written to `FolderSize/Release/x64`. To create the
installer, run `FolderSize/MSI/BuildMSI64.bat` after building.

Every push is also built on GitHub Actions. The installer
(`FolderSize64.msi`) and the program files can be downloaded from the
run's Artifacts section on the Actions page.

## Translating

The texts of the program are translated with gettext files, like many other projects:
`FolderSize/Lang/folder-size-redux.pot` holds the English texts, and each language has its
own `.po` file next to it (`fr.po`, `de.po`, and so on). Edit them with
[Poedit](https://poedit.net) or any `.po` editor. To add a language, copy the `.pot` file to
`<language>.po` and add the language to the list at the top of `build_lang.py`.

Windows programs can't read `.po` files directly, so `FolderSize/Lang/build_lang.py` converts
them into Windows string tables (`FolderSize/Common/LangStrings.*`), compiled into the programs.
Run it after changing a `.po` or `.pot` file and commit the generated files along with it:

    python FolderSize/Lang/build_lang.py

The build on GitHub checks that the generated files are up to date. Texts that are missing or
marked fuzzy in a `.po` file are shown in English.

By default the program uses the language of Windows when it is available, and English
otherwise. The Language setting in the General tab of the settings chooses another language.

## License

Folder Size Redux is free software, released under the GNU General Public
License, version 2. See `COPYING`. Folder Size for Windows is copyright
Brio and its contributors; the changes made in Folder Size Redux are
copyright Laurent LaSalle and released under the same license.
