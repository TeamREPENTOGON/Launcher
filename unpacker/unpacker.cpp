#include <WinSock2.h>
#include <Windows.h>
#include <ktmw32.h>
#include <WinBase.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "comm/messages.h"
#include "unpacker/logger.h"
#include "unpacker/synchronization.h"
#include "unpacker/unpacker.h"
#include "unpacker/utils.h"

const char* Unpacker::ResumeArg = "--resume";
const char* Unpacker::ForcedArg = "--force";

namespace Unpacker {
	struct FileContent {
		char* name = NULL;
		char* buffer = NULL;
		size_t buffLen = 0;

		FileContent() {

		}

		~FileContent() {
			free(name);
			free(buffer);
		}

		FileContent(FileContent const&) = delete;
		FileContent& operator=(FileContent const&) = delete;

		FileContent(FileContent&& other) : name(other.name), buffer(other.buffer), buffLen(other.buffLen) {
			other.name = other.buffer = NULL;
			other.buffLen = 0;
		}

		FileContent& operator=(FileContent&& other) {
			name = other.name;
			buffer = other.buffer;
			buffLen = other.buffLen;

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
	Unpacker::Utils::ScopedFile scopedFile(f);

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

	Utils::ScopedHandle scopedHandle(transaction);

	for (FileContent const& content : files) {
		HANDLE file = CreateFileTransactedA(content.name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 
			FILE_ATTRIBUTE_NORMAL, NULL, transaction, 0, NULL);
		if (file == INVALID_HANDLE_VALUE) {
			Logger::Error("Unpacker::MemoryToDisk: error while creating file %s (%d)\n", content.name, GetLastError());
			RollbackTransaction(transaction);
			return false;
		}

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

void Unpacker::StartLauncher() {
	STARTUPINFOA startup;
	memset(&startup, 0, sizeof(startup));

	PROCESS_INFORMATION info;
	memset(&info, 0, sizeof(info));

	char cli[] = { 0 };
	BOOL created = CreateProcessA("REPENTOGONLauncher.exe", cli, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info);
	if (!created) {
		ExitProcess(-1);
	}

	ExitProcess(0);
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR cli, int) {
	Logger::Init("unpacker.log");

	int argc = 0;
	char** argv = Unpacker::Utils::CommandLineToArgvA(cli, &argc);
	HANDLE lockFile = NULL;

	Unpacker::Utils::LockUnpackerResult lockResult = Unpacker::Utils::LockUnpacker(&lockFile);

	if (lockResult != Unpacker::Utils::LOCK_UNPACKER_SUCCESS) {
		bool isForced = Unpacker::Utils::IsForced(argc, argv);

		if (!isForced) {
			if (lockResult == Unpacker::Utils::LOCK_UNPACKER_ERR_INTERNAL) {
				MessageBoxA(NULL, "Fatal error", "Unable to check if another instance of the unpacker is already running\n"
					"Check the log file (you may need to launch Isaac normaly at least once; otherwise rerun with --force)\n", MB_ICONERROR);
				Logger::Fatal("Error while attempting to lock the unpacker\n");
			} else {
				MessageBoxA(NULL, "Fatal error", "Another instance of the unpacker is already running, "
					"terminate it first then restart the unpacker\n"
					"(If no other instance of the updater is running, rerun with --force)\n", MB_ICONERROR);
				Logger::Fatal("Unpacker is already running, terminate other instances first\n");
			}

			return -1;
		} else {
			Logger::Warn("Cannot take unpacker lock, but ignoring (--force given)\n");
		}
	}

	Unpacker::Utils::ScopedHandle scopedHandle(lockFile);

	if (!Unpacker::Utils::IsContinuation(argc, argv)) {
		Logger::Info("Running in non continuation mode, synchronizing with the launcher\n");
		Synchronization::SynchronizationResult result = Synchronization::SynchronizeWithLauncher();
		if (result != Synchronization::SYNCHRONIZATION_OK) {
			MessageBoxA(NULL, "Fatal error", "Unable to synchronize with the launcher\n"
				"If you are running the unpacker as a standalone, rerun it with --resume\n", MB_ICONERROR);
			Logger::Fatal("Error while synchronizing with launcher\n");
			return -1;
		}
	} else {
		Logger::Info("Running in continuation mode, skipping synchronization\n");
	}

	if (!Unpacker::ExtractArchive(Comm::UnpackedArchiveName)) {
		MessageBoxA(NULL, "Fatal error", "Unable to unpack the update, check log file\n", MB_ICONERROR);
		Logger::Fatal("Error while extracting archive\n");
		return -1;
	}

	if (!DeleteFileA(Comm::UnpackedArchiveName)) {
		MessageBoxA(NULL, "Fatal error", "Unable to delete the archive containing the update.\n"
			"Delete the archive (launcher_update.bin) then you can start the launcher\n", MB_ICONERROR);
		Logger::Fatal("Error while deleting archive (%d)\n", GetLastError());
		return -1;
	}

	Unpacker::StartLauncher();

	return 0;
}