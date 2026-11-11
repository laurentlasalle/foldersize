#include "StdAfx.h"
#include "PerformanceMonitor.h"
#include "EventLog.h"

// Above this average number of requests waiting for the disk, the disk is considered busy and
// the scanner pauses. At rest the value stays close to 0; it goes over 0.7 when other programs
// use the disk a lot (copying large files, loading a game).
#define PERFORMANCE_THRESHOLD 0.7

// PdhAddEnglishCounter is declared by pdh.h only for _WIN32_WINNT 0x0600 and later, and the
// service is built with an older value. pdh.dll has it since Windows Vista.
extern "C" PDH_STATUS __stdcall PdhAddEnglishCounterW(PDH_HQUERY hQuery, LPCWSTR szFullCounterPath,
	DWORD_PTR dwUserData, PDH_HCOUNTER* phCounter);

PerformanceMonitor::PerformanceMonitor(const Path& pathVolume) :
	m_bDiskQueueTooLong(false), m_hQuitEvent(NULL), m_hActiveEvent(NULL), m_hThread(NULL)
{
	// Performance counters exist only for volumes with a drive letter.
	if (PathGetDriveNumber(pathVolume.c_str()) >= 0)
	{
		// The name of the counter is in English: it is added with PdhAddEnglishCounter, which
		// accepts the English name whatever the language of Windows. (PdhAddCounter, used before,
		// expects the name in the language of Windows, so it failed on Windows in other languages,
		// and the scanner then never paused.)
		wsprintf(m_szCounter, _T("\\LogicalDisk(%c:)\\Avg. Disk Queue Length"), pathVolume[0]);

		m_hQuitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		m_hActiveEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		DWORD dwThreadId;
		m_hThread = CreateThread(NULL, 0, ThreadProc, this, 0, &dwThreadId);
	}
}

PerformanceMonitor::~PerformanceMonitor()
{
	if (m_hThread != NULL)
	{
		SetEvent(m_hQuitEvent);
		WaitForSingleObject(m_hThread, INFINITE);
		CloseHandle(m_hThread);
	}
	if (m_hQuitEvent != NULL)
		CloseHandle(m_hQuitEvent);
	if (m_hActiveEvent != NULL)
		CloseHandle(m_hActiveEvent);
}

bool PerformanceMonitor::IsDiskQueueTooLong()
{
	return m_bDiskQueueTooLong;
}

void PerformanceMonitor::SetActive(bool bActive)
{
	if (m_hActiveEvent == NULL)
		return;  // volume without a drive letter: not watched
	if (bActive)
		SetEvent(m_hActiveEvent);
	else
		ResetEvent(m_hActiveEvent);
}

DWORD WINAPI PerformanceMonitor::ThreadProc(LPVOID lpParam)
{
	PerformanceMonitor* pPerformanceMonitor = (PerformanceMonitor*)lpParam;
	pPerformanceMonitor->ThreadProc();
	return 0;
}

// Writes a warning to the Application event log when the counter can't be set up or read: the
// scanner then doesn't pause while the disk is busy. Each monitor reports at most one failure
// (see ThreadProc), so a counter that keeps failing doesn't fill the log.
void PerformanceMonitor::ReportFailure(LPCTSTR pszStep, PDH_STATUS status)
{
	TCHAR szMessage[512];
	wsprintf(szMessage, _T("Can't read the performance counter %s (%s failed with status 0x%08X). ")
		_T("Background scans of this drive won't pause while the disk is busy."),
		m_szCounter, pszStep, (UINT)status);
	EventLog::Instance().ReportWarning(szMessage);
}

void PerformanceMonitor::ThreadProc()
{
	PDH_STATUS status;
	PDH_HQUERY hQuery;
	if ((status = PdhOpenQuery(NULL, 0, &hQuery)) != ERROR_SUCCESS)
	{
		ReportFailure(_T("PdhOpenQuery"), status);
		return;
	}

	PDH_HCOUNTER hCounter;
	if ((status = PdhAddEnglishCounterW(hQuery, m_szCounter, 0, &hCounter)) != ERROR_SUCCESS)
	{
		ReportFailure(_T("PdhAddEnglishCounter"), status);
		PdhCloseQuery(hQuery);
		return;
	}

	bool bReported = false;  // a failure to read the counter is reported only once
	int nFailures = 0;       // readings failed in a row (see below)
	HANDLE hHandles[2] = { m_hQuitEvent, m_hActiveEvent };
	while (true)
	{
		// Sleep until the scanner has work, or the monitor is destroyed.
		if (WaitForMultipleObjects(2, hHandles, FALSE, INFINITE) == WAIT_OBJECT_0)
			break;

		// "Avg. Disk Queue Length" is an average over the time between two readings: the first
		// reading only sets the starting point, the value is available from the second one on.
		status = PdhCollectQueryData(hQuery);
		if (status != ERROR_SUCCESS && !bReported)
		{
			ReportFailure(_T("PdhCollectQueryData"), status);
			bReported = true;
		}

		// While the scanner has work, read the counter regularly.
		while (WaitForSingleObject(m_hQuitEvent, PERFORMANCE_UPDATE_FREQUENCY) == WAIT_TIMEOUT)
		{
			if (WaitForSingleObject(m_hActiveEvent, 0) != WAIT_OBJECT_0)
				break;  // the scanner went to sleep

			status = PdhCollectQueryData(hQuery);
			PDH_FMT_COUNTERVALUE Value;
			if (status == ERROR_SUCCESS)
				status = PdhGetFormattedCounterValue(hCounter, PDH_FMT_DOUBLE, NULL, &Value);
			if (status == ERROR_SUCCESS)
			{
				m_bDiskQueueTooLong = Value.doubleValue > PERFORMANCE_THRESHOLD;
				nFailures = 0;
			}
			else
			{
				// Unknown: don't hold the scanner back. A single failed reading can happen (for
				// example when the disk's counters restart), so only report three in a row.
				m_bDiskQueueTooLong = false;
				if (++nFailures >= 3 && !bReported)
				{
					ReportFailure(_T("Reading the counter"), status);
					bReported = true;
				}
			}
		}

		// The next activity starts from a fresh reading, not from this one.
		m_bDiskQueueTooLong = false;
		if (WaitForSingleObject(m_hQuitEvent, 0) == WAIT_OBJECT_0)
			break;
	}

	PdhCloseQuery(hQuery);
}
