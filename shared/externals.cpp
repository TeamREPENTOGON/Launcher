#include "shared/externals.h"

namespace Externals {
	BOOL (WINAPI *pGetUserProfileDirectoryA)(HANDLE, LPSTR, LPDWORD) = NULL;
	static HMODULE userEnv = NULL;

	void Init() {
		userEnv = LoadLibraryA("Userenv");
		if (userEnv) {
			pGetUserProfileDirectoryA = (BOOL(WINAPI *)(HANDLE, LPSTR, LPDWORD))GetProcAddress(userEnv, "GetUserProfileDirectoryA");
		}
	}

	void End() {
		if (userEnv) {
			FreeLibrary(userEnv);
		}
	}
}