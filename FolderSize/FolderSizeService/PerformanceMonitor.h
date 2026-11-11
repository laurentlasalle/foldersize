#pragma once

#include "Path.h"

// How often the disk queue length is read while the scanner works, in milliseconds. The scanner
// also waits in steps of this length while the disk is busy (see Scanner::ThreadProc).
#define PERFORMANCE_UPDATE_FREQUENCY 700

// Watches how busy the disk of a volume is, so the background scanner of that volume can pause
// while other programs use the disk (see Scanner::ThreadProc). It reads the Windows performance
// counter "Avg. Disk Queue Length" of the volume's drive letter: the average number of read and
// write requests waiting for the disk. Volumes without a drive letter aren't watched, and their
// scanner never pauses.
//
// The counter is read only while the scanner has work (see SetActive): an idle service doesn't
// wake up every PERFORMANCE_UPDATE_FREQUENCY milliseconds for nothing.
class PerformanceMonitor
{
public:
	PerformanceMonitor(const Path& pathVolume);
	~PerformanceMonitor();

	// Whether the disk was busy at the last reading of the counter. Always false while the
	// monitor is inactive, or if the counter can't be read (that is reported once in the
	// Application event log).
	bool IsDiskQueueTooLong();

	// The scanner calls this with true when it starts working through its queue of folders, and
	// with false when the queue is empty and it goes to sleep.
	void SetActive(bool bActive);

private:
	static DWORD WINAPI ThreadProc(LPVOID lpParam);
	void ThreadProc();
	void ReportFailure(LPCTSTR pszStep, PDH_STATUS status);

	HANDLE m_hThread;
	HANDLE m_hQuitEvent;    // manual reset: set once, when the monitor is destroyed
	HANDLE m_hActiveEvent;  // manual reset: set while the scanner has work
	volatile bool m_bDiskQueueTooLong;
	TCHAR m_szCounter[256];
};
