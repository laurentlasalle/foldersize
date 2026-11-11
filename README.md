# Folder Size for Windows

Folder Size for Windows shows the size of folders and files as you browse
them in File Explorer. When you open a folder, a small companion window
appears next to the Explorer window and lists the size of every folder and
file in it. A background service scans your drives and caches the results.

This repository is a continuation of the original project by Brio
(https://sourceforge.net/projects/foldersize/), focused on Windows 10
and 11. The goal is to fix bugs and keep it working on current versions
of Windows.

## History

The full CVS history of the original project (2005–2013) was imported from
SourceForge's read-only CVS snapshot on 2026-11-10. Each CVS change became a
git commit with its original author, date and message. Original
contributors include Brio, Kirill Müller and eisber.

## Building

The project is written in C++. Open `FolderSize/FolderSize.sln` in
Visual Studio. The solution was last saved with Visual Studio 2005, so it
still needs to be upgraded to a current version of Visual Studio.

## License

Folder Size for Windows is free software, released under the
GNU General Public License, version 2. See `COPYING`.
The original copyright belongs to its authors; changes in this
repository are released under the same license.
