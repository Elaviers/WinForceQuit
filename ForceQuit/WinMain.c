#include <Windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <shlobj.h>
#include <tchar.h>
#include "resource.h"

#define WMU_TRAYMENU (WM_APP + 1)

HWND hwnd;
NOTIFYICONDATAA notify;
bool active = true;

TCHAR* startupFilePath = NULL;

IShellLink* iShellLink;
IPersistFile* iPersistFile;

HANDLE mutex;

bool GetIsLaunchingOnStartup()
{
	DWORD attrib = GetFileAttributes(startupFilePath);
	if (attrib == INVALID_FILE_ATTRIBUTES || attrib == FILE_ATTRIBUTE_DIRECTORY)
		return false;

	HRESULT r = iPersistFile->lpVtbl->Load(iPersistFile, startupFilePath, STGM_READ);
	if (FAILED(r))
	{
		printf("Error: IPersistFile::Load failed\n");
		return false;
	}

	r = iShellLink->lpVtbl->Resolve(iShellLink, hwnd, 0);
	if (FAILED(r))
	{
		printf("Error: IShellLink::Resolve failed\n");
		return false;
	}

	WIN32_FIND_DATAW fd;
	WCHAR scPath[MAX_PATH];
	r = iShellLink->lpVtbl->GetPath(iShellLink, scPath, MAX_PATH, &fd, SLGP_RAWPATH);
	if (FAILED(r))
	{
		printf("Error: IShellLink::GetPath failed\n");
		return false;
	}

	WCHAR currentPath[MAX_PATH];
	GetModuleFileNameW(NULL, currentPath, MAX_PATH);

	//Check if the file referenced by the shortcut is us
	HANDLE f1 = CreateFileW(scPath, 0, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	HANDLE f2 = CreateFileW(currentPath, 0, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (f1 != INVALID_HANDLE_VALUE && f2 != INVALID_HANDLE_VALUE)
	{
		BY_HANDLE_FILE_INFORMATION info1;
		BY_HANDLE_FILE_INFORMATION info2;
		if (GetFileInformationByHandle(f1, &info1) && GetFileInformationByHandle(f2, &info2))
		{
			CloseHandle(f1);
			CloseHandle(f2);

			return info1.dwVolumeSerialNumber == info2.dwVolumeSerialNumber 
				&& info1.nFileIndexLow == info2.nFileIndexLow 
				&& info1.nFileIndexHigh == info2.nFileIndexHigh;
		}
	}

	if (f1 != INVALID_HANDLE_VALUE) CloseHandle(f1);
	if (f2 != INVALID_HANDLE_VALUE) CloseHandle(f2);
	return false;
}

void SetLaunchOnStartup(bool launchOnStartup)
{
	if (launchOnStartup)
	{
		WCHAR currentPath[MAX_PATH];
		GetModuleFileNameW(NULL, currentPath, MAX_PATH);

		iShellLink->lpVtbl->SetPath(iShellLink, currentPath);

		HRESULT r = iPersistFile->lpVtbl->Save(iPersistFile, startupFilePath, TRUE);
		if (FAILED(r)) printf("ERROR: Unable to save startup shortcut\n");
	}
	else
	{
		DeleteFile(startupFilePath);
	}
}

void Cleanup()
{
	iPersistFile->lpVtbl->Release(iPersistFile);
	iShellLink->lpVtbl->Release(iShellLink);

	Shell_NotifyIconA(NIM_DELETE, &notify);
	DestroyWindow(hwnd);
	free(startupFilePath);
	ReleaseMutex(mutex);
}

BOOL WINAPI ConsoleHandler(DWORD ctrlType)
{
	if (ctrlType == CTRL_CLOSE_EVENT)
	{
		Cleanup();
		return TRUE;
	}

	return FALSE;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
	case WM_COMMAND:
	{
		switch (wparam)
		{
		case ID_EXITFORCEQUIT:
			active = false;
			break;

		case ID_LAUNCHONSTARTUP:
			SetLaunchOnStartup(!GetIsLaunchingOnStartup());
			break;
		}	
		return 0;
	}

	case WMU_TRAYMENU:
	{
		switch (lparam)
		{
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		{
			POINT cp;
			GetCursorPos(&cp);
			HMENU menu = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU)), 0);
			CheckMenuItem(menu, ID_LAUNCHONSTARTUP, GetIsLaunchingOnStartup() ? MF_CHECKED : MF_UNCHECKED);
			SetForegroundWindow(hwnd); //If I don't do this the popup menu doesn't go away on fg loss
			TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON, cp.x, cp.y, 0, hwnd, NULL);
			SendMessage(hwnd, WM_NULL, 0, 0); //Force task switch
			DestroyMenu(menu);
			break;
		}
		}

		return 0;
	}

	default:
		return DefWindowProcA(hwnd, msg, wparam, lparam);
	}
}

int APIENTRY WinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev, _In_ LPSTR cmdLine, _In_ int showCmd)
{
	mutex = CreateMutexA(NULL, FALSE, "Force_Quit_MUTEX");
	if (mutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
		return 0;

	//
	//	Handle Debug switch
	//
	bool debug = true;
	{
		const char* debugSwitch = "-debug";

		for (int i = 0; i < 6; ++i)
		{
			if (cmdLine[i] != debugSwitch[i])
			{
				debug = false;
				break;
			}
		}

		if (debug && !(cmdLine[6] == ' ' || cmdLine[6] == '\0'))
			debug = false;

		if (debug)
		{
			AllocConsole();
			FILE* dummy;
			freopen_s(&dummy, "CONIN$", "r", stdin);
			freopen_s(&dummy, "CONOUT$", "w", stderr);
			freopen_s(&dummy, "CONOUT$", "w", stdout);

			SetConsoleCtrlHandler(ConsoleHandler, TRUE);

			printf("Debug console activated\n");
		}
	}

	//
	//	Create HWND to own the notification & popup
	//
	{
		LPCSTR wndClassName = "DummyWindow";
		WNDCLASSEXA wndClass;
		ZeroMemory(&wndClass, sizeof(wndClass));
		wndClass.cbSize = sizeof(wndClass);
		wndClass.lpszClassName = wndClassName;
		wndClass.hInstance = instance;
		wndClass.hIcon = wndClass.hIconSm = LoadIcon(instance, MAKEINTRESOURCE(IDI_ICON));
		wndClass.lpfnWndProc = WindowProc;
		if (!RegisterClassExA(&wndClass)) printf("Error creating window class!\n");

		hwnd = CreateWindowExA(0, wndClassName, "ForceQuit", 0, 0, 0, 0, 0, NULL, NULL, instance, NULL);

	//
	//	Add the notification icon
	//
		ZeroMemory(&notify, sizeof(notify));
		notify.cbSize = sizeof(notify);
		notify.hWnd = hwnd;
		notify.uID = IDI_ICON;
		notify.uCallbackMessage = WMU_TRAYMENU;
		notify.hIcon = wndClass.hIcon;
		strcpy_s(notify.szTip, 16, "ForceQuit"); //szTip will always be longer than 16 chars
		notify.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;

		Shell_NotifyIconA(NIM_ADD, &notify);
	}

	//
	//	Find the startup shortcut scPath
	//
	{
		const TCHAR* filename = _T("\\ForceQuit.lnk");
		PTSTR startupDir = NULL;
		SHGetKnownFolderPath(&FOLDERID_Startup, KF_FLAG_DEFAULT, NULL, &startupDir);
		size_t dirLength = _tcslen(startupDir);

		size_t fullSize = dirLength + _tcslen(filename) + 1;
		startupFilePath = (TCHAR*)malloc(sizeof(TCHAR) * fullSize);

		if (startupFilePath != NULL)
		{
			_tcscpy_s(startupFilePath, fullSize, startupDir);
			_tcscpy_s(startupFilePath + dirLength, fullSize - dirLength, filename);

			if (debug)
			{
				char* startupFilePathA = malloc(sizeof(char) * fullSize);
				wcstombs_s(NULL, startupFilePathA, fullSize, startupFilePath, fullSize);
				printf("Startup shortcut path is \"%s\"\n", startupFilePathA);

				free(startupFilePathA);
			}
		}
		else if (debug) printf("ERROR: malloc failed for startupFilePath\n");

		CoTaskMemFree(startupDir);
	}

	//
	//	COM shortcut stuff
	//
	{
		if (CoInitializeEx(NULL, COINIT_APARTMENTTHREADED) != S_OK)
			printf("ERROR: Could not initialise COM\n");
		
		HRESULT r = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, &iShellLink);
		if (FAILED(r))
			printf("ERROR: Failed to create COM object for IShellLink\n");
		else
		{
			r = iShellLink->lpVtbl->QueryInterface(iShellLink, &IID_IPersistFile, &iPersistFile);
			if (FAILED(r))
				printf("ERROR: Failed to query IPersistFile\n");
		}
	}

	//
	//
	//
	HWND fg = 0;
	MSG msg;
	while (active)
	{
		while (PeekMessageA(&msg, hwnd, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}

		if (GetKeyState(VK_MENU) & 0x8000 && GetKeyState(VK_F4) & 0x8000)
		{
			if (fg)
			{
				Sleep(100); //Wait a little to see if window will close normally

				if (GetForegroundWindow() == fg)
				{
					//Ok, go away now

					DWORD pid;
					GetWindowThreadProcessId(fg, &pid);

					if (debug)
					{
						char str[256];
						GetWindowTextA(fg, str, 256);

						printf("Terminating \"%s\" (%d)\n", str, pid);
					}

					HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, TRUE, pid);
					TerminateProcess(process, 0);
				}

				fg = 0;
			}
		}
		else
		{
			fg = GetForegroundWindow();
		}

		Sleep(10); //Poll at 100hz
	}

	Cleanup();
	return 0;
}
