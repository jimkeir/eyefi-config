#include "eyefi-config.h"
#include <string.h>

char EyeFiMount[MAX_PATH + 1] = { 0 };
char EyeFiVolume[MAX_PATH + 1] = { 0 };
char EyeFiDrive[MAX_PATH + 1] = { 0 };

HANDLE deviceHandle = INVALID_HANDLE_VALUE;
HANDLE memMap = INVALID_HANDLE_VALUE;

// Using the unix-standard low-level I/O functions open(), write() etc. does't make it easy to give unbuffered
// access on Windows despite the docs saying that these are indeed unbuffered. Fake these and map actual I/O functions
// to standard Win32 calls which can explicitly be marked as unbuffered, and the disk cache flushed.
#define MAX_OPEN_HANDLES (10)
HANDLE fileList[MAX_OPEN_HANDLES] = { INVALID_HANDLE_VALUE };
int maxFileID = 0;

char *locate_eyefi_mount(void)
{
	EyeFiVolume[0] = 0;
	EyeFiDrive[0] = 0;

	HANDLE volHandle = FindFirstVolumeA(EyeFiVolume, ARRAYSIZE(EyeFiVolume));
	if (volHandle != INVALID_HANDLE_VALUE)
	{
		for (;;)
		{
			if (FindNextVolumeA(volHandle, EyeFiVolume, ARRAYSIZE(EyeFiVolume)))
			{
				char diskName[MAX_PATH + 1];
				DWORD volSerial, fsFlags, pathNameSize;

				debug_printf(3, "Volume: %s\n", EyeFiVolume);

				if (GetVolumeInformationA(EyeFiVolume, diskName, MAX_PATH, &volSerial, NULL, &fsFlags, NULL, 0))
				{
					if (volSerial == 0xAA526922)
					{
						// Bingo.
						if (GetVolumePathNamesForVolumeNameA(EyeFiVolume, EyeFiDrive, MAX_PATH, &pathNameSize))
						{
							// Let's quietly assume that it only has one mount point.
							debug_printf(3, " Found Eye-Fi Pro X2(%s) on %s\n", diskName, EyeFiDrive);
							strcpy_s(EyeFiMount, MAX_PATH, EyeFiDrive);
							break;
						}
					}
				}
			}
			else
			{
				break;
			}
		}

		FindVolumeClose(volHandle);
	}

	return (EyeFiMount[0] ? EyeFiMount : NULL);
}

void eject_card(void)
{
	const char *eyeFiPath = locate_eyefi_mount();
	if (eyeFiPath != NULL)
	{
		char fullPath[MAX_PATH];
		sprintf_s(fullPath, MAX_PATH, "\\\\.\\%c:", EyeFiMount[0]);

		HANDLE handle = CreateFileA(fullPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
		if (handle != INVALID_HANDLE_VALUE)
		{
			DWORD dummy = 0;
			if (DeviceIoControl(handle, FSCTL_LOCK_VOLUME, 0, 0, 0, 0, &dummy, 0))
			{
				if (DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, 0, 0, 0, 0, &dummy, 0))
				{
					if (DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &dummy, NULL))
					{
					}
				}
			}

			CloseHandle(handle);
		}
	}
}

void *mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
	return NULL;
}

int munmap(void *addr, size_t len)
{
	return ERROR_SUCCESS;
}

int getpagesize(void)
{
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);

	return sysInfo.dwAllocationGranularity;
}


int fd_flush(int fd)
{
	if (fd >= 0 && fd <= maxFileID)
	{
		FlushFileBuffers(fileList[fd]);
	}

	return ERROR_SUCCESS;
}

int _winOpen(const char *fileName, DWORD fileMode)
{
	int res = -1;

	if (maxFileID < MAX_OPEN_HANDLES)
	{
		DWORD access = GENERIC_READ;
		switch (fileMode & 0x7)
		{
		case O_RDONLY:
			access = GENERIC_READ;
			break;
		case O_RDWR:
			access = GENERIC_READ | GENERIC_WRITE;
			break;
		case O_WRONLY:
			access = GENERIC_WRITE;
			break;
		}

		if (fileMode & O_APPEND)
		{
			access |= FILE_APPEND_DATA;
		}

		DWORD creation = OPEN_EXISTING;
		if (fileMode & O_CREAT)
		{
			creation = OPEN_ALWAYS;
		}

		HANDLE newFile = CreateFileA(fileName, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, creation, 
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
		if (newFile != INVALID_HANDLE_VALUE)
		{
			res = ++maxFileID;
			fileList[res] = newFile;
		}
	}

	return res;
}

int _winRead(int fd, void *buffer, DWORD size)
{
	int res = -1;
	if (fd >= 0 && fd <= maxFileID)
	{
		DWORD bytesRead = 0;
		ReadFile(fileList[fd], buffer, size, &bytesRead, NULL);

		res = bytesRead;
	}
	
	return res;
}

int _winWrite(int fd, void *buffer, DWORD size)
{
	int res = -1;
	if (fd >= 0 && fd <= maxFileID)
	{
		DWORD bytesWritten = 0;
		WriteFile(fileList[fd], buffer, size, &bytesWritten, NULL);

		res = bytesWritten;
	}

	return res;
}

void _winClose(int fd)
{
	if (fd >= 0 && fd <= maxFileID)
	{
		CloseHandle(fileList[fd]);
		fileList[fd] = INVALID_HANDLE_VALUE;

		// This isn't pretty... just try to bring the high water mark back down if we've been opening multiple files.
		for (maxFileID = 0; maxFileID < MAX_OPEN_HANDLES && fileList[maxFileID] != INVALID_HANDLE_VALUE; maxFileID++)
		{
		}
	}
}
