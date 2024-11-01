#pragma once

/*
 * Utility functions. 
 * 
 * Separate from the shared lib because we want the unpacker to be small.
 */

#include <Windows.h>

#include <string>

namespace Unpacker::Utils {
	enum LockUnpackerResult {
		LOCK_UNPACKER_SUCCESS,
		LOCK_UNPACKER_ERR_ALREADY_LOCKED,
		LOCK_UNPACKER_ERR_INTERNAL
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
	LockUnpackerResult LockUnpacker(HANDLE* result);
	bool IsContinuation(int argc, char** argv);
	bool IsForced(int argc, char** argv);
	bool GetLockFilePath(char** path);
}