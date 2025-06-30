#include <WinSock2.h>
#include <ktmw32.h>
#include <vector>

#include "shared/logger.h"
#include "self_updater/unpacker.h"
#include "self_updater/utils.h"
#include "shared/filesystem.h"

namespace Unpacker {
	struct FileContent {
		char* name = NULL;
		char* buffer = NULL;
		size_t buffLen = 0;
		bool isFolder = false;

		FileContent() {

		}

		~FileContent() {
			free(name);
			free(buffer);
		}

		FileContent(FileContent const&) = delete;
		FileContent& operator=(FileContent const&) = delete;

		FileContent(FileContent&& other) noexcept : name(other.name), buffer(other.buffer),
			buffLen(other.buffLen), isFolder(other.isFolder) {
			other.name = other.buffer = NULL;
			other.buffLen = 0;
		}

		FileContent& operator=(FileContent&& other) noexcept {
			name = other.name;
			buffer = other.buffer;
			buffLen = other.buffLen;
			isFolder = other.isFolder;

			other.name = other.buffer = NULL;
			other.buffLen = 0;
			return *this;
		}
	};

	static bool MemoryUnpack(const char* name, std::vector<FileContent>& files);
	static bool MemoryToDisk(std::vector<FileContent> const& files);
}

bool Unpacker::MemoryUnpack(const char* name, std::vector<Unpacker::FileContent>& files) {
	FILE* f = fopen(name, "rb");
	Updater::Utils::ScopedFile scopedFile(f);

	if (!f) {
		Logger::Error("Failed to open file %s\n", name);
		return false;
	}

	int nFiles = 0;
	if (fread(&nFiles, sizeof(nFiles), 1, f) != 1) {
		Logger::Error("Failed to read number of files to unpack\n");
		return false;
	}

	Logger::Info("Reading %d files\n", nFiles);
	for (int i = 0; i < nFiles; ++i) {
		size_t nameLen = 0;
		if (fread(&nameLen, sizeof(nameLen), 1, f) != 1) {
			Logger::Error("Failed to read length of filename\n");
			return false;
		}

		Unpacker::FileContent file;
		file.name = (char*)malloc(nameLen + 1);
		if (!file.name) {
			Logger::Error("Unable to allocate memory to store filename\n");
			return false;
		}

		if (fread(file.name, nameLen, 1, f) != 1) {
			Logger::Error("Error while reading filename\n");
			return false;
		}

		file.name[nameLen] = '\0';

		uint64_t fileSize = 0;
		if (fread(&fileSize, sizeof(fileSize), 1, f) != 1) {
			Logger::Error("Error while reading length of file\n");
			return false;
		}

		if (fileSize != 0) {
			file.buffer = (char*)malloc(fileSize + 1);
			if (!file.buffer) {
				Logger::Error("Unable to allocate memory to store file content\n");
				return false;
			}

			if (fread(file.buffer, fileSize, 1, f) != 1) {
				Logger::Error("Error while reading content of file\n");
				return false;
			}

			file.buffer[fileSize] = '\0';
		}

		file.isFolder = fileSize == 0 &&
			(file.name[fileSize - 1] == '/' || file.name[fileSize - 1] == '\\');
		file.buffLen = fileSize;
		Logger::Info("Read %s of size %d\n", file.name, fileSize);
		files.push_back(std::move(file));
	}

	return true;
}

bool Unpacker::MemoryToDisk(std::vector<FileContent> const& files) {
	wchar_t description[] = L"Unpacker memory to disk unpacking";
	HANDLE transaction = CreateTransaction(NULL, NULL, 0, 0, 0, 0, description);
	if (transaction == INVALID_HANDLE_VALUE) {
		Logger::Error("Unpacker::MemoryToDisk: unable to create transaction (%d)\n", GetLastError());
		return false;
	}

	Updater::Utils::ScopedHandle scopedHandle(transaction);

	for (FileContent const& content : files) {
		HANDLE file = INVALID_HANDLE_VALUE;
		bool createdFolder = false;

		if (content.isFolder && Filesystem::Exists(content.name)) {
			if (!Filesystem::DeleteFolder(content.name, transaction)) {
				Logger::Error("Unpacker::MemoryToDisk: unable to delete folder %s\n", content.name);
			} else {
				BOOL ok = CreateDirectoryTransactedA(NULL, content.name, NULL, transaction);
				createdFolder = (ok != 0);

				if (!ok) {
					Logger::Error("Unpacker::MemoryToDisk: unable to create folder %s (%d)\n", content.name, GetLastError());
				}
			}
		} else {
			file = CreateFileTransactedA(content.name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, NULL, transaction, 0, NULL);
		}

		if (file == INVALID_HANDLE_VALUE && !createdFolder) {
			Logger::Error("Unpacker::MemoryToDisk: error while creating file %s (%d)\n", content.name, GetLastError());
			RollbackTransaction(transaction);
			return false;
		}

		if (content.buffLen != 0) {
			DWORD bytesWritten = 0;
			if (!WriteFile(file, content.buffer, content.buffLen, &bytesWritten, NULL)) {
				Logger::Error("Unpacker::MemoryToDisk: error while writing file %s (%d)\n", content.name, GetLastError());
				CloseHandle(file);
				RollbackTransaction(transaction);
				return false;
			}

			if (bytesWritten != content.buffLen) {
				Logger::Error("Unpacker::MemoryToDisk: incorrect amount of bytes written for file %s (expected %d, got %d)\n",
					content.name, content.buffLen, bytesWritten);
				CloseHandle(file);
				RollbackTransaction(transaction);
				return false;
			}
		}

		CloseHandle(file);
	}

	if (!CommitTransaction(transaction)) {
		Logger::Error("Unpacker::MemoryToDisk: error while commiting transaction\n");
		return false;
	}

	return true;
}

bool Unpacker::ExtractArchive(const char* name) {
	std::vector<Unpacker::FileContent> files;
	if (!MemoryUnpack(name, files)) {
		Logger::Error("Unpacker::ExtractArchive: error while unpacking in memory\n");
		return false;
	}

	Logger::Info("Read all files\n");

	return MemoryToDisk(files);
}