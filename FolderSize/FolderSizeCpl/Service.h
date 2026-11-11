#pragma once

enum MODIFY_SERVICE
{
	MS_START,
	MS_STOP,
	MS_PAUSE,
	MS_CONTINUE,
	MS_PARAMCHANGE,
	MS_MAX
};

DWORD ModifyService(MODIFY_SERVICE ms);

DWORD GetServiceStatus();

// Saves the drive types to scan (an administrator's registry location) and tells the running
// service to read them again. Needs administrator rights.
DWORD SaveDriveTypesAndTellService(int DriveTypes);
