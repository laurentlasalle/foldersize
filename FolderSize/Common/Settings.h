#define SERVICE_PARAMETERS_KEY   TEXT("SYSTEM\\CurrentControlSet\\Services\\FolderSize\\Parameters")


#define SCANDRIVETYPE_LOCAL        0x01
#define SCANDRIVETYPE_CD           0x02
#define SCANDRIVETYPE_REMOVABLE    0x04
#define SCANDRIVETYPE_NETWORK      0x08

int LoadScanDriveTypes();
LONG SaveScanDriveTypes(int DriveTypes);
