#include "shared/logger.h"
#include "self_updater/self_updater.h"
#include "self_updater/utils.h"
#include "shared/utils.h"

#include <UserEnv.h>
#include <stdexcept>

namespace Updater::Utils {
	static const char* LockFileBasePath = "Documents\\My Games";
	static const char* LockFileName = "repentogon_launcher_updater.lock";

	static bool FileExists(const char* path);
	static bool FileExists(const wchar_t* path);
	static bool HasFlag(int argc, char** argv, const char* name);
	static void ReplaceQuotes(char* str, size_t* len);
	static char** PushCLIArg(char** array, int* arraySize, int* arrayCapacity, const char* start, const char* end);

	// Returns true if the provided arg (ie, `--flag`) is present in argv.
	bool HasFlag(int argc, char** argv, const char* name) {
		for (int i = 0; i < argc; ++i) {
			if (!strcmp(argv[i], name)) {
				return true;
			}
		}

		return false;
	}

	// Locates the provided arg (ie, `--flag`) in argv, then returns the NEXT arg.
	// For input flags such as `--name "bob"`, in which case the string `bob` is returned.
	// Returns nullptr if the arg is not present.
	char* GetParamValue(int argc, char** argv, const char* name) {
		for (int i = 0; i < argc-1; ++i) {
			if (!strcmp(argv[i], name)) {
				return argv[i+1];
			}
		}
		return nullptr;
	}

	bool FileExists(const char* path) {
		return GetFileAttributesA(path) != -1;
	}

	bool FileExists(const wchar_t* path) {
		return GetFileAttributesW(path) != -1;
	}

	bool IsForced(int argc, char** argv) {
		return HasFlag(argc, argv, Updater::ForcedArg);
	}

	bool AllowUnstable(int argc, char** argv) {
		return HasFlag(argc, argv, Updater::UnstableArg);
	}

	char* GetLauncherProcessIdArg(int argc, char** argv) {
		return GetParamValue(argc, argv, Updater::LauncherProcessIdArg);
	}

	const char* GetUpdateURL(int argc, char** argv) {
		return GetParamValue(argc, argv, Updater::ReleaseURL);
	}

	const char* GetUpdateVersion(int argc, char** argv) {
		return GetParamValue(argc, argv, Updater::UpgradeVersion);
	}

	void ReplaceQuotes(char* str, size_t* len) {
		bool escaped = false;
		char* cur = str;
		while (*cur) {
			if (*cur == '"') {
				if (escaped) {
					memmove(cur - 1, cur, *len - (cur - str));
					--* len;
					escaped = false;
					continue;
				} else {
					memmove(cur, cur + 1, *len - (cur - str - 1));
				}
				--* len;
			} else if (*cur == '\\') {
				escaped = !escaped;
			} else {
				escaped = false;
			}

			++cur;
		}
	}

	char** PushCLIArg(char** array, int* arraySize, int* arrayCapacity, const char* start, const char* end) {
		char** result = array;
		if (*arraySize == *arrayCapacity) {
			*arrayCapacity *= 2;
			result = (char**)realloc(array, sizeof(char*) * *arrayCapacity);

			if (!result) {
				Logger::Error("PushCLIArgs: unable to allocate memory to store arguments\n");
				return NULL;
			}
		}

		char* param = (char*)malloc(end - start + 2);
		if (!param) {
			Logger::Error("PushCLIArgs: unable to allocate memory to store parameter\n");
			return NULL;
		}

		size_t len = end - start + 1;
		memcpy(param, start, end - start + 1);
		param[len] = '\0';
		ReplaceQuotes(param, &len);

		result[*arraySize] = param;
		++* arraySize;

		return result;
	}

	char** CommandLineToArgvA(const char* cli, int* argc) {
		bool inString = false;
		bool inEscape = false;

		const char* cur = cli;
		const char* startToken = NULL;

		int nResults = 0;
		int maxResults = 1;
		char** results = (char**)malloc(sizeof(char*) * maxResults);;
		if (!results) {
			Logger::Error("CommandLineToArgvA: unable to allocate memory to store arguments\n");
			return NULL;
		}

		while (*cur) {
			if (*cur == '"') {
				if (!inEscape) {
					inString = !inString;

					if (inString) {
						if (!startToken) {
							startToken = cur;
						}
					}
				}
			} else if ((isspace(*cur) || *cur == '\t') && !inString) {
				if (startToken) {
					char** newResults = PushCLIArg(results, &nResults, &maxResults, startToken, cur - 1);
					if (!newResults) {
						FreeCli(nResults, results);
						Logger::Error("CommandLineToArgvA: error during PushCLIArg\n");
						return NULL;
					}

					results = newResults;
					startToken = NULL;
				}
			} else if (!startToken) {
				startToken = cur;
			}

			inEscape = *cur == '\\';
			++cur;
		}

		if (startToken) {
			/* CLI should be well formed. We cannot be in a string at the end of the
			 * loop. Therefore the final argument is either empty (startToken == NULL,
			 * so irrelevant), or an unquoted string (so no need to startToken).
			 */
			char** newResults = PushCLIArg(results, &nResults, &maxResults, startToken, cur - 1);
			if (!newResults) {
				FreeCli(nResults, results);
				Logger::Error("CommandLineToArgvA: error during PushCLIArgs\n");
				return NULL;
			}

			results = newResults;
		}

		*argc = nResults;
		return results;
	}

	void FreeCli(int argc, char** argv) {
		for (int i = 0; i < argc; ++i) {
			free(argv[i]);
		}

		free(argv);
	}

	LockUpdaterResult LockUpdater(HANDLE* result) {
		std::filesystem::path lockFilePath = utils::GetUserMyGamesDir();
		if (lockFilePath.empty()) {
			Logger::Error("LockUpdater: Failed to obtain user profile directory\n");
			return LOCK_UPDATER_ERR_INTERNAL;
		}
		lockFilePath /= LockFileName;

		HANDLE file = CreateFileW(lockFilePath.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

		if (file == INVALID_HANDLE_VALUE) {
			DWORD lastError = GetLastError();
			if (lastError == ERROR_SHARING_VIOLATION) {
				Logger::Warn("LockUpdater: lock file is already owned by another process\n");
				return LOCK_UPDATER_ERR_ALREADY_LOCKED;
			} else {
				Logger::Error("LockUpdater: CreateFileA failed (%d)\n", lastError);
				return LOCK_UPDATER_ERR_INTERNAL;
			}
		}

		*result = file;
		return LOCK_UPDATER_SUCCESS;
	}

	bool FileLocked(const char* path) {
		return FileExists(path) && ScopedHandle(CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE;
	}

	bool FileLocked(const wchar_t* path) {
		return FileExists(path) && ScopedHandle(CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE;
	}

	ScopedHandle::ScopedHandle(HANDLE handle) : _handle(handle) { }
	ScopedHandle::~ScopedHandle() { Close(); }
	ScopedHandle::operator HANDLE() { return _handle; }

	ScopedHandle& ScopedHandle::operator=(ScopedHandle&& rhs) {
		_handle = rhs._handle;
		rhs._handle = NULL;
		return *this;
	}

	void ScopedHandle::Close() {
		if (_handle != NULL && _handle != INVALID_HANDLE_VALUE) {
			CloseHandle(_handle);
			_handle = NULL;
		}
	}

	ScopedFile::ScopedFile(FILE* f) : _f(f) { }
	ScopedFile::~ScopedFile() { if (_f) fclose(_f); }
	ScopedFile::operator FILE*() { return _f;  }
}