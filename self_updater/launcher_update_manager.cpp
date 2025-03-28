#include <WinSock2.h>

#include <chrono>
#include <future>

#include "shared/externals.h"
#include "shared/github.h"
#include "self_updater/launcher_update_manager.h"
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

	bool LauncherUpdateManager::DownloadAndExtractUpdate(const char* url) {
		_gui->Log("Downloading update for the the REPENTOGON launcher from %s\n", url);

		new (&_updater) LauncherUpdater(url);

		if (!DoPreUpdateChecks()) {
			return false;
		}

		LauncherUpdateData updateData;
		bool downloadResult = DownloadUpdate(&updateData);

		_gui->Log("Update scheduled from version %s to version %s\n", Launcher::version, _updater.GetReleaseInfo()["name"].GetString());
		
		if (!PostDownloadChecks(downloadResult, &updateData)) {
			_gui->LogError("Error while downloading release\n");
			return false;
		}

		if (!ExtractArchive(&updateData)) {
			_gui->LogError("Error while extracting the releases's archive\n");
			return false;
		}

		_gui->Log("Successfully downloaded and extracted the new release\n");
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

	void LauncherUpdateManager::LogGithubDownloadAsString(const char* prefix, Github::DownloadAsStringResult result) {
		_gui->Log("%s: %s\n", prefix, Github::DownloadAsStringResultToLogString(result));
	}

	LauncherUpdateManager::SelfUpdateCheckResult LauncherUpdateManager::CheckSelfUpdateAvailability(bool allowPreReleases,
		std::string& version, std::string& url) {
		_gui->LogNoNL("Checking for availability of launcher updates... ");
		Github::DownloadAsStringResult downloadReleasesResult;
		if (_updateChecker.IsSelfUpdateAvailable(allowPreReleases, false, version, url, &downloadReleasesResult)) {
			_gui->Log("OK\n");
			_gui->Log("", true, "New version of the launcher available: %s (can be downloaded from %s)\n", version.c_str(), url.c_str());
			return SELF_UPDATE_CHECK_UPDATE_AVAILABLE;
		} else {
			if (downloadReleasesResult != Github::DOWNLOAD_AS_STRING_OK) {
				_gui->Log("KO");
				_gui->LogError("Error encountered while checking for availability of launcher update");
				LogGithubDownloadAsString("Launcher Update Check", downloadReleasesResult);
				return SELF_UPDATE_CHECK_ERR_GENERIC;
			} else {
				_gui->Log("Up-to-date");
				return SELF_UPDATE_CHECK_UP_TO_DATE;
			}
		}

		_gui->LogWarn("You should not be seeing this, Sylmir probably forgot a return path somewhere, report it as a bug");
		return SELF_UPDATE_CHECK_UP_TO_DATE;
	}
}