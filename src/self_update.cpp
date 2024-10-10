// #include <Windows.h>

#include "self_updater/updater.h"
#include "self_updater/updater_resources.h"

#include "comm/messages.h"
#include "launcher/launcher.h"
#include "launcher/self_update.h"
#include "rapidjson/document.h"
#include "shared/github.h"
#include "shared/logger.h"

namespace Launcher {
	enum SelfUpdateCommState {
		SELF_UPDATE_COMM_WAIT_UPDATER_HELLO,
		SELF_UPDATE_COMM_WAIT_PROCESS_ID_REQUEST
	};

	enum MessageProcessBaseResult {
		MESSAGE_PROCESS_BASE_BAD_MESSAGE,
		MESSAGE_PROCESS_BASE_WRITE_ERROR,
		MESSAGE_PROCESS_BASE_WRITE_OVERLAPPED_ERROR,
		MESSAGE_PROCESS_BASE_WRITE_OVERLAPPED_TIMEOUT_ERROR,
		MESSAGE_PROCESS_BASE_WRITE_NWRITE_ERROR,
		MESSAGE_PROCESS_BASE_OK,
	};

	struct MessageProcessResult {
		SelfUpdateCommState msg;
		MessageProcessBaseResult base;
		union {

		};
	};

	typedef MessageProcessResult(*MessageProcessFn)(HANDLE, char*, DWORD, bool*);

	static constexpr size_t ReadBufferLength = 100;

	struct SelfUpdateCommunicationState {
		char buffer[ReadBufferLength] = { 0 };
		OVERLAPPED readOverlapped;
		SelfUpdateCommState state = SELF_UPDATE_COMM_WAIT_UPDATER_HELLO;
		DWORD nextMessageLen = 0;
		MessageProcessFn messageFn = NULL;
		char context[1024];

		void Configure(SelfUpdateCommState state, DWORD nextMessageLen, MessageProcessFn messageFn,
			const char* context);
	};

	/* Struct used to resume a self update that timed out while waiting for the
	 * self updater to become ready.
	 */
	struct SelfUpdateState {
		/* Handle to the self updater. */
		HANDLE selfUpdaterHandle = INVALID_HANDLE_VALUE;
		/* Handle to the pipe used to sync the updater. */
		HANDLE pipe = INVALID_HANDLE_VALUE;
		/* Whether the updater has connected with the pipe. */
		bool pipeConnected = false;
		bool waiting = false;
		bool connectedNotify = false;
		OVERLAPPED connectOverlapped;

		SelfUpdateCommunicationState commState;

		void Init() {
			memset(&connectOverlapped, 0, sizeof(connectOverlapped));
			memset(&commState.readOverlapped, 0, sizeof(commState.readOverlapped));
		}

		void Reset() {
			selfUpdaterHandle = pipe = INVALID_HANDLE_VALUE;
			pipeConnected = waiting = connectedNotify = false;
			Init();
		}
	};

	static SelfUpdateState updateState;

	static SelfUpdateRunUpdaterResult RunUpdater(std::string const& url, std::string const& version);
	static SelfUpdateRunUpdaterResult HandleUpdaterCommunication();
	static SelfUpdateExtractionResult ExtractUpdater();
	static Github::DownloadAsStringResult FetchReleases(rapidjson::Document& answer);
	static void AbortSelfUpdate(bool reset);

	static MessageProcessResult MessageUpdaterHello(HANDLE, char*, DWORD, bool*);
	static MessageProcessResult MessageUpdaterRequestPID(HANDLE, char*, DWORD, bool*);

	static bool WriteMessage(HANDLE pipe, const void* msg, size_t len, MessageProcessResult* result);

	static const char* ReleasesURL = "https://api.github.com/repos/TeamREPENTOGON/Launcher/releases";
	static const char* SelfUpdaterExePath = "./repentogon_launcher_self_updater.exe";

	Github::DownloadAsStringResult FetchReleases(rapidjson::Document& answer) {
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		std::string releaseString;
		// return Github::FetchReleaseInfo(ReleasesURL, answer, &monitor);
		Github::DownloadAsStringResult result = Github::DownloadAsString(ReleasesURL, releaseString, nullptr);
		if (result != Github::DOWNLOAD_AS_STRING_OK) {
			return result;
		}

		answer.Parse(releaseString.c_str());
		if (answer.HasParseError()) {
			return Github::DOWNLOAD_AS_STRING_INVALID_JSON;
		}

		return Github::DOWNLOAD_AS_STRING_OK;
	}

	SelfUpdateExtractionResult ExtractUpdater() {
		HRSRC updater = FindResource(NULL, MAKEINTRESOURCE(IDB_EMBEDEXE1), RT_RCDATA);
		if (!updater) {
			return SELF_UPDATE_EXTRACTION_ERR_RESOURCE_NOT_FOUND;
		}

		HGLOBAL global = LoadResource(NULL, updater);
		if (!global) {
			return SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOAD_FAILED;
		}

		DWORD size = SizeofResource(NULL, updater);
		if (size == 0) {
			return SELF_UPDATE_EXTRACTION_ERR_BAD_RESOURCE_SIZE;
		}

		void* data = LockResource(global);
		if (!data) {
			return SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOCK_FAILED;
		}

		const char* filename = SelfUpdaterExePath;
		FILE* output = fopen(filename, "wb");
		if (!output) {
			return SELF_UPDATE_EXTRACTION_ERR_OPEN_TEMPORARY_FILE;
		}

		size_t count = fwrite(data, size, 1, output);
		if (count != 1) {
			fclose(output);
			return SELF_UPDATE_EXTRACTION_ERR_WRITTEN_SIZE;
		}

		fclose(output);
		return SELF_UPDATE_EXTRACTION_OK;
	}

	SelfUpdateRunUpdaterResult RunUpdater(std::string const& url, std::string const& version) {
		std::string updateStatePath = "repentogon_launcher_self_updater_state";

		{
			FILE* updateState = fopen(updateStatePath.c_str(), "wb");
			if (!updateState) {
				return SELF_UPDATE_RUN_UPDATER_ERR_OPEN_LOCK_FILE;
			}

			fprintf(updateState, "%d", ::Updater::UpdateState::UPDATE_STATE_INIT);
			fflush(updateState);
			fclose(updateState);
		}

		HANDLE pipe = CreateNamedPipeA(Comm::PipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE, 1, 1024, 1024, 0, NULL);
		if (pipe == INVALID_HANDLE_VALUE) {
			return SELF_UPDATE_RUN_UPDATER_ERR_NO_PIPE;
		}

		char cli[4096] = { 0 };
		int printfCount = snprintf(cli, 4096, "--lock-file=\"%s\" --from=\"%s\" --to=\"%s\" --url=\"%s\"", 
			updateStatePath.c_str(), Launcher::version, version.c_str(), url.c_str());
		if (printfCount < 0) {
			return SELF_UPDATE_RUN_UPDATER_ERR_GENERATE_CLI;
		}

		PROCESS_INFORMATION info;
		memset(&info, 0, sizeof(info));

		STARTUPINFOA startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));

		BOOL ok = CreateProcessA(SelfUpdaterExePath, cli, NULL, NULL, false, 0, NULL, NULL, &startupInfo, &info);
		if (!ok) {
			return SELF_UPDATE_RUN_UPDATER_ERR_CREATE_PROCESS;
		}

		HANDLE child = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, false, info.dwProcessId);
		if (child == INVALID_HANDLE_VALUE) {
			return SELF_UPDATE_RUN_UPDATER_ERR_OPEN_PROCESS;
		}

		updateState.selfUpdaterHandle = child;
		updateState.pipe = pipe;
		updateState.commState.Configure(SELF_UPDATE_COMM_WAIT_UPDATER_HELLO, 
			strlen(Comm::UpdaterHello), 
			MessageUpdaterHello, 
			"waiting for self-updater ready");
		return HandleUpdaterCommunication();
	}

	SelfUpdateRunUpdaterResult HandleUpdaterCommunication() {
		if (!updateState.pipeConnected) {
			BOOL waitResult = WaitNamedPipeA(Comm::PipeName, 1000);
			if (!waitResult) {
				Logger::Info("HandleUpdaterCommunication: WaitNamedPipeA timeout\n");
				return SELF_UPDATE_RUN_UPDATER_INFO_WAIT_TIMEOUT;
			}
			else {
				updateState.pipeConnected = true;
			}
		}

		if (!updateState.connectedNotify) {
			Logger::Info("HandleUpdaterCommunication: self updater ready\n");
			updateState.connectedNotify = true;
			BOOL connectResult = ConnectNamedPipe(updateState.pipe, &updateState.connectOverlapped);
			DWORD lastError = GetLastError();

			if (connectResult == FALSE) {
				if (lastError == ERROR_IO_PENDING) {
					Logger::Error("HandleUpdaterCommunication: received ERROR_IO_PENDING in ConnectNamedPipe\n");
					AbortSelfUpdate(true);
					return;
				}

				if (lastError == ERROR_PIPE_CONNECTED) {
					Logger::Info("HandleUpdaterCommunication: self updater connected (ERROR_PIPE_CONNECTED, this is normal)\n");
				}
			}
			else {
				Logger::Info("HandleUpdaterCommunication: self updater connected\n");
			}
		}
		
		BOOL readResult = ReadFile(updateState.pipe, updateState.commState.buffer, updateState.commState.nextMessageLen, NULL, &updateState.commState.readOverlapped);

		if (readResult == FALSE) {
			DWORD readError = GetLastError();
			if (readError == ERROR_IO_PENDING) {
				Logger::Info("HandleUpdaterCommunication: %s...\n", updateState.commState.context);
				return SELF_UPDATE_RUN_UPDATER_INFO_READFILE_IO_PENDING;
			}
			else {
				Logger::Error("HandleUpdaterCommunication: unexpected error %d in ReadFile\n", readError);
				AbortSelfUpdate(true);
				return SELF_UPDATE_RUN_UPDATER_ERR_READFILE_ERROR;
			}
		}
		else {
			DWORD nRead = 0;
			GetOverlappedResult(updateState.pipe, &updateState.commState.readOverlapped, &nRead, TRUE);

			if (nRead == ReadBufferLength) {
				AbortSelfUpdate(true);
				return SELF_UPDATE_RUN_UPDATER_ERR_READ_OVERFLOW;
			}

			updateState.commState.buffer[nRead] = '\0';
			bool shouldAbort = false;
			MessageProcessResult messageResult = updateState.commState.messageFn(updateState.pipe, updateState.commState.buffer, nRead, &shouldAbort);

			if (shouldAbort) {
				AbortSelfUpdate(true);
			}
		}
	}

	void AbortSelfUpdate(bool reset) {
		TerminateProcess(updateState.selfUpdaterHandle, 1);
		CloseHandle(updateState.pipe);
		CloseHandle(updateState.selfUpdaterHandle);

		if (reset) {
			updateState.Reset();
		}
	}

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPre,
		bool force, std::string& version, std::string& url);

	bool SelfUpdater::IsSelfUpdateAvailable(bool allowDrafts, bool force, 
		std::string& version, std::string& url, Github::DownloadAsStringResult* fetchReleasesResult) {
		Github::DownloadAsStringResult downloadResult = FetchReleases(_releasesInfo);
		if (fetchReleasesResult)
			*fetchReleasesResult = downloadResult;

		if (downloadResult != Github::DOWNLOAD_AS_STRING_OK) {
			return false;
		}

		rapidjson::Document& releases = _releasesInfo;
		_hasRelease = true;
		
		return SelectTargetRelease(releases, allowDrafts, force, version, url);
	}

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPre,
		bool force, std::string& version, std::string& url) {
		auto releaseArray = releases.GetArray();
		for (auto const& release : releaseArray) {
			if (!release["prerelease"].IsTrue() || allowPre) {
				version = release["name"].GetString();
				url = release["url"].GetString();
				return true;
			}
		}

		return false;
	}

	SelfUpdateErrorCode SelfUpdater::DoSelfUpdate(bool allowPreRelease, bool force) {
		SelfUpdateErrorCode result;
		rapidjson::Document releases;

		Github::DownloadAsStringResult releasesResult = FetchReleases(releases);
		if (releasesResult != Github::DOWNLOAD_AS_STRING_OK) {
			result.base = SELF_UPDATE_UPDATE_CHECK_FAILED;
			result.detail.fetchUpdatesResult = releasesResult;
			return result;
		}

		std::string version, url;
		if (!SelectTargetRelease(releases, allowPreRelease, force, version, url)) {
			result.base = SELF_UPDATE_UP_TO_DATE;
			return result;
		}

		return DoSelfUpdate(version, url);
		
	}

	SelfUpdateErrorCode SelfUpdater::DoSelfUpdate(std::string const& version, std::string const& url) {
		SelfUpdateErrorCode result;

		if (updateState.selfUpdaterHandle != INVALID_HANDLE_VALUE) {
			Logger::Fatal("Attempted to perform a full self update while a previous attempt is still in progress\n");
			result.base = SELF_UPDATE_STILLBORN_CHILD;
			return result;
		}

		SelfUpdateExtractionResult extractResult = ExtractUpdater();
		if (extractResult != SELF_UPDATE_EXTRACTION_OK) {
			result.base = SELF_UPDATE_EXTRACTION_FAILED;
			result.detail.extractionResult = extractResult;
			return result;
		}

		SelfUpdateRunUpdaterResult runResult = RunUpdater(url, version);
		result.base = SELF_UPDATE_SELF_UPDATE_FAILED;
		result.detail.runUpdateResult = runResult;

		return result;
	}

	SelfUpdateErrorCode SelfUpdater::ResumeSelfUpdate() {
		SelfUpdateErrorCode result;
		if (updateState.selfUpdaterHandle == INVALID_HANDLE_VALUE) {
			result.base = SELF_UPDATE_SELF_UPDATE_FAILED;
			result.detail.runUpdateResult = SELF_UPDATE_RUN_UPDATER_ERR_INVALID_RESUME;
			return result;
		}


		SelfUpdateRunUpdaterResult runResult = HandleUpdaterCommunication();
		/* Remember: if the above function returns, then the updater is not launched.
		 * If the updater is launched, the function never returns.
		 */

		result.base = SELF_UPDATE_SELF_UPDATE_FAILED;
		result.detail.runUpdateResult = runResult;

		return result;
	}

	void SelfUpdater::AbortSelfUpdate(bool reset) {
		Launcher::AbortSelfUpdate(reset);
	}

	MessageProcessResult MessageUpdaterHello(HANDLE pipe, char* message, DWORD nRead, bool* shouldAbort) {
		MessageProcessResult result;
		result.msg = SELF_UPDATE_COMM_WAIT_UPDATER_HELLO;

		if (strcmp(message, Comm::UpdaterHello)) {
			result.base = MESSAGE_PROCESS_BASE_BAD_MESSAGE;
			return result;
		}

		if (WriteMessage(pipe, Comm::LauncherHello, strlen(Comm::LauncherHello), &result)) {
			result.base = MESSAGE_PROCESS_BASE_OK;
			updateState.commState.Configure(
				SELF_UPDATE_COMM_WAIT_PROCESS_ID_REQUEST,
				strlen(Comm::UpdaterRequestPID),
				MessageUpdaterRequestPID,
				"request for launcher PID"
			);
		}
		else {
			*shouldAbort = true;
		}

		return result;
	}

	MessageProcessResult MessageUpdaterRequestPID(HANDLE pipe, char* message, DWORD nRead, bool* shouldAbort) {
		MessageProcessResult result;
		result.msg = SELF_UPDATE_COMM_WAIT_PROCESS_ID_REQUEST;

		if (strcmp(message, Comm::UpdaterRequestPID)) {
			result.base = MESSAGE_PROCESS_BASE_BAD_MESSAGE;
			return result;
		}

		if (WriteMessage(pipe, Comm::LauncherAnswerPID, strlen(Comm::LauncherAnswerPID), &result)) {
			DWORD pid = GetProcessId(NULL);
			if (WriteMessage(pipe, &pid, sizeof(pid), &result)) {
				result.base = MESSAGE_PROCESS_BASE_OK;
			}
		}

		return result;
	}

	bool WriteMessage(HANDLE pipe, const void* msg, size_t len, MessageProcessResult* result) {
		OVERLAPPED write;
		memset(&write, 0, sizeof(write));
		BOOL writeResult = WriteFile(pipe, msg, len, NULL, &write);

		if (writeResult == FALSE && GetLastError() != ERROR_IO_PENDING) {
			result->base = MESSAGE_PROCESS_BASE_WRITE_ERROR;
			return false;
		}

		DWORD nWritten = 0;
		BOOL overlappedResult = GetOverlappedResultEx(pipe, &write, &nWritten, 3000, TRUE);

		if (overlappedResult == FALSE) {
			if (GetLastError() == ERROR_IO_PENDING) {
				result->base = MESSAGE_PROCESS_BASE_WRITE_OVERLAPPED_TIMEOUT_ERROR;
			}
			else {
				result->base = MESSAGE_PROCESS_BASE_WRITE_OVERLAPPED_ERROR;
			}
			return false;
		}

		if (nWritten != len) {
			result->base = MESSAGE_PROCESS_BASE_WRITE_NWRITE_ERROR;
			return false;
		}

		return true;
	}

	void SelfUpdateCommunicationState::Configure(SelfUpdateCommState state, DWORD nextMessageLen, MessageProcessFn messageFn,
		const char* context) {
		memset(&readOverlapped, 0, sizeof(readOverlapped));
		memset(buffer, 0, ReadBufferLength);
		this->state = state;
		this->nextMessageLen = nextMessageLen;
		this->messageFn = messageFn;
		strcpy(this->context, context);
	}
}