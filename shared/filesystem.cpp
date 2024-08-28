#include <Windows.h>

#include <cstdlib>
#include <cstring>

#include "shared/externals.h"
#include "shared/filesystem.h"
#include "shared/logger.h"

namespace Filesystem {
	bool CreateFileHierarchy(const char* name) {
		char* copy = (char*)malloc(strlen(name) + 1);
		if (!copy) {
			Logger::Error("CreateFileHierarchy: unable to allocate memory to duplicate %s\n", name);
			return false;
		}

		strcpy(copy, name);
		char* next = strpbrk(copy, "/");
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
			next = strpbrk(next + 1, "/");
		}

		free(copy);
		return true;
	}

	bool FolderExists(const char* name) {
		return GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES;
	}

	bool FileExists(const char* name) {
		WIN32_FIND_DATAA data;
		return FileExists(name, &data);
	}

	bool FileExists(const char* name, WIN32_FIND_DATAA* data) {
		memset(data, 0, sizeof(*data));
		HANDLE result = FindFirstFileA(name, data);
		bool ret = result != INVALID_HANDLE_VALUE;
		if (ret) {
			FindClose(result);
		}

		return ret;
	}

	std::string GetCurrentDirectory_() {
		DWORD count = GetCurrentDirectoryA(0, NULL);
		std::string result;
		result.reserve(count + 1);
		GetCurrentDirectoryA(result.capacity(), result.data());
		return result;
	}

	SaveFolderResult GetIsaacSaveFolder(std::string* saveFolder) {
		char homeDirectory[4096];
		DWORD homeLen = 4096;
		HANDLE token = GetCurrentProcessToken();

		if (!Externals::pGetUserProfileDirectoryA) {
			DWORD result = GetEnvironmentVariableA("USERPROFILE", homeDirectory, 4096);
			if (result < 0) {
				return SAVE_FOLDER_ERR_USERPROFILE;
			}
		}
		else {
			BOOL result = Externals::pGetUserProfileDirectoryA(token, homeDirectory, &homeLen);

			if (!result) {
				Logger::Error("Unable to find user profile directory: %d\n", GetLastError());
				return SAVE_FOLDER_ERR_GET_USER_PROFILE_DIR;
			}
		}

		std::string path(homeDirectory);
		path += "\\Documents\\My Games\\Binding of Isaac Repentance";

		if (!FolderExists(path.c_str())) {
			Logger::Error("Repentance save folder %s does not exist\n", path.c_str());
			return SAVE_FOLDER_DOES_NOT_EXIST;
		}

		*saveFolder = std::move(path);
		return SAVE_FOLDER_OK;
	}

	bool RemoveFile(const char* filename) {
		return DeleteFileA(filename) != 0;
	}
}