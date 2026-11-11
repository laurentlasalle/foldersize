@echo off
rem Create or overwrite the MSI from the tables

msidb -c -d FolderSize.msi -f "%cd%\tables" *

rem Create a CAB containing the three FolderSize files

cabarc -m lzx:21 n FolderSize.cab ..\release\FolderSize.cpl ..\release\FolderSizeSvc.exe ..\release\FolderSize.exe

rem Add the CAB to the MSI as a stream

msidb -d FolderSize.msi -a FolderSize.cab

rem Delete the CAB

del FolderSize.cab