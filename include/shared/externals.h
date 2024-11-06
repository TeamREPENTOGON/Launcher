#pragma once

#include <WinSock2.h>
#include <Windows.h>

/* Define pointers to functions that are part of the Win32 API but that may 
 * not be present on all installations of Windows. 
 */
namespace Externals {
	void Init();
	void End();

	extern BOOL(WINAPI *pGetUserProfileDirectoryA)(HANDLE, LPSTR, LPDWORD);
}