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
		explicit ScopedHandle(HANDLE handle);
		~ScopedHandle();

		ScopedHandle& operator=(ScopedHandle const&) = delete;
		ScopedHandle& operator=(ScopedHandle&& rhs);

		operator HANDLE();

	private:
		void Close();
		HANDLE _handle = NULL;
	};

	class ScopedFile {
	public:
		explicit ScopedFile(FILE* f);
		~ScopedFile();

		ScopedFile& operator=(ScopedFile const&) = delete;

		operator FILE*();

	private:
		FILE* _f = NULL;
	};

	char** CommandLineToArgvA(const char* cli, int* argc);
	void FreeCli(int argc, char** args);
	LockUpdaterResult LockUpdater(HANDLE* result);
	bool IsForced(int argc, char** argv);
	bool AllowUnstable(int argc, char** argv);
	char* GetLauncherProcessIdArg(int argc, char** argv);
	const char* GetUpdateURL(int argc, char** argv);
	const char* GetUpdateVersion(int argc, char** argv);
	bool GetLockFilePath(char** path);
	bool FileLocked(const char* path);
}