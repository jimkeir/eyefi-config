#include "eyefi-config.h"
#include <string.h>

char EyeFiMount[MAX_PATH + 1] = { 0 };
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
	char VolumeName[MAX_PATH + 1] = { 0 };
	HANDLE volHandle = FindFirstVolumeA(VolumeName, ARRAYSIZE(VolumeName));
	if (volHandle != INVALID_HANDLE_VALUE)
	{
		for (;;)
		{
			if (FindNextVolumeA(volHandle, VolumeName, ARRAYSIZE(VolumeName)))
			{
				char diskName[MAX_PATH + 1], mountNames[MAX_PATH+1];
				DWORD volSerial, fsFlags, pathNameSize;

				printf("Volume: %s\n", VolumeName);

				if (GetVolumeInformationA(VolumeName, diskName, MAX_PATH, &volSerial, NULL, &fsFlags, NULL, 0))
				{
					if (volSerial == 0xAA526922)
					{
						// Bingo.
						if (GetVolumePathNamesForVolumeNameA(VolumeName, mountNames, MAX_PATH, &pathNameSize))
						{
							// Let's quietly assume that it only has one mount point.
							printf(" Found Eye-Fi ProX2(%s) on %s\n", diskName, mountNames);
							strcpy_s(EyeFiMount, MAX_PATH, mountNames);
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
	if (EyeFiMount != NULL)
	{
		HANDLE handle = CreateFileA(EyeFiMount, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (handle != INVALID_HANDLE_VALUE)
		{
			DWORD dummy = 0;
			DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &dummy, NULL);
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

		for (maxFileID = 0; maxFileID < INVALID_HANDLE_VALUE && fileList[maxFileID] != INVALID_HANDLE_VALUE; maxFileID++)
		{
		}
	}
}


/*
13:30:03.0948228	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\RSPM	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.0949218	EyeFiHelper.exe	21892	ReadFile	G:\EYEFI\RSPM	SUCCESS	Offset: 0, Length: 16,384, I/O Flags: Non-cached, Priority: Normal	JIM-I7\Jim
13:30:03.0978307	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\RSPM	SUCCESS		JIM-I7\Jim
13:30:03.0978917	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\REQM	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.0979793	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI\REQM	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Write Through, Priority: Normal	JIM-I7\Jim
13:30:03.0986232	EyeFiHelper.exe	21892	FlushBuffersFile	G:\EYEFI\REQM	SUCCESS		JIM-I7\Jim
13:30:03.1040209	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Paging I/O, Synchronous Paging I/O, Priority: Normal	JIM-I7\Jim
13:30:03.1251564	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\REQM	SUCCESS		JIM-I7\Jim
13:30:03.1411996	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Paging I/O, Synchronous Paging I/O, Priority: Normal	JIM-I7\Jim
13:30:03.1630997	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\REQC	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.1632342	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI\REQC	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Write Through, Priority: Normal	JIM-I7\Jim
13:30:03.1799199	EyeFiHelper.exe	21892	FlushBuffersFile	G:\EYEFI\REQC	SUCCESS		JIM-I7\Jim
13:30:03.1799948	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Paging I/O, Synchronous Paging I/O, Priority: Normal	JIM-I7\Jim
13:30:03.1963017	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\REQC	SUCCESS		JIM-I7\Jim
13:30:03.2111687	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Paging I/O, Synchronous Paging I/O, Priority: Normal	JIM-I7\Jim
13:30:03.2567364	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\RSPC	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.2568232	EyeFiHelper.exe	21892	ReadFile	G:\EYEFI\RSPC	SUCCESS	Offset: 0, Length: 16,384, I/O Flags: Non-cached, Priority: Normal	JIM-I7\Jim
13:30:03.3644463	EyeFiHelper.exe	21892	ReadFile	G:\EYEFI\RSPC	SUCCESS	Offset: 0, Length: 16,384, I/O Flags: Non-cached, Priority: Normal	JIM-I7\Jim
13:30:03.3674428	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\RSPC	SUCCESS		JIM-I7\Jim
13:30:03.3675127	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\RSPM	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.3675548	EyeFiHelper.exe	21892	ReadFile	G:\EYEFI\RSPM	SUCCESS	Offset: 0, Length: 16,384, I/O Flags: Non-cached, Priority: Normal	JIM-I7\Jim
13:30:03.3699879	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\RSPM	SUCCESS		JIM-I7\Jim
13:30:03.3700489	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\RSPM	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.3700772	EyeFiHelper.exe	21892	ReadFile	G:\EYEFI\RSPM	SUCCESS	Offset: 0, Length: 16,384, I/O Flags: Non-cached, Priority: Normal	JIM-I7\Jim
13:30:03.3719987	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\RSPM	SUCCESS		JIM-I7\Jim
13:30:03.3720531	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\REQM	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.3721385	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI\REQM	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Write Through, Priority: Normal	JIM-I7\Jim
13:30:03.3727510	EyeFiHelper.exe	21892	FlushBuffersFile	G:\EYEFI\REQM	SUCCESS		JIM-I7\Jim
13:30:03.3728039	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Paging I/O, Synchronous Paging I/O, Priority: Normal	JIM-I7\Jim
13:30:03.7706241	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\REQM	SUCCESS		JIM-I7\Jim
13:30:03.7841890	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Paging I/O, Synchronous Paging I/O, Priority: Normal	JIM-I7\Jim
13:30:03.8019589	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\REQC	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.8020595	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI\REQC	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Write Through, Priority: Normal	JIM-I7\Jim
13:30:03.8129767	EyeFiHelper.exe	21892	FlushBuffersFile	G:\EYEFI\REQC	SUCCESS		JIM-I7\Jim
13:30:03.8130538	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Paging I/O, Synchronous Paging I/O, Priority: Normal	JIM-I7\Jim
13:30:03.8313330	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\REQC	SUCCESS		JIM-I7\Jim
13:30:03.8447765	EyeFiHelper.exe	21892	WriteFile	G:\EYEFI	SUCCESS	Offset: 0, Length: 4,096, I/O Flags: Non-cached, Paging I/O, Synchronous Paging I/O, Priority: Normal	JIM-I7\Jim
13:30:03.8678545	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\RSPC	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.8678920	EyeFiHelper.exe	21892	ReadFile	G:\EYEFI\RSPC	SUCCESS	Offset: 0, Length: 16,384, I/O Flags: Non-cached, Priority: Normal	JIM-I7\Jim
13:30:03.9775780	EyeFiHelper.exe	21892	ReadFile	G:\EYEFI\RSPC	SUCCESS	Offset: 0, Length: 16,384, I/O Flags: Non-cached, Priority: Normal	JIM-I7\Jim
13:30:03.9804661	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\RSPC	SUCCESS		JIM-I7\Jim
13:30:03.9805382	EyeFiHelper.exe	21892	CreateFile	G:\EYEFI\RSPM	SUCCESS	Desired Access: Generic Read/Write, Disposition: OpenIf, Options: Write Through, No Buffering, Synchronous IO Non-Alert, Non-Directory File, Attributes: N, ShareMode: Read, Write, AllocationSize: 0, OpenResult: Opened	JIM-I7\Jim
13:30:03.9805770	EyeFiHelper.exe	21892	ReadFile	G:\EYEFI\RSPM	SUCCESS	Offset: 0, Length: 16,384, I/O Flags: Non-cached, Priority: Normal	JIM-I7\Jim
13:30:03.9829938	EyeFiHelper.exe	21892	CloseFile	G:\EYEFI\RSPM	SUCCESS		JIM-I7\Jim
13:30:03.9830778	EyeFiHelper.exe	21892	CreateFile	G:\EYEFIFWU.BIN	NAME NOT FOUND	Desired Access: Read Attributes, Disposition: Open, Options: Open Reparse Point, Attributes: n/a, ShareMode: Read, Write, Delete, AllocationSize: n/a	JIM-I7\Jim
*/