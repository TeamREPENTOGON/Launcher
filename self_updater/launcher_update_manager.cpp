#include <WinSock2.h>

#include <chrono>
#include <future>

#include "shared/externals.h"
#include "shared/github.h"
#include "shared/logger.h"
#include "self_updater/launcher_update_manager.h"
#include "launcher/version.h"

namespace Externals {
	void Init();
}

namespace Updater {
	bool LauncherUpdateManager::DoPreUpdateChecks() {
		UpdateStartupCheckResult startupCheck = _updater.DoStartupCheck();
		switch (startupCheck) {
		case UPDATE_STARTUP_CHECK_CANNOT_FETCH_RELEASE:
			Logger::Error("Unable to download release information from GitHub\n");
			LogGithubDownloadAsString("Download release information", _updater.GetReleaseDownloadResult());
			return false;

		case UPDATE_STARTUP_CHECK_INVALID_RELEASE_INFO:
			Logger::Error("Release information is invalid\n");
			switch (_updater.GetReleaseInfoState()) {
			case RELEASE_INFO_STATE_NO_ASSETS:
				Logger::Error("The release contains neither a hash file nor a launcher archive file\n");
				break;

			case RELEASE_INFO_STATE_NO_HASH:
				Logger::Error("The release contains no hash file\n");
				break;

			case RELEASE_INFO_STATE_NO_ZIP:
				Logger::Error("The release contains no launcher archive file\n");
				break;

			default:
				Logger::Error("Sylmir forgot to handle this case\n");
				break;
			}
		}

		return true;
	}

	bool LauncherUpdateManager::DownloadUpdate(LauncherUpdateData* updateData) {
		Threading::Monitor<curl::DownloadNotification> monitor;
		_updater.DownloadUpdate(updateData);

		struct {
			Threading::Monitor<curl::DownloadNotification>* monitor;
			std::string name;
			size_t totalDownloadSize;
		} monitorAndName[] = {
			{ &updateData->_hashDownloadDesc->base.monitor, "Hash file", 0 },
			{ &updateData->_zipDownloadDesc->base.monitor, "Launcher archive", 0 },
			{ NULL, "" }
		};

		std::chrono::steady_clock::time_point lastReceived = std::chrono::steady_clock::now();
		bool anyEmpty = false;
		while (updateData->_hashDownloadDesc->result.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready ||
			updateData->_zipDownloadDesc->result.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
			anyEmpty = false;
			for (auto s = monitorAndName; s->monitor; ++s) {
				bool timedout = false;
				std::optional<curl::DownloadNotification> message;
				while (message = s->monitor->Get(&timedout)) {
					switch (message->type) {
					case curl::DOWNLOAD_INIT_CURL:
						Logger::Info("[%s] Initializing cURL connection to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case curl::DOWNLOAD_INIT_CURL_DONE:
						Logger::Info("[%s] Initialized cURL connection to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case curl::DOWNLOAD_CURL_PERFORM:
						Logger::Info("[%s] Performing cURL request to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case curl::DOWNLOAD_CURL_PERFORM_DONE:
						Logger::Info("[%s] Performed cURL request to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case curl::DOWNLOAD_DATA_RECEIVED:
					{
						s->totalDownloadSize += std::get<size_t>(message->data);

						std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
						if (std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastReceived).count() > 100000000) {
							Logger::Info("[%s] Downloaded %lu bytes\n", s->name.c_str(), s->totalDownloadSize);
							lastReceived = now;
						}
						break;
					}

					case curl::DOWNLOAD_DONE:
						Logger::Info("[%s] Successfully downloaded content from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					default:
						Logger::Error("[%s] Unexpected asynchronous notification (id = %d)\n", s->name.c_str(), message->type);
						break;
					}
				}

				if (!message && !timedout) {
					anyEmpty = true;
				}
			}

			if (anyEmpty) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}

		/* async synchronizes-with get, so all non atomic accesses become visible
		 * side-effects here. Therefore, there is no need to introduce a fence in
		 * order to read the content of updateData.
		 */
		curl::DownloadStringDescriptor hashDescriptor = updateData->_hashDownloadDesc->result.get();
		curl::DownloadFileDescriptor zipDescriptor = updateData->_zipDownloadDesc->result.get();

		updateData->_hashDescriptor = hashDescriptor;
		updateData->_zipDescriptor = zipDescriptor;

		return hashDescriptor.result == curl::DOWNLOAD_STRING_OK &&
			zipDescriptor.result == curl::DOWNLOAD_FILE_OK;
	}

	bool LauncherUpdateManager::PostDownloadChecks(bool downloadOk, LauncherUpdateData* data) {
		if (!downloadOk) {
			Logger::Error("Error while downloading launcher update\n");

			if (data->_hashDescriptor->result != curl::DOWNLOAD_STRING_OK) {
				LogGithubDownloadAsString("hash download", data->_hashDescriptor->result);
			}

			if (data->_zipDescriptor->result != curl::DOWNLOAD_FILE_OK) {
				switch (data->_zipDescriptor->result) {
				case curl::DOWNLOAD_FILE_BAD_CURL:
					Logger::Error("Launcher archive: error while initializeing cURL connection to %s\n", data->_zipUrl.c_str());
					break;

				case curl::DOWNLOAD_FILE_BAD_FS:
					Logger::Error("Launcher archive: error while creating file %s on disk\n", data->_zipFilename.c_str());
					break;

				case curl::DOWNLOAD_FILE_DOWNLOAD_ERROR:
					Logger::Error("Launcher archive: error while downloading archive from %s\n", data->_zipUrl.c_str());
					break;

				default:
					Logger::Error("Launcher archive: unexpected error code %d\n", data->_zipDescriptor->result);
					break;
				}
			}

			return false;
		} else {
			Logger::Info("Checking release integrity...\n");

			Sha256::Trim(data->_hashDescriptor->string);
			if (!_updater.CheckHashConsistency(data->_zipFilename.c_str(), data->_hashDescriptor->string.c_str())) {
				Logger::Error("Hash mismatch: download was corrupted\n");
				return false;
			} else {
				Logger::Info("OK\n");
			}

			return true;
		}

		Logger::Error("Sylmir probably forgot a return path in this function, please report this as an error\n");
		return false;
	}

	bool LauncherUpdateManager::DownloadUpdate(const std::string& url, std::string& zipFilename) {
		if (url.find("github") == std::string::npos) { //if its not from github, it assumes it's a local filepath
			zipFilename = url + "/Launcher/REPENTOGONLauncher.zip";
			Logger::Info("Found update for the REPENTOGON launcher at local filepath %s\n", zipFilename.c_str());
			return true;
		}

		Logger::Info("Downloading update for the the REPENTOGON launcher from %s\n", url.c_str());

		new (&_updater) LauncherUpdater(url.c_str());

		LauncherUpdateData updateData;
		if (!DoPreUpdateChecks()) {
			return false;
		}
		bool downloadResult = DownloadUpdate(&updateData);

		Logger::Info("Update scheduled from version %s to version %s\n", Launcher::LAUNCHER_VERSION, _updater.GetReleaseInfo()["name"].GetString());

		if (!PostDownloadChecks(downloadResult, &updateData)) {
			Logger::Error("Error while downloading release\n");
			return false;
		}

		Logger::Info("Successfully downloaded and extracted the new release\n");
		zipFilename = updateData._zipFilename;
		return true;
	}

	void LauncherUpdateManager::LogGithubDownloadAsString(const char* prefix,
		curl::DownloadStringResult result) {
		Logger::Info("%s: %s\n", prefix, curl::DownloadAsStringResultToLogString(result));
	}

	LauncherUpdateManager::SelfUpdateCheckResult LauncherUpdateManager::CheckSelfUpdateAvailability(bool allowPreReleases,
		std::string& version, std::string& url) {
		Logger::Info("Checking for availability of launcher updates...\n");
		curl::DownloadStringResult downloadReleasesResult;
		Shared::SteamLauncherUpdateStatus steamUpdateStatus;
		if (_updateChecker.IsSelfUpdateAvailable(allowPreReleases, false, version, url, downloadReleasesResult, steamUpdateStatus)) {
			Logger::Info("...OK\n");
			Logger::Info("New version of the launcher available: %s (can be downloaded from %s)\n", version.c_str(), url.c_str());
			return SELF_UPDATE_CHECK_UPDATE_AVAILABLE;
		} else {
			if (downloadReleasesResult != curl::DOWNLOAD_STRING_OK && steamUpdateStatus != Shared::STEAM_LAUNCHER_UPDATE_UP_TO_DATE) {
				Logger::Error("...KO\n");
				Logger::Error("Error encountered while checking for availability of launcher update\n");
				LogGithubDownloadAsString("Launcher Update Check", downloadReleasesResult);
				if (steamUpdateStatus == Shared::STEAM_LAUNCHER_UPDATE_FAILED) {
					return SELF_UPDATE_CHECK_STEAM_METHOD_FAILED;
				}
				return SELF_UPDATE_CHECK_ERR_GENERIC;
			} else {
				Logger::Info("...Up-to-date\n");
				return SELF_UPDATE_CHECK_UP_TO_DATE;
			}
		}

		Logger::Error("You should not be seeing this, Sylmir probably forgot a return path somewhere, report it as a bug\n");
		return SELF_UPDATE_CHECK_UP_TO_DATE;
	}
}