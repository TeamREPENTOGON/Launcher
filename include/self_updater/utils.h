#pragma once

/*
 * Utility functions. 
 */

#include <Windows.h>

#include <string>

namespace Updater::Utils {
	enum LockUpdaterResult {
		LOCK_UPDATER_SUCCESS,
		LOCK_UPDATER_ERR_ALREADY_LOCKED,
		LOCK_UPDATER_ERR_INTERNAL
	};

	class ScopedHandle {
	public:
		ScopedHandle(HANDLE handle);
		~ScopedHandle();

		operator HANDLE();

	private:
		HANDLE _handle = NULL;
	};

	class ScopedFile {
	public:
		ScopedFile(FILE* f);
		~ScopedFile();

		operator FILE*();

	private:
		FILE* _f = NULL;
	};

	char** CommandLineToArgvA(const char* cli, int* argc);
	void FreeCli(int argc, char** args);
	LockUpdaterResult LockUpdater(HANDLE* result);
	bool IsForced(int argc, char** argv);
	bool AllowUnstable(int argc, char** argv);
	char* GetUrl(int argc, char** argv);
	bool GetLockFilePath(char** path);
}