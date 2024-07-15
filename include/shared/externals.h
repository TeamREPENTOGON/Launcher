#pragma once

#include <Windows.h>

/* Define pointers to functions that are part of the Win32 API but that may 
 * not be present on all installations of Windows. 
 */
namespace Externals {
	void Init();

	extern BOOL(WINAPI *pGetUserProfileDirectoryA)(HANDLE, LPSTR, LPDWORD);
}