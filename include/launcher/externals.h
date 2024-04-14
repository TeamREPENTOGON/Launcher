#pragma once

#include <UserEnv.h>

/* Define pointers to functions that are part of the Win32 API but that may 
 * not be present on all installations of Windows. 
 */



namespace Launcher {
	namespace Externals {
		void Init();

		extern BOOL(WINAPI *pGetUserProfileDirectoryA)(HANDLE, LPSTR, LPDWORD);
	}
}