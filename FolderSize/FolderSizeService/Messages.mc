; // Message table of the Folder Size service, for the Application event log.
; //
; // The service writes its events with ReportEvent and event ID 0, passing the whole text of the
; // message as the only insertion string (see EventLog.cpp). Windows looks up the event ID in
; // the message table of the file registered as EventMessageFile for the event source
; // "FolderSize" (by the installer, or by FolderSizeSvc.exe -install); without it, Event Viewer
; // and PowerShell can't show the text. This table has a single message, ID 0, which shows the
; // insertion string as is.
; //
; // It is compiled with mc.exe during the build (see FolderSizeService.vcxproj) into
; // Messages.rc and MSG00409.bin, which are added to the resources of FolderSizeSvc.exe.

MessageIdTypedef=DWORD

SeverityNames=(Success=0x0:STATUS_SEVERITY_SUCCESS)
FacilityNames=(None=0x0:FACILITY_NONE)
LanguageNames=(English=0x409:MSG00409)

MessageId=0x0
Severity=Success
Facility=None
SymbolicName=MSG_TEXT
Language=English
%1
.
