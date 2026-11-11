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

## License

Folder Size Redux is free software, released under the GNU General Public
License, version 2. See `COPYING`. Folder Size for Windows is copyright
Brio and its contributors; the changes made in Folder Size Redux are
copyright Laurent LaSalle and released under the same license.
