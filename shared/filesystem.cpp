#include <WinSock2.h>
#include <Windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <cstring>

#include <filesystem>
#include <sstream>

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

	bool Exists(const char* name, HANDLE transaction) {
		if (transaction) {
			WIN32_FILE_ATTRIBUTE_DATA data;
			return GetFileAttributesTransactedA(name, GetFileExInfoStandard, &data, transaction) != 0
				&& data.dwFileAttributes != INVALID_FILE_ATTRIBUTES;
		} else {
			return GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES;
		}
	}

	bool SafeExists(std::filesystem::path const& path, std::error_code* ec) {
		try {
			std::error_code err;
			if (!ec)
				ec = &err;

			return std::filesystem::exists(path, *ec);
		} catch (std::filesystem::filesystem_error&) {
			return false;
		}
	}

	bool SafeExists(const char* name, std::error_code* ec) {
		return SafeExists(std::filesystem::path(name), ec);
	}

	bool RemoveFile(const char* filename, HANDLE transaction) {
		if (transaction) {
			return DeleteFileTransactedA(filename, transaction) != 0;
		} else {
			return DeleteFileA(filename) != 0;
		}
	}

	// SHFILEOPSTRUCT requires double-null terminated strings
	std::vector<wchar_t> CreateShellPathBuffer(const std::filesystem::path& path, const bool dirContents) {
		std::wstring str = path.wstring();
		std::vector<wchar_t> buffer(str.begin(), str.end());
		if (dirContents && std::filesystem::is_directory(path)) {
			buffer.push_back(L'\\');
			buffer.push_back(L'*');
		}
		buffer.push_back(L'\0');
		buffer.push_back(L'\0');
		return buffer;
	}

	bool OneDriveSafeCopy(const std::filesystem::path& src, const std::filesystem::path& dst) {
		if (!Filesystem::SafeExists(src)) {
			Logger::Error("Cannot copy `%s`: It doesn't exist!\n", src.string().c_str());
			return false;
		}

		try {
			std::filesystem::copy(src, dst, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive);
			return true;
		}
		catch (std::filesystem::filesystem_error& err) {
			Logger::Error("Failed to copy from `%s` to `%s` (%d)\n", src.string().c_str(), dst.string().c_str(), err.what());
		}

		// copy failed. Try again using a windows file operation.
		
		std::vector<wchar_t> src_buf = CreateShellPathBuffer(src, true);
		std::vector<wchar_t> dst_buf = CreateShellPathBuffer(dst, false);

		SHFILEOPSTRUCTW file_op = { 0 };
		file_op.wFunc = FO_COPY;
		file_op.pFrom = src_buf.data();
		file_op.pTo = dst_buf.data();
		file_op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI;

		int result = SHFileOperationW(&file_op);
		if (result != 0) {
			Logger::Error("Shell copy of `%s` -> `%s` failed: %d\n", src.string().c_str(), dst.string().c_str(), result);
			return false;
		}
		if (file_op.fAnyOperationsAborted) {
			Logger::Error("Shell copy of `%s` -> `%s` aborted!\n", src.string().c_str(), dst.string().c_str());
			return false;
		}
		return true;
	}

	bool OneDriveSafeDelete(const std::filesystem::path& path) {
		if (!Filesystem::SafeExists(path)) {
			return true;
		}

		try {
			std::filesystem::remove_all(path);
			return true;
		} catch (std::filesystem::filesystem_error& err) {
			Logger::Error("Deletion of `%s` failed: %s\n", path.string().c_str(), err.what());
		}

		// remove_all failed. Try again using a windows file operation.
		
		std::vector<wchar_t> buf = CreateShellPathBuffer(path, false);

		SHFILEOPSTRUCTW file_op = { 0 };
		file_op.wFunc = FO_DELETE;
		file_op.pFrom = buf.data();
		file_op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

		int result = SHFileOperationW(&file_op);
		if (result != 0) {
			Logger::Error("Shell deletion of `%s` failed: %d\n", path.string().c_str(), result);
			return false;
		}
		if (file_op.fAnyOperationsAborted) {
			Logger::Error("Shell deletion of `%s` aborted!\n", path.string().c_str());
			return false;
		}
		return true;
	}

	bool DeleteFolder(const char* path, HANDLE transaction) {
		std::ostringstream stream;
		stream << path << "\\*";
		std::string realPath = stream.str();

		WIN32_FIND_DATAA data;
		HANDLE searchHandle;
		memset(&data, 0, sizeof(data));

		if (transaction) {
			searchHandle = FindFirstFileTransactedA(realPath.c_str(),
				FindExInfoBasic, &data, FindExSearchNameMatch,
				NULL, 0, transaction);
		} else {
			searchHandle = FindFirstFileA(realPath.c_str(), &data);
		}

		if (searchHandle == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
			Logger::Error("DeleteFolder %s: FindFirstFile(Transacted)A = %d (transaction = %p)\n",
				path, GetLastError(), transaction);
			return false;
		}

		if (searchHandle != INVALID_HANDLE_VALUE) {
			do {
				std::ostringstream fullPath;
				fullPath << path << "\\" << data.cFileName;
				if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, "..")) {
						continue;
					}

					if (!DeleteFolder(fullPath.str().c_str(), transaction)) {
						Logger::Error("DeleteFolder %s: failed to recursively delete subfolder %s\n",
							path, fullPath.str().c_str());
						return false;
					}
				} else {
					if (!RemoveFile(fullPath.str().c_str(), transaction)) {
						Logger::Error("DeleteFolder %s: failed to delete file %s\n",
							path, fullPath.str().c_str());
						return false;
					}
				}
			} while (FindNextFileA(searchHandle, &data));
			DWORD error = GetLastError();

			FindClose(searchHandle);

			if (error != ERROR_SUCCESS && error != ERROR_NO_MORE_FILES) {
				Logger::Error("DeleteFolder %s: FindNextFileA = %d\n", path, error);
				return false;
			}
		}

		if (transaction) {
			return RemoveDirectoryTransactedA(path, transaction) != 0;
		} else {
			return RemoveDirectoryA(path) != 0;
		}
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
