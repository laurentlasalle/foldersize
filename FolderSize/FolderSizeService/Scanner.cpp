#include "StdAfx.h"
#include "Scanner.h"
#include "PerformanceMonitor.h"
#include "EventLog.h"

Scanner::Scanner(const Path& pathVolume, IScannerCallback* pCallback)
: m_hThread(NULL), m_BytesPerCluster(0), m_bCompressed(false)
{
	DWORD SectorsPerCluster, BytesPerSector;
	if (GetDiskFreeSpace(pathVolume.c_str(), &SectorsPerCluster, &BytesPerSector, NULL, NULL))
	{
		m_BytesPerCluster = SectorsPerCluster * BytesPerSector;
	}

	DWORD FileSystemFlags;
	if (GetVolumeInformation(pathVolume.c_str(), NULL, 0, NULL, NULL, &FileSystemFlags, NULL, 0))
	{
		if (FileSystemFlags & (FILE_VOLUME_IS_COMPRESSED))
		{
			m_bCompressed = true;
		}
	}

	m_pPerformanceMonitor = new PerformanceMonitor(pathVolume);
	m_pCallback = pCallback;
	m_hQuitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hScanEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	// if this thread is impersonating the user, the scanner thread needs to have the same impersonation token
	HANDLE hToken;
	if (OpenThreadToken(GetCurrentThread(), TOKEN_IMPERSONATE, FALSE, &hToken))
	{
		// apparently it's a bad idea to create a thread while impersonating
		if (RevertToSelf())
		{
			m_hThread = CreateThread(NULL, 0, ThreadProc, this, CREATE_SUSPENDED, NULL);
			if (m_hThread != NULL)
			{
				if (!SetThreadToken(&m_hThread, hToken) ||
					!ResumeThread(m_hThread))
				{
					TerminateThread(m_hThread, 0);
					m_hThread = NULL;
				}
			}
			// restore the impersonation
			SetThreadToken(NULL, hToken);
		}
		CloseHandle(hToken);
	}
	else
	{
		// if this thread is not impersonating, just create the thread normally
		m_hThread = CreateThread(NULL, 0, ThreadProc, this, 0, NULL);
	}

	if (m_hThread == NULL)
	{
		EventLog::Instance().ReportError(_T("Scanner CreateThread"), GetLastError());
	}

}

Scanner::~Scanner()
{
	if (m_hThread != NULL)
	{
		SetEvent(m_hQuitEvent);
		WaitForSingleObject(m_hThread, INFINITE);
		CloseHandle(m_hThread);
	}
	CloseHandle(m_hQuitEvent);
	CloseHandle(m_hScanEvent);
	delete m_pPerformanceMonitor;
}

void Scanner::ScanFolder(const Path& path)
{
	FOLDERINFO nSize;
	Path pathFind = path + Path(_T("*"));

	LARGE_INTEGER nTime;
	QueryPerformanceCounter(&nTime);

	WIN32_FIND_DATA FindData;
	HANDLE hFind = FindFirstFile(pathFind.GetLongAPIRepresentation().c_str(), &FindData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		DWORD dwError = GetLastError();
		// most folders have the . and .. directories, but scanning the root
		// of an empty drive will give this error
		if (dwError == ERROR_FILE_NOT_FOUND)
		{
			m_pCallback->GotScanResult(path, nSize, nTime.QuadPart);
			return;
		}
		// We could be legitimately denied access to this folder, and the folder may have been
		// deleted or moved since it was found (ERROR_PATH_NOT_FOUND): the cache hears about that
		// from the file system notifications. Report only other errors. (The error is reported
		// as read above: calling GetLastError again here returned 0 when this was the first
		// event since the service started, as opening the event log resets it.)
		if (dwError != ERROR_ACCESS_DENIED && dwError != ERROR_PATH_NOT_FOUND)
		{
			// this is an unexpected error!
			EventLog::Instance().ReportError(_T("Scanner FindFirstFile"), dwError);
		}
		// don't call GotScanResult, so we will never be asked to scan this folder again
		return;
	}

	do
	{
		// ignore reparse points... they are links to other directories.
		// including them here might make some folders look smaller than 
		// you think it is, but that's better than multiply counting files.
		if (!(FindData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
		{
			if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (FindData.cFileName[0] != L'.' || (FindData.cFileName[1] != L'\0' && (FindData.cFileName[1] != L'.' || FindData.cFileName[2] != L'\0')))
				{
					Path pathFound = path + Path(FindData.cFileName);
					m_pCallback->FoundFolder(pathFound);

					nSize.nFolders++;
				}
			}
			else
			{
				nSize.nLogicalSize += MakeULongLong(FindData.nFileSizeHigh, FindData.nFileSizeLow);

				if (m_bCompressed ||
					FindData.dwFileAttributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE))
				{
					Path filePath = path + Path(FindData.cFileName);
					DWORD dwSizeHigh;
					DWORD dwSizeLow = GetCompressedFileSize(filePath.GetLongAPIRepresentation().c_str(), &dwSizeHigh);
					DWORD dwError = (dwSizeLow == INVALID_FILE_SIZE) ? GetLastError() : NO_ERROR;
					if (dwError != NO_ERROR)
					{
						// The file may have been deleted or moved since the folder was listed;
						// report only other errors.
						if (dwError != ERROR_FILE_NOT_FOUND && dwError != ERROR_PATH_NOT_FOUND)
							EventLog::Instance().ReportError(_T("Scanner GetCompressedFileSize"), dwError);
						// just use the regular size that we have i guess
						nSize.nPhysicalSize += MakeULongLong(FindData.nFileSizeHigh, FindData.nFileSizeLow);
					}
					else
					{
						nSize.nPhysicalSize += MakeULongLong(dwSizeHigh, dwSizeLow);
					}
				}
				else
				{
					// Round up to the next cluster size
					// This is meaningless for small files that are resident to the MFT!
					ULONGLONG nLogicalSize = MakeULongLong(FindData.nFileSizeHigh, FindData.nFileSizeLow);
					nSize.nPhysicalSize += ((nLogicalSize + m_BytesPerCluster - 1) / m_BytesPerCluster) * m_BytesPerCluster;
				}

				nSize.nFiles++;
			}
		}
	} while (FindNextFile(hFind, &FindData));

	DWORD dwError = GetLastError();
	if (dwError != ERROR_NO_MORE_FILES)
	{
		EventLog::Instance().ReportError(_T("Scanner FindNextFile"), dwError);
	}

	FindClose(hFind);

	m_pCallback->GotScanResult(path, nSize, nTime.QuadPart);
}

void Scanner::Wakeup()
{
	SetEvent(m_hScanEvent);
}

bool Scanner::GetAnItemFromTheQueue(Path& path)
{
	// check if we should quit
	if (WaitForSingleObject(m_hQuitEvent, 0) == WAIT_OBJECT_0)
	{
		return false;
	}

	while (true)
	{
		// We can't reset the event after calling GetNextScanFolder,
		// or we might wipe out the event set by a Wakeup call.
		// So, always set it before.
		ResetEvent(m_hScanEvent);
		if (m_pCallback->GetNextScanFolder(path))
		{
			// there is work: watch how busy the disk is (see ThreadProc)
			m_pPerformanceMonitor->SetActive(true);
			return true;
		}
		// Nothing to scan: stop watching the disk while this thread sleeps.
		m_pPerformanceMonitor->SetActive(false);
		HANDLE hHandles[2];
		hHandles[0] = m_hQuitEvent;
		hHandles[1] = m_hScanEvent;
		if (WaitForMultipleObjects(2, hHandles, FALSE, INFINITE) == WAIT_OBJECT_0)
		{
			return false;
		}
	}
}

DWORD WINAPI Scanner::ThreadProc(LPVOID lpParameter)
{
	Scanner* pScanner = (Scanner*)lpParameter;
	pScanner->ThreadProc();
	return 0;
}

void Scanner::ThreadProc()
{
	Path path;
	while ((GetAnItemFromTheQueue(path)))
	{
		ScanFolder(path);
		// If the disk is busy with other programs (its queue of waiting requests is long, see
		// PerformanceMonitor), pause between folders until it calms down, checking again at each
		// reading of the counter, so the scan doesn't slow those programs down.
		while (m_pPerformanceMonitor->IsDiskQueueTooLong())
		{
			if (WaitForSingleObject(m_hQuitEvent, PERFORMANCE_UPDATE_FREQUENCY) == WAIT_OBJECT_0)
				return;
		}
	}
}
