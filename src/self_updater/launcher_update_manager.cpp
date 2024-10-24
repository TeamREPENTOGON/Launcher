#include <WinSock2.h>

#include <chrono>
#include <future>

#include "wx/cmdline.h"

#include "shared/externals.h"
#include "shared/github.h"
#include "shared/logger.h"
#include "unpacker/synchronization.h"
#include "launcher/self_updater/launcher_update_manager.h"

namespace Externals {
	void Init();
}

namespace Updater {
	char LauncherUpdateManager::_printBuffer[LauncherUpdateManager::BUFF_SIZE] = { 0 };

	LauncherUpdateManager::LauncherUpdateManager(ILoggableGUI* gui) : _gui(gui) {

	}

	bool LauncherUpdateManager::DoPreUpdateChecks() {
		UpdateStartupCheckResult startupCheck = _updater.DoStartupCheck();
		switch (startupCheck) {
		case UPDATE_STARTUP_CHECK_CANNOT_FETCH_RELEASE:
			_gui->LogError("Unable to download release information from GitHub");
			LogGithubDownloadAsString("Download release information", _updater.GetReleaseDownloadResult());
			return false;

		case UPDATE_STARTUP_CHECK_INVALID_RELEASE_INFO:
			_gui->LogError("Release information is invalid");
			switch (_updater.GetReleaseInfoState()) {
			case RELEASE_INFO_STATE_NO_ASSETS:
				_gui->LogError("The release contains neither a hash file nor a launcher archive file");
				break;

			case RELEASE_INFO_STATE_NO_HASH:
				_gui->LogError("The release contains no hash file");
				break;

			case RELEASE_INFO_STATE_NO_ZIP:
				_gui->LogError("The release contains no launcher archive file");
				break;

			default:
				_gui->LogError("Sylmir forgot to handle this case");
				break;
			}
		}

		return true;
	}

	bool LauncherUpdateManager::DownloadUpdate(LauncherUpdateData* updateData) {
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		std::future<bool> future = std::async(std::launch::async, &LauncherUpdater::DownloadUpdate,
			&_updater, updateData);
		size_t totalDownloadSize = 0;
		struct {
			Threading::Monitor<Github::GithubDownloadNotification>* monitor;
			std::string name;
			bool done;
		} monitorAndName[] = {
			{ &updateData->_hashMonitor, "Hash file", false },
			{ &updateData->_zipMonitor, "Launcher archive", false },
			{ NULL, "" }
		};

		std::chrono::steady_clock::time_point lastReceived = std::chrono::steady_clock::now();
		while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready && 
			std::any_of(monitorAndName, monitorAndName + 2, [](auto const& monitor) -> bool { return !monitor.done;  })) {
			for (auto s = monitorAndName; s->monitor; ++s) {
				while (std::optional<Github::GithubDownloadNotification> message = s->monitor->Get()) {
					switch (message->type) {
					case Github::GH_NOTIFICATION_INIT_CURL:
						_gui->Log("[%s] Initializing cURL connection to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_INIT_CURL_DONE:
						_gui->Log("[%s] Initialized cURL connection to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_CURL_PERFORM:
						_gui->Log("[%s] Performing cURL request to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_CURL_PERFORM_DONE:
						_gui->Log("[%s] Performed cURL request to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_DATA_RECEIVED:
					{
						totalDownloadSize += std::get<size_t>(message->data);

						std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
						if (std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastReceived).count() > 100000000) {
							_gui->Log("[%s] Downloaded %lu bytes\n", s->name.c_str(), totalDownloadSize);
							lastReceived = now;
						}
						break;
					}

					case Github::GH_NOTIFICATION_PARSE_RESPONSE:
						_gui->Log("[%s] Parsing result of cURL request from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_PARSE_RESPONSE_DONE:
						_gui->Log("[%s] Parsed result of cURL request from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_DONE:
						s->done = true;
						_gui->Log("[%s] Successfully downloaded content from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					default:
						_gui->LogError("[%s] Unexpected asynchronous notification (id = %d)\n", s->name.c_str(), message->type);
						break;
					}
				}
			}
		}

		/* async synchronizes-with get, so all non atomic accesses become visible
		 * side-effects here. Therefore, there is no need to introduce a fence in
		 * order to read the content of updateData.
		 */
		return future.get();
	}

	bool LauncherUpdateManager::PostDownloadChecks(bool downloadOk, LauncherUpdateData* data) {
		if (!downloadOk) {
			_gui->LogError("Error while downloading launcher update");

			if (data->_hashDownloadResult != Github::DOWNLOAD_AS_STRING_OK) {
				LogGithubDownloadAsString("hash download", data->_hashDownloadResult);
			}

			if (data->_zipDownloadResult != Github::DOWNLOAD_FILE_OK) {
				switch (data->_zipDownloadResult) {
				case Github::DOWNLOAD_FILE_BAD_CURL:
					_gui->LogError("Launcher archive: error while initializeing cURL connection to %s", data->_zipUrl.c_str());
					break;

				case Github::DOWNLOAD_FILE_BAD_FS:
					_gui->LogError("Launcher archive: error while creating file %s on disk", data->_zipFileName.c_str());
					break;

				case Github::DOWNLOAD_FILE_DOWNLOAD_ERROR:
					_gui->LogError("Launcher archive: error while downloading archive from %s", data->_zipUrl.c_str());
					break;

				default:
					_gui->LogError("Launcher archive: unexpected error code %d", data->_zipDownloadResult);
					break;
				}
			}

			return false;
		} else {
			_gui->Log("Checking release integrity...\n");

			data->TrimHash();
			if (!_updater.CheckHashConsistency(data->_zipFileName.c_str(), data->_hash.c_str())) {
				_gui->LogError("Hash mismatch: download was corrupted\n");
				return false;
			} else {
				_gui->Log("OK\n");
			}

			return true;
		}

		_gui->LogError("Sylmir probably forgot a return path in this function, please report this as an error");
		return false;
	}

	bool LauncherUpdateManager::DoUpdate(const char* from, const char* to, const char* url) {
		_gui->Log("Updating the REPENTOGON launcher\n");
		_gui->Log("Update scheduled from version %s to version %s\n", from, to);

		new (&_updater) LauncherUpdater(from, to, url);

		if (!DoPreUpdateChecks()) {
			return false;
		}

		LauncherUpdateData updateData;
		bool downloadResult = DownloadUpdate(&updateData);
		
		if (!PostDownloadChecks(downloadResult, &updateData)) {
			_gui->LogError("Error during release download\n");
			return false;
		}

		if (!ExtractArchive(&updateData)) {
			_gui->LogError("Error while extracting archive\n");
			return false;
		}

		_gui->Log("Successfully downloaded and extracted new release\n");
		return true;
	}

	bool LauncherUpdateManager::ExtractArchive(LauncherUpdateData* data) {
		const char* filename = data->_zipFileName.c_str();
		ExtractArchiveResult result = _updater.ExtractArchive(filename);
		switch (result.errCode) {
		case EXTRACT_ARCHIVE_OK:
			_gui->Log("Sucessfully extracted new version\n");
			return true;

		case EXTRACT_ARCHIVE_ERR_NO_EXE:
			_gui->LogError("Downloaded archive contains no exe\n");
			break;

		case EXTRACT_ARCHIVE_ERR_OTHER:
			_gui->LogError("Unexpected error while extracting archive\n");
			break;

		case EXTRACT_ARCHIVE_ERR_CANNOT_OPEN_ZIP:
			_gui->LogError("Unable to open archive %s\n", filename);
			break;

		case EXTRACT_ARCHIVE_ERR_CANNOT_OPEN_BINARY_OUTPUT:
			_gui->LogError("Unable to open file in which to write unpacked archive\n");
			break;

		case EXTRACT_ARCHIVE_ERR_FILE_EXTRACT:
			_gui->LogError("Errors encountered while extracting files\n");
			break;

		case EXTRACT_ARCHIVE_ERR_FWRITE:
			_gui->LogError("Error while writing uncompressed files\n");
			break;

		case EXTRACT_ARCHIVE_ERR_ZIP_ERROR:
			_gui->LogError("libzip error encountered: %s\n", result.zipError.c_str());
			break;

		default:
			_gui->LogError("Unknown error %d encountered\n", result.errCode);
			break;
		}

		_gui->Log("", true, "************ Archive content read before error ************");
		for (auto const& [name, state] : result.files) {
			_gui->Log("\t", false, "%s: ", name.c_str());
			switch (state) {
			case Zip::EXTRACT_FILE_OK:
				_gui->Log("", true, "OK");
				break;

			case Zip::EXTRACT_FILE_ERR_ZIP_FREAD:
				_gui->Log("", true, "error while reading file content");
				break;

			case Zip::EXTRACT_FILE_ERR_FOPEN:
				_gui->Log("", true, "unable to open file on disk to write content");
				break;

			case Zip::EXTRACT_FILE_ERR_FWRITE:
				_gui->Log("", true, "unable to write extracted file");
				break;

			case Zip::EXTRACT_FILE_ERR_ZIP_STAT:
				_gui->Log("", true, "unable to read file size");
				break;

			default:
				_gui->Log("", true, "unknown error");
				break;
			}
		}

		return false;
	}

	bool LauncherUpdateManager::ProcessSynchronizationResult(Synchronization::SynchronizationResult result) {
		if (result == Synchronization::SYNCHRONIZATION_OK) {
			return true;
		}

		namespace s = Synchronization;
		std::ostringstream stream;

		switch (result) {
		case s::SYNCHRONIZATION_ERR_WAIT_PIPE_TIMEOUT:
			stream << "Timeout while waiting for communication channel ready";
			break;

		case s::SYNCHRONIZATION_ERR_CANNOT_OPEN_PIPE:
			stream << "Unable to open communication channel with launcher";
			break;

		case s::SYNCHRONIZATION_ERR_SEND_PING:
			stream << "Unable to send hello to launcher";
			break;

		case s::SYNCHRONIZATION_ERR_RECV_PONG:
			stream << "No hello received from launcher";
			break;

		case s::SYNCHRONIZATION_ERR_INVALID_PONG:
			stream << "Invalid hello received from launcher";
			break;

		case s::SYNCHRONIZATION_ERR_SEND_PID_REQUEST:
			stream << "Unable to send PID request to launcher";
			break;

		case s::SYNCHRONIZATION_ERR_RECV_PID_ANSWER:
			stream << "Invalid PID response from launcher";
			break;

		case s::SYNCHRONIZATION_ERR_RECV_PID:
			stream << "Invalid PID received from launcher";
			break;

		case s::SYNCHRONIZATION_ERR_OPEN_PROCESS:
			stream << "Unable to get access to launcher process";
			break;

		case s::SYNCHRONIZATION_ERR_TERMINATE_PROCESS:
			stream << "Unable to close launcher process";
			break;

		case s::SYNCHRONIZATION_ERR_WAIT_PROCESS:
			stream << "Timed out while waiting for launcher to close";
			break;

		default:
			_gui->LogError("Unknown error (%d)\n", result);
			return false;
		}

		stream << ". Check log file for details";
		_gui->LogError(stream.str().c_str());
		return false;
	}

	void LauncherUpdateManager::LogGithubDownloadAsString(const char* prefix, Github::DownloadAsStringResult result) {
		switch (result) {
		case Github::DOWNLOAD_AS_STRING_OK:
			_gui->Log("%s: successfully downloaded\n", prefix);
			break;

		case Github::DOWNLOAD_AS_STRING_BAD_CURL:
			_gui->LogError("%s: error while initiating cURL connection\n", prefix);
			break;

		case Github::DOWNLOAD_AS_STRING_BAD_REQUEST:
			_gui->LogError("%s: error while performing cURL request\n", prefix);
			break;

		case Github::DOWNLOAD_AS_STRING_INVALID_JSON:
			_gui->LogError("%s: malformed JSON in answer\n", prefix);
			break;

		case Github::DOWNLOAD_AS_STRING_NO_NAME:
			_gui->LogError("%s: JSON answer contains no \"name\" field\n", prefix);
			break;

		default:
			_gui->LogError("%s: Sylmir forgot a case somewhere\n", prefix);
			break;
		}
	}
}