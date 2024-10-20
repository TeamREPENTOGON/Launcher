#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include <variant>

namespace Updater {
	enum FinalizationExtractionResult {
		FINALIZATION_EXTRACTION_OK,
		FINALIZATION_EXTRACTION_ERR_RESOURCE_NOT_FOUND,
		FINALIZATION_EXTRACTION_ERR_RESOURCE_LOAD_FAILED,
		FINALIZATION_EXTRACTION_ERR_BAD_RESOURCE_SIZE,
		FINALIZATION_EXTRACTION_ERR_RESOURCE_LOCK_FAILED,
		FINALIZATION_EXTRACTION_ERR_OPEN_TEMPORARY_FILE,
		FINALIZATION_EXTRACTION_ERR_WRITTEN_SIZE
	};

	enum FinalizationStartUnpackerResult {
		FINALIZATION_START_UNPACKER_OK,
		FINALIZATION_START_UNPACKER_ERR_NO_PIPE,
		// FINALIZATION_START_UNPACKER_ERR_OPEN_LOCK_FILE,
		FINALIZATION_START_UNPACKER_ERR_CREATE_PROCESS,
		FINALIZATION_START_UNPACKER_ERR_OPEN_PROCESS
	};

	enum FinalizationCommunicationResult {
		FINALIZATION_COMM_INFO_TIMEOUT,
		FINALIZATION_COMM_ERR_CONNECT_ERR,
		FINALIZATION_COMM_ERR_READFILE_ERROR,
		FINALIZATION_COMM_ERR_INVALID_PING,
		FINALIZATION_COMM_ERR_INVALID_RESUME,
		FINALIZATION_COMM_ERR_READ_OVERFLOW,
		FINALIZATION_COMM_ERR_MESSAGE_ERROR,
		FINALIZATION_COMM_ERR_STILL_ALIVE,
		FINALIZATION_COMM_FATAL_TIMEOUT,
		FINALIZATION_COMM_INTERNAL_OK
	};

	enum Messages {
		MESSAGE_UNPACKER_HELLO,
		MESSAGE_REQUEST_PID,
	};

	enum MessageProcessResult {
		MESSAGE_PROCESS_OK,
		MESSAGE_PROCESS_ERR_BAD_MESSAGE,
		MESSAGE_PROCESS_ERR_WRITE,
		MESSAGE_PROCESS_ERR_WRITE_OVERLAPPED,
		MESSAGE_PROCESS_ERR_WRITE_OVERLAPPED_TIMEOUT,
		MESSAGE_PROCESS_ERR_WRITE_NWRITE
	};

	typedef std::variant<FinalizationExtractionResult, FinalizationStartUnpackerResult, FinalizationCommunicationResult> FinalizationResult;

	class Finalizer {
	public:
		Finalizer();
		FinalizationResult Finalize();
		FinalizationCommunicationResult ResumeFinalize();

	private:
		static constexpr size_t MaxMessageLength = 100;
		typedef MessageProcessResult(Finalizer::* MessageProcessFn)();

		HANDLE unpacker = NULL;			/* Handle to the unpacker process. */
		HANDLE pipe = NULL;				/* Pipe to communicate with the unpacker. */
		bool pipeConnected = false;		/* Whether unpacker is connected or not. */
		OVERLAPPED connectOverlapped;	/* Overlapped structure for ConnectNamedPipe. */
		bool waitUntilDeath = false;	/* True if waiting for unpacker to kill us. */
		OVERLAPPED readOverlapped;		/* Overlapped structure for ReadFile. */
		Messages nextMessage;			/* ID of the next message. */
		DWORD nextMessageLength;		/* Expected size of the next message. */
		char message[MaxMessageLength] = { 0 }; /* Content of the message. */
		char messageContext[1024] = { 0 }; /* Debug string. */
		MessageProcessFn nextMessageFn; /* Function to process next message. */

		void Init();

		void ConfigureNextMessage(Messages next, DWORD length, MessageProcessFn fn,
			const char* ctx);

		FinalizationCommunicationResult ConnectPipe();
		void WaitUntilDeath();
		FinalizationCommunicationResult ProcessNextMessage();

		MessageProcessResult ProcessHelloMessage();
		MessageProcessResult ProcessRequestPIDMessage();

		FinalizationExtractionResult ExtractUnpacker();
		FinalizationStartUnpackerResult StartUnpacker();
		FinalizationCommunicationResult SynchronizeUnpacker();
	};
}