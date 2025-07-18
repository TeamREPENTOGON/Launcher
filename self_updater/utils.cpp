#include "shared/logger.h"
#include "self_updater/self_updater.h"
#include "self_updater/utils.h"

#include <UserEnv.h>
#include <stdexcept>

namespace Updater::Utils {
	static const char* LockFileBasePath = "Documents\\My Games\\Binding of Isaac Repentance";
	static const char* RepentogonSubFolder = "Repentogon";
	static const char* LockFileName = "repentogon_launcher_updater.lock";

	static char* GetUserProfileDir();
	static bool FileExists(const char* path);
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
		ReplaceQuotes(param, &len);
		param[len] = '\0';

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
		char* lockFilePath = NULL;
		if (!GetLockFilePath(&lockFilePath)) {
			free(lockFilePath);
			Logger::Error("LockUpdater: error in call to GetLockFilePath\n");
			return LOCK_UPDATER_ERR_INTERNAL;
		}

		HANDLE file = CreateFileA(lockFilePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
		free(lockFilePath);

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

	bool GetLockFilePath(char** path) {
		char* profilePath = GetUserProfileDir();
		if (!profilePath) {
			Logger::Error("GetLockFilePath: error in call to GetUserProfileDir\n");
			return false;
		}

		std::string pathStr(profilePath);
		free(profilePath);

		if (!FileExists(pathStr.c_str())) {
			Logger::Error("GetLockFilePath: user profile directory %s does not exist\n", pathStr.c_str());
			return false;
		}

		pathStr += std::string("\\") + LockFileBasePath;
		if (!FileExists(pathStr.c_str())) {
			Logger::Error("GetLockFilePath: Repentance save folder %s does not exist\n", pathStr.c_str());
			return false;
		}

		pathStr += std::string("\\") + RepentogonSubFolder;
		if (!FileExists(pathStr.c_str())) {
			Logger::Info("GetLockFilePath: Repentogon data folder %s does not exist, creating it\n", pathStr.c_str());
			if (!CreateDirectoryA(pathStr.c_str(), NULL)) {
				DWORD lastError = GetLastError();
				Logger::Error("GetLockFilePath: error while creating Repentogon data folder %s (%d)\n", pathStr.c_str(), lastError);
				return false;
			}
		}

		pathStr += std::string("\\") + LockFileName;
		*path = (char*)malloc(pathStr.size() + 1);
		if (!*path) {
			Logger::Error("GetLockFilePath: unable to allocate memory to store path\n");
			return false;
		}

		strcpy(*path, pathStr.c_str());
		return true;
	}

	char* GetUserProfileDir() {
		char buffer[4096];
		DWORD len = 4096;
		HANDLE token = GetCurrentProcessToken();
		char* path = NULL;

		if (!GetUserProfileDirectoryA(token, buffer, &len)) {
			if (len != 4096) {
				path = (char*)malloc(len);
				if (!path) {
					Logger::Error("GetUserProfileDir: unable to allocate memory for path\n");
					return NULL;
				}

				if (!GetUserProfileDirectoryA(token, path, &len)) {
					Logger::Error("GetUserProfileDir: GetUserProfileDirectoryA on malloced buffer failed (%d)\n", GetLastError());
					free(path);
					return NULL;
				}

				return path;
			} else {
				Logger::Error("GetUserProfileDir: GetUserProfileDirectoryA failed (%d)\n", GetLastError());
				return NULL;
			}
		}

		path = (char*)malloc(strlen(buffer) + 1);
		if (!path) {
			Logger::Error("GetUserProfileDir: unable to allocate memory for path\n");
			return NULL;
		}

		strcpy(path, buffer);
		return path;
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