// Per-user settings of the window (HKEY_CURRENT_USER). The key keeps the name used by the
// original Folder Size.
#define USER_SETTINGS_KEY        TEXT("Software\\Brio\\FolderSize")
#define SERVICE_PARAMETERS_KEY   TEXT("SYSTEM\\CurrentControlSet\\Services\\FolderSize\\Parameters")


#define SCANDRIVETYPE_LOCAL        0x01
#define SCANDRIVETYPE_CD           0x02
#define SCANDRIVETYPE_REMOVABLE    0x04
#define SCANDRIVETYPE_NETWORK      0x08

// Default sort order of the window
#define SORT_NAME          0   // by name, folders first (as File Explorer does)
#define SORT_NAME_MIXED    1   // by name, folders and files mixed
#define SORT_SIZE          2   // by size, largest first

#include <string>

// Language of the user interface: a language code ("fr"), or empty for the language of Windows
std::wstring LoadLanguageSetting();
void SaveLanguageSetting(const std::wstring& code);

int LoadDefaultSort();
void SaveDefaultSort(int sort);
int LoadScanDriveTypes();
LONG SaveScanDriveTypes(int DriveTypes);
