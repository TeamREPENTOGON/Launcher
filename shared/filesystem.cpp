#include <WinSock2.h>
#include <Windows.h>

#include <cstdlib>
#include <cstring>

#include "shared/externals.h"
#include "shared/filesystem.h"
#include "shared/logger.h"
#include "shared/utils.h"

namespace Filesystem {
	bool CreateFileHierarchy(const char* name, const char* sep) {
		Logger::Debug("Creating file hierarchy %s\n", name);
		char* copy = (char*)malloc(strlen(name) + 1);
		if (!copy) {
			Logger::Error("CreateFileHierarchy: unable to allocate memory to duplicate %s\n", name);
			return false;
		}

		strcpy(copy, name);
		char* next = strpbrk(copy, sep);
		while (next) {
			char save = *next;
			*next = '\0';
			BOOL created = CreateDirectoryA(copy, NULL);
			if (!created) {
				DWORD lastError = GetLastError();
				if (lastError != ERROR_ALREADY_EXISTS) {
					Logger::Error("CreateFileHierarchy: unable to create folder %s: %d\n", copy, lastError);
					free(copy);
					return false;
				}
			}
			*next = save;
			next = strpbrk(next + 1, sep);
		}

		free(copy);
		return true;
	}

	bool IsFolder(const char* name) {
		DWORD attributes = GetFileAttributesA(name);
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool FindFile(const char* name, WIN32_FIND_DATAA* data) {
		if (!data) {
			return Exists(name);
		}

		memset(data, 0, sizeof(*data));
		HANDLE result = FindFirstFileA(name, data);
		bool ret = result != INVALID_HANDLE_VALUE;
		if (ret) {
			FindClose(result);
		}

		return ret;
	}

	bool Exists(const char* name) {
		return GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES;
	}

	std::string GetCurrentDirectory_() {
		DWORD count = GetCurrentDirectoryA(0, NULL);
		char* buffer = (char*)malloc(count + 1);
		if (!buffer) {
			Logger::Error("GetCurrentDirectory_: unable to allocate string\n");
			return std::string();
		}

		GetCurrentDirectoryA(count, buffer);
		std::string result(buffer);
		free(buffer);
		return result;
	}

	bool RemoveFile(const char* filename) {
		return DeleteFileA(filename) != 0;
	}

	bool SplitIntoComponents(const char* path, std::string* drive,
		std::string* filename, std::string* extension,
		std::vector<std::string>* folders) {
		char driveStr[10];
		char foldersStr[4096];
		char filenameStr[4096];
		char extensionStr[4096];

		if (_splitpath_s(path,
			drive ? driveStr : nullptr, drive ? 10 : 0,
			folders ? foldersStr : nullptr, folders ? 4096 : 0,
			filename ? filenameStr : nullptr, filename ? 4096 : 0,
			extension ? extensionStr : nullptr, extension ? 4096 : 0)) {
			return false;
		}

		if (drive) {
			*drive = driveStr;
		}

		if (filename) {
			*filename = filenameStr;
		}

		if (extension) {
			*extension = extensionStr;
		}

		if (!folders) {
			return true;
		}

		TokenizePath(foldersStr, *folders);

		return true;
	}

	void TokenizePath(const char* path, std::vector<std::string>& tokens) {
		utils::Tokenize(path, "/\\", tokens);
	}
}
