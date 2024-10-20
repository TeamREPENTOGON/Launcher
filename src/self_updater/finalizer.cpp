#include <string>

#include "comm/messages.h"
#include "launcher/self_updater/finalizer.h"
#include "shared/logger.h"
#include "unpacker/unpacker_resources.h"

namespace Updater {
	static const char* SelfUpdaterExePath = "./repentogon_launcher_update_unpacker.exe";

	static bool WriteMessage(HANDLE pipe, const void* msg, size_t len, MessageProcessResult* result);

	Finalizer::Finalizer() {
		Init();
	}

	void Finalizer::Init() {
		memset(&readOverlapped, 0, sizeof(readOverlapped));
		memset(&connectOverlapped, 0, sizeof(connectOverlapped));
	}

	FinalizationResult Finalizer::Finalize() {
		FinalizationExtractionResult extractionResult = ExtractUnpacker();
		if (extractionResult != FINALIZATION_EXTRACTION_OK) {
			Logger::Error("Finalizer::Finalize: error while extracting unpacker (%d)\n", extractionResult);
			return extractionResult;
		}

		FinalizationStartUnpackerResult startResult = StartUnpacker();
		if (startResult != FINALIZATION_START_UNPACKER_OK) {
			Logger::Error("Finalizer::Finalize: error while starting unpacker (%d)\n", startResult);
			return startResult;
		}

		FinalizationCommunicationResult commResult = SynchronizeUnpacker();
		return commResult;
	}

	FinalizationCommunicationResult Finalizer::ResumeFinalize() {
		if (pipe == NULL || unpacker == NULL) {
			Logger::Error("Finalizer::ResumeFinalize: invalid resume (pipe = %p, unpacker = %p)\n", pipe, unpacker);
			return FINALIZATION_COMM_ERR_INVALID_RESUME;
		}

		return SynchronizeUnpacker();
	}

	FinalizationExtractionResult Finalizer::ExtractUnpacker() {
		HRSRC updater = FindResource(NULL, MAKEINTRESOURCE(IDB_EMBEDEXE1), RT_RCDATA);
		if (!updater) {
			return FINALIZATION_EXTRACTION_ERR_RESOURCE_NOT_FOUND;
		}

		HGLOBAL global = LoadResource(NULL, updater);
		if (!global) {
			return FINALIZATION_EXTRACTION_ERR_RESOURCE_LOAD_FAILED;
		}

		DWORD size = SizeofResource(NULL, updater);
		if (size == 0) {
			return FINALIZATION_EXTRACTION_ERR_BAD_RESOURCE_SIZE;
		}

		void* data = LockResource(global);
		if (!data) {
			return FINALIZATION_EXTRACTION_ERR_RESOURCE_LOCK_FAILED;
		}

		const char* filename = SelfUpdaterExePath;
		FILE* output = fopen(filename, "wb");
		if (!output) {
			return FINALIZATION_EXTRACTION_ERR_OPEN_TEMPORARY_FILE;
		}

		size_t count = fwrite(data, size, 1, output);
		if (count != 1) {
			fclose(output);
			return FINALIZATION_EXTRACTION_ERR_WRITTEN_SIZE;
		}

		fclose(output);
		return FINALIZATION_EXTRACTION_OK;
	}

	FinalizationStartUnpackerResult Finalizer::StartUnpacker() {
		std::string updateStatePath = "repentogon_launcher_self_updater_state";

		/* {
			FILE* updateState = fopen(updateStatePath.c_str(), "wb");
			if (!updateState) {
				return SELF_UPDATE_RUN_UPDATER_ERR_OPEN_LOCK_FILE;
			}

			fprintf(updateState, "%d", ::Updater::UpdateState::UPDATE_STATE_INIT);
			fflush(updateState);
			fclose(updateState);
		} */

		HANDLE pipe = CreateNamedPipeA(Comm::PipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE, 2, 1024, 1024, 0, NULL);
		if (pipe == INVALID_HANDLE_VALUE) {
			Logger::Error("RunUnpacker: error while creating pipe (%d)\n", GetLastError());
			return FINALIZATION_START_UNPACKER_ERR_NO_PIPE;
		}

		Logger::Info("RunUnpacker: created communication pipe (%p, %d)\n", pipe, GetLastError());
		char cli[4096] = { 0 };

		PROCESS_INFORMATION info;
		memset(&info, 0, sizeof(info));

		STARTUPINFOA startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));

		BOOL ok = CreateProcessA(SelfUpdaterExePath, cli, NULL, NULL, false, 0, NULL, NULL, &startupInfo, &info);
		if (!ok) {
			return FINALIZATION_START_UNPACKER_ERR_CREATE_PROCESS;
		}

		HANDLE child = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, false, info.dwProcessId);
		if (child == INVALID_HANDLE_VALUE) {
			return FINALIZATION_START_UNPACKER_ERR_OPEN_PROCESS;
		}

		unpacker = child;
		pipe = pipe;
		
		ConfigureNextMessage(MESSAGE_UNPACKER_HELLO, strlen(Comm::UpdaterHello), &Finalizer::ProcessHelloMessage, "waiting for hello from unpacker");

		return FINALIZATION_START_UNPACKER_OK;
	}

	FinalizationCommunicationResult Finalizer::SynchronizeUnpacker() {
		if (!pipeConnected) {
			FinalizationCommunicationResult connectResult = ConnectPipe();
			if (connectResult != FINALIZATION_COMM_INTERNAL_OK) {
				return connectResult;
			}
		}

		if (waitUntilDeath) {
			WaitUntilDeath();
			return FINALIZATION_COMM_ERR_STILL_ALIVE;
		}
		else {
			FinalizationCommunicationResult result = ProcessNextMessage();

			if (waitUntilDeath)
				return SynchronizeUnpacker();

			return result;
		}
	}

	FinalizationCommunicationResult Finalizer::ConnectPipe() {
		BOOL connectResult = ConnectNamedPipe(pipe, &connectOverlapped);
		if (!connectResult) {
			DWORD error = GetLastError();
			if (error == ERROR_IO_PENDING) {
				DWORD dummy;
				connectResult = GetOverlappedResultEx(pipe, &connectOverlapped, &dummy, 2000, FALSE);
				if (!connectResult) {
					error = GetLastError();
					if (error == WAIT_TIMEOUT) {
						Logger::Info("Finalizer::ConnectPipe: timeout while waiting for client\n");
						return FINALIZATION_COMM_INFO_TIMEOUT;
					}
					else {
						Logger::Error("Finalizer::ConnectPipe: unexpected error while waiting for client (%d)\n", error);
						return FINALIZATION_COMM_ERR_CONNECT_ERR;
					}
				}

				// Safe fallthrough
			}
			else if (error != ERROR_PIPE_CONNECTED) {
				Logger::Error("Finalizer::ConnectPipe: ConnectNamedPipe error (%d)\n", error);
				return FINALIZATION_COMM_ERR_CONNECT_ERR;
			}

			// Safe fallthrough
		}

		pipeConnected = true;
		Logger::Info("FinalizationManager::Finalize: client connected\n");
		return FINALIZATION_COMM_INTERNAL_OK;
	}

	FinalizationCommunicationResult Finalizer::ProcessNextMessage() {
		BOOL readResult = ReadFile(pipe, message, nextMessageLength, NULL, &readOverlapped);

		if (readResult == FALSE) {
			DWORD readError = GetLastError();
			if (readError == ERROR_IO_PENDING) {
				Logger::Info("Finalizer::ProcessNextMessage: %s...\n", messageContext);
				return FINALIZATION_COMM_INFO_TIMEOUT;
			}
			else {
				Logger::Error("Finalizer::ProcessNextMessage: unexpected error %d in ReadFile\n", readError);
				return FINALIZATION_COMM_ERR_READFILE_ERROR;
			}
		}
		else {
			DWORD nRead = 0;
			GetOverlappedResult(pipe, &readOverlapped, &nRead, TRUE);

			if (nRead == MaxMessageLength) {
				Logger::Error("Finalizer::ProcessNextMessage: overflow when reading from pipe\n");
				return FINALIZATION_COMM_ERR_READ_OVERFLOW;
			}

			message[nRead] = '\0';
			MessageProcessResult messageResult = (this->*nextMessageFn)();
			if (messageResult != MESSAGE_PROCESS_OK) {
				Logger::Error("Finalizer::ProcessNextMessage: error while processing message %d (%d)\n",
					nextMessage, messageResult);
				return FINALIZATION_COMM_ERR_MESSAGE_ERROR;
			}
		}

		return FINALIZATION_COMM_INTERNAL_OK;
	}

	void Finalizer::WaitUntilDeath() {
		Sleep(10000);
	}

	void Finalizer::ConfigureNextMessage(Messages next, DWORD length, MessageProcessFn fn,
		const char* ctx) {
		nextMessage = next;
		nextMessageLength = length;
		nextMessageFn = fn;

		memset(&readOverlapped, 0, sizeof(readOverlapped));
		memset(message, 0, sizeof(message));

		strcpy(messageContext, ctx);
	}

	MessageProcessResult Finalizer::ProcessHelloMessage() {
		if (strcmp(message, Comm::UpdaterHello)) {
			return MESSAGE_PROCESS_ERR_BAD_MESSAGE;
		}

		MessageProcessResult result;
		if (WriteMessage(pipe, Comm::LauncherHello, strlen(Comm::LauncherHello), &result)) {
			ConfigureNextMessage(MESSAGE_REQUEST_PID, strlen(Comm::UpdaterRequestPID), &Finalizer::ProcessRequestPIDMessage, "waiting for PID request from unpacker");
		}

		return result;
	}

	MessageProcessResult Finalizer::ProcessRequestPIDMessage() {
		if (strcmp(message, Comm::UpdaterRequestPID)) {
			return MESSAGE_PROCESS_ERR_BAD_MESSAGE;
		}

		MessageProcessResult result;
		if (WriteMessage(pipe, Comm::LauncherAnswerPID, strlen(Comm::LauncherAnswerPID), &result)) {
			DWORD pid = GetCurrentProcessId();
			Logger::Info("MessageUpdaterRequestPID: sending PID %d\n", pid);
			WriteMessage(pipe, &pid, sizeof(pid), &result);
		}

		return result;
	}

	bool WriteMessage(HANDLE pipe, const void* msg, size_t len, MessageProcessResult* result) {
		OVERLAPPED write;
		memset(&write, 0, sizeof(write));
		BOOL writeResult = WriteFile(pipe, msg, len, NULL, &write);

		if (writeResult == FALSE && GetLastError() != ERROR_IO_PENDING) {
			*result = MESSAGE_PROCESS_ERR_WRITE;
			return false;
		}

		DWORD nWritten = 0;
		BOOL overlappedResult = GetOverlappedResultEx(pipe, &write, &nWritten, 3000, TRUE);

		if (overlappedResult == FALSE) {
			if (GetLastError() == ERROR_IO_PENDING) {
				*result = MESSAGE_PROCESS_ERR_WRITE_OVERLAPPED_TIMEOUT;
			}
			else {
				*result = MESSAGE_PROCESS_ERR_WRITE_OVERLAPPED;
			}
			return false;
		}

		if (nWritten != len) {
			*result = MESSAGE_PROCESS_ERR_WRITE_NWRITE;
			return false;
		}

		*result = MESSAGE_PROCESS_OK;
		return true;
	}
}