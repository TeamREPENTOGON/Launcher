#include "shared/externals.h"

namespace Externals {
	BOOL (WINAPI *pGetUserProfileDirectoryA)(HANDLE, LPSTR, LPDWORD) = NULL;

	void Init() {
		HMODULE userEnv = LoadLibraryA("Userenv");
		if (userEnv) {
			pGetUserProfileDirectoryA = (BOOL(WINAPI *)(HANDLE, LPSTR, LPDWORD))GetProcAddress(userEnv, "GetUserProfileDirectoryA");
			FreeLibrary(userEnv);
		}
	}
}