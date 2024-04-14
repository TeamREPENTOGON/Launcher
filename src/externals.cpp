#include "launcher/externals.h"

namespace Launcher::Externals {
	BOOL (*pGetUserProfileDirectoryA)(HANDLE, LPSTR, LPDWORD) = NULL;

	void Init() {
		HMODULE userEnv = LoadLibraryA("Userenv");
		if (userEnv) {
			pGetUserProfileDirectoryA = (BOOL(*)(HANDLE, LPSTR, LPDWORD))GetProcAddress(userEnv, "GetUserProfileDirectoryA");
			FreeLibrary(userEnv);
		}
	}
}