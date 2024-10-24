#include <WinSock2.h>
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "comm/messages.h"
#include "unpacker/logger.h"
#include "unpacker/synchronization.h"
#include "unpacker/unpacker.h"

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

bool Unpacker::ExtractArchive(const char* name) {
	class ScopedFile {
	public:
		ScopedFile(FILE* f) : _f(f) { }
		~ScopedFile() { if (_f) fclose(_f); }

	private:
		FILE* _f = NULL;
	};

	FILE* f = fopen(name, "rb");
	ScopedFile scopedFile(f);

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
	std::vector<FileContent> files;
	for (int i = 0; i < nFiles; ++i) {
		size_t nameLen = 0;
		if (fread(&nameLen, sizeof(nameLen), 1, f) != 1) {
			Logger::Error("Failed to read length of filename\n");
			return false;
		}

		FileContent file;
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

	Logger::Info("Read all files\n");

	for (FileContent const& fileDesc : files) {
		FILE* file = fopen(fileDesc.name, "wb");
		if (!file) {
			Logger::Error("Error while opening %s\n", fileDesc.name);
			return false;
		}

		if (fwrite(fileDesc.buffer, fileDesc.buffLen, 1, file) != 1) {
			Logger::Error("Error while extracting %s\n", fileDesc.name);
			return false;
		}

		fclose(file);
	}

	return true;
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

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Logger::Init("unpacker.log");

	Synchronization::SynchronizationResult result = Synchronization::SynchronizeWithLauncher();
	if (result != Synchronization::SYNCHRONIZATION_OK) {
		Logger::Error("Error while synchronizing with launcher\n");
		return -1;
	}

	if (!Unpacker::ExtractArchive(Comm::UnpackedArchiveName)) {
		Logger::Error("Error while extracing archive\n");
		return -1;
	}

	if (!DeleteFileA(Comm::UnpackedArchiveName)) {
		Logger::Error("Error while deleting archive (%d)\n", GetLastError());
		return -1;
	}

	Unpacker::StartLauncher();

	return 0;
}