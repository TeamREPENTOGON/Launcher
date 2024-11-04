#include <WinSock2.h>

#include <chrono>
#include <future>

#include "wx/cmdline.h"

#include "shared/externals.h"
#include "shared/github.h"
#include "shared/logger.h"
#include "unpacker/synchronization.h"
#include "launcher/self_updater/launcher_update_manager.h"
#include "launcher/self_updater/finalizer.h"
#include "launcher/version.h"

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
						_gui->Log("", true, "[%s] Initializing cURL connection to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_INIT_CURL_DONE:
						_gui->Log("", true, "[%s] Initialized cURL connection to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_CURL_PERFORM:
						_gui->Log("", true, "[%s] Performing cURL request to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_CURL_PERFORM_DONE:
						_gui->Log("", true, "[%s] Performed cURL request to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_DATA_RECEIVED:
					{
						totalDownloadSize += std::get<size_t>(message->data);

						std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
						if (std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastReceived).count() > 100000000) {
							_gui->Log("", true, "[%s] Downloaded %lu bytes\n", s->name.c_str(), totalDownloadSize);
							lastReceived = now;
						}
						break;
					}

					case Github::GH_NOTIFICATION_PARSE_RESPONSE:
						_gui->Log("", true, "[%s] Parsing result of cURL request from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_PARSE_RESPONSE_DONE:
						_gui->Log("", true, "[%s] Parsed result of cURL request from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_DONE:
						s->done = true;
						_gui->Log("", true, "[%s] Successfully downloaded content from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
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
			_gui->LogError("Error while downloading release\n");
			return false;
		}

		if (!ExtractArchive(&updateData)) {
			_gui->LogError("Error while extracting the releases's archive\n");
			return false;
		}

		_gui->Log("Successfully downloaded and extracted the new release\n");
		FinalizeUpdate();
		_gui->LogError("Error while finalizing the update of the launcher\n");
		return false;
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

	LauncherUpdateManager::SelfUpdateCheckResult LauncherUpdateManager::CheckSelfUpdateAvailability(bool allowPreReleases,
		std::string& version, std::string& url) {
		_gui->LogNoNL("Checking for availability of launcher updates... ");
		rapidjson::Document launcherResponse;
		Github::DownloadAsStringResult downloadReleasesResult;
		if (_selfUpdater.IsSelfUpdateAvailable(allowPreReleases, false, version, url, &downloadReleasesResult)) {
			_gui->Log("OK\n");
			_gui->Log("", true, "New version of the launcher available: %s (can be downloaded from %s)\n", version.c_str(), url.c_str());
			return SELF_UPDATE_CHECK_UPDATE_AVAILABLE;
		} else {
			if (downloadReleasesResult != Github::DOWNLOAD_AS_STRING_OK) {
				_gui->Log("KO");
				_gui->LogError("Error encountered while checking for availability of launcher update");
				switch (downloadReleasesResult) {
				case Github::DOWNLOAD_AS_STRING_BAD_CURL:
					_gui->LogError("Unable to initialize cURL connection");
					break;

				case Github::DOWNLOAD_AS_STRING_BAD_REQUEST:
					_gui->LogError("Unable to perform cURL request");
					break;

				case Github::DOWNLOAD_AS_STRING_INVALID_JSON:
					_gui->LogError("Invalid response");
					break;

				case Github::DOWNLOAD_AS_STRING_NO_NAME:
					_gui->LogError("Release has no \"name\" field (although you should not be seeing this error, report it as a bug");
					break;

				default:
					_gui->LogError("Unexpected error %d: report it as a bug", downloadReleasesResult);
					break;
				}

				return SELF_UPDATE_CHECK_ERR_GENERIC;
			} else {
				_gui->Log("Up-to-date");
				return SELF_UPDATE_CHECK_UP_TO_DATE;
			}
		}

		_gui->LogWarn("You should not be seeing this, Sylmir probably forgot a return path somewhere, report it as a bug");
		return SELF_UPDATE_CHECK_UP_TO_DATE;
	}

	void LauncherUpdateManager::FinalizeUpdate() {
		Updater::Finalizer finalizer;
		Updater::FinalizationResult result = finalizer.Finalize();

		if (!std::holds_alternative<Updater::FinalizationCommunicationResult>(result)) {
			HandleFinalizationResult(result);
			return;
		}

		Updater::FinalizationCommunicationResult commResult = std::get<Updater::FinalizationCommunicationResult>(result);
		if (commResult != Updater::FINALIZATION_COMM_INTERNAL_OK &&
			commResult != Updater::FINALIZATION_COMM_INFO_TIMEOUT) {
			HandleFinalizationResult(result);
			return;
		}

		std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
		uint32_t diff = 0;
		do {
			commResult = finalizer.ResumeFinalize();
			std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
			diff = std::chrono::duration_cast<std::chrono::seconds>(now - begin).count();
			std::this_thread::sleep_for(std::chrono::milliseconds(200));

			if (diff < 3) {
				_gui->Log("Waiting for unpacker...\n");
			} else if (diff < 7) {
				_gui->LogWarn("Waiting for unpacker (possibly hanging)...\n");
			} else {
				_gui->LogError("Waiting for unpacker (probably hanging, considering aborting update)...\n");
			}
		} while ((commResult == Updater::FINALIZATION_COMM_INTERNAL_OK || commResult == Updater::FINALIZATION_COMM_INFO_TIMEOUT) && 
			diff < 10);

		if (diff >= 10) {
			_gui->LogError("Fatal timeout while waiting for the unpacker, update aborted\n");
		} else {
			HandleFinalizationResult(commResult);
		}
	}

	void LauncherUpdateManager::ForceSelfUpdate(bool allowPreReleases) {
		_gui->Log("Performing self-update (forcibly triggered)");
		Launcher::SelfUpdateErrorCode result = _selfUpdater.SelectReleaseTarget(allowPreReleases, true);
		if (result.base != Launcher::SELF_UPDATE_CANDIDATE) {
			_gui->LogError("Error %d while selecting target release\n", result.base);
			return;
		}

		Launcher::CandidateVersion const& candidate = std::get<Launcher::CandidateVersion>(result.detail);
		if (!DoUpdate(Launcher::version, candidate.version.c_str(), candidate.url.c_str())) {
			return;
		}

		FinalizeUpdate();
	}

	void LauncherUpdateManager::HandleFinalizationResult(Updater::FinalizationResult const& finalizationResult) {
		struct ResultVisitor {
			ResultVisitor(std::ostringstream* info, std::ostringstream* err, std::ostringstream* warn,
				bool* isInfo, bool* isWarn) : info(info), err(err), warn(warn),
				isInfo(isInfo), isWarn(isWarn) {

			}

			std::ostringstream* info, * err, * warn;
			bool* isWarn, * isInfo;

			void operator()(Updater::FinalizationExtractionResult code) {
				*err << "Error while extracting the unpacker: ";
				switch (code) {
				case Updater::FINALIZATION_EXTRACTION_ERR_RESOURCE_NOT_FOUND:
					*err << "unable to locate unpacker inside the launcher";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_RESOURCE_LOAD_FAILED:
					*err << "unable to load unpacker from the launcher";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_RESOURCE_LOCK_FAILED:
					*err << "unable to acquire resource lock on unpacker";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_BAD_RESOURCE_SIZE:
					*err << "embedded unpacker has the wrong size";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_OPEN_TEMPORARY_FILE:
					*err << "unable to open temporary file to extract unpacker";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_WRITTEN_SIZE:
					*err << "unable to write self-updater on the disk";
					break;
				}
			}

			void operator()(Updater::FinalizationStartUnpackerResult code) {
				*err << "Error while launching unpacker: ";
				switch (code) {
				case Updater::FINALIZATION_START_UNPACKER_ERR_NO_PIPE:
					*err << "unable to create pipe to communicate with unpacker";
					break;

				case Updater::FINALIZATION_START_UNPACKER_ERR_CREATE_PROCESS:
					*err << "error while creating unpacker process";
					break;

				case Updater::FINALIZATION_START_UNPACKER_ERR_OPEN_PROCESS:
					*err << "error while opening unpacker process handle";
					break;

				default:
					*err << "Unknown error " << code << " while launching unpacker";
					break;
				}
			}

			void operator()(Updater::FinalizationCommunicationResult code) {
				switch (code) {
				case Updater::FINALIZATION_COMM_ERR_READFILE_ERROR:
					*err << "unknown error while checking for availability of self-updater";
					break;

				case Updater::FINALIZATION_COMM_ERR_INVALID_PING:
					*err << "invalid message sent by self-updater";
					break;

				case Updater::FINALIZATION_COMM_ERR_INVALID_RESUME:
					*err << "attempted to resume an update that was not started";
					break;

				case Updater::FINALIZATION_COMM_ERR_STILL_ALIVE:
					*err << "launcher was not properly terminated by updater";
					break;

				case Updater::FINALIZATION_COMM_INFO_TIMEOUT:
					*isWarn = true;
					*warn << "Timed out while waiting for availability of the self-updater";
					break;

				case Updater::FINALIZATION_COMM_INTERNAL_OK:
					*isInfo = true;
					*info << "Synchronizing the launcher and the updater...";
					break;
				}
			}
		};

		std::ostringstream info, err, warn;
		bool isInfo = false, isWarn = false;

		std::visit(ResultVisitor(&info, &err, &warn, &isInfo, &isWarn), finalizationResult);

		if (isInfo) {
			_gui->Log(info.str().c_str());
		} else if (isWarn) {
			_gui->LogWarn(warn.str().c_str());
		} else {
			_gui->LogError(err.str().c_str());
		}
	}
}