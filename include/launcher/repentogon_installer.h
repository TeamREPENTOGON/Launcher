#pragma once

#include <atomic>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <variant>

#include "launcher/installation.h"
#include "shared/loggable_gui.h"
#include "shared/monitor.h"
#include "shared/scoped_file.h"
#include "shared/github.h"

namespace Launcher {
	/* Phases in the update of Repentogon. */
	enum RepentogonInstallationPhase {
		/* Checking the assets available. */
		REPENTOGON_INSTALLATION_PHASE_CHECK_ASSETS,
		/* Downloading. */
		REPENTOGON_INSTALLATION_PHASE_DOWNLOAD,
		/* Check that the hash of the downloaded file is okay. */
		REPENTOGON_INSTALLATION_PHASE_CHECK_HASH,
		/* Unpack the downloaded file. */
		REPENTOGON_INSTALLATION_PHASE_UNPACK,
		/* Done. */
		REPENTOGON_INSTALLATION_PHASE_DONE
	};

	enum RepentogonInstallationResult {
		/* Update was successful. */
		REPENTOGON_INSTALLATION_RESULT_OK,
		/* Update failed early: unable to remove dsound.dll. */
		REPENTOGON_INSTALLATION_RESULT_DSOUND_REMOVAL_FAILED,
		/* Update failed: missing asset. */
		REPENTOGON_INSTALLATION_RESULT_MISSING_ASSET,
		/* Update filed: curl error during download. */
		REPENTOGON_INSTALLATION_RESULT_DOWNLOAD_ERROR,
		/* Update failed: hash of REPENTOGON.zip is invalid. */
		REPENTOGON_INSTALLATION_RESULT_BAD_HASH,
		/* Update failed: error during extraction. */
		REPENTOGON_INSTALLATION_RESULT_EXTRACT_FAILED,
		/* Update failed: invalid output folder. */
		REPENTOGON_INSTALLATION_RESULT_INVALID_OUTPUT_DIR,
		/* Update failed: unable to create Repentogon folder. */
		REPENTOGON_INSTALLATION_RESULT_NO_REPENTOGON,
		/* Update failed: unable to create marker. */
		REPENTOGON_INSTALLATION_RESULT_NO_MARKER,
		/* Update failed: failed to copy Isaac files. */
		REPENTOGON_INSTALLATION_RESULT_NO_ISAAC_COPY,
		/* Update failed: failed to create steam_appid.txt. */
		REPENTOGON_INSTALLATION_RESULT_NO_STEAM_APPID,
		/* Update failed: failed to patch Isaac files. */
		REPENTOGON_INSTALLATION_RESULT_NO_ISAAC_PATCH,
		/* Empty state. */
		REPENTOGON_INSTALLATION_RESULT_NONE
	};

	/* Structure holding the state of the current Repentogon update.
	 * This is used for logging and debugging purposes.
	 */
	struct RepentogonInstallationState {
		RepentogonInstallationState();

		RepentogonInstallationResult result = REPENTOGON_INSTALLATION_RESULT_NONE;
		RepentogonInstallationPhase phase = REPENTOGON_INSTALLATION_PHASE_CHECK_ASSETS;
		bool zipOk = false; /* zip is present in assets. */
		bool hashOk = false; /* hash is present in assets. */
		std::string hashUrl; /* URL to download the hash. */
		std::string zipUrl; /* URL to download the archive. */
		ScopedFile hashFile; /* Pointer to the file in which we write the hash. */
		ScopedFile zipFile; /* Pointer to the file in which we write the zip. */
		std::string hash; /* Expected hash of the zip file. */
		std::string zipHash; /* Hash of the zip file. */
		/* All files that have been found in the archive and whether they
		 * were properly unpacked. */
		std::vector<std::tuple<std::string, bool>> unzipedFiles;

		void Clear();
	};

	struct RepentogonInstallationNotification {
		RepentogonInstallationNotification() { }

		template<typename T>
		RepentogonInstallationNotification(T&& data) {
			_data = std::forward<T>(data);
		}

		struct GeneralNotification {
			std::string _text;
			bool _isError;
		};

		struct FileRemoval {
			std::string _name;
			bool _ok;
		};

		struct FileDownload {
			std::string _filename;
			uint32_t _size;
			uint32_t _id;
		};

		std::variant<GeneralNotification, FileRemoval, FileDownload> _data;
	};

	template<typename T>
	using RepentogonMonitor = Threading::MonitoredFuture<T, RepentogonInstallationNotification>;

	template<typename T>
	using GithubMonitor = Threading::MonitoredFuture<T, curl::DownloadNotification>;

	class RepentogonInstaller {
	public:
		RepentogonInstaller(Installation* installation);

		enum DownloadInstallRepentogonResult {
			DOWNLOAD_INSTALL_REPENTOGON_OK,
			DOWNLOAD_INSTALL_REPENTOGON_UTD,
			DOWNLOAD_INSTALL_REPENTOGON_ERR,
			DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES
		};

		enum CheckRepentogonUpdatesResult {
			CHECK_REPENTOGON_UPDATES_UTD,
			CHECK_REPENTOGON_UPDATES_NEW,
			CHECK_REPENTOGON_UPDATES_ERR
		};

		/* Install a specific release of Repentogon. */
		RepentogonMonitor<bool> InstallRepentogon(rapidjson::Document& document);

		/* Download and install the latest release of Repentogon.
		 *
		 * Parameter force indicates whether the function should download and
		 * install the latest release even if the installation is up-to-date.
		 *
		 * This function initiates a multi-threading context. The future can be
		 * used to synchronize with the completion of the underlying thread.
		 */
		RepentogonMonitor<DownloadInstallRepentogonResult> InstallLatestRepentogon(
			bool force, bool allowPreReleases);

		/* Check if an update is available.
		 *
		 * Parameter force will cause the function to consider that an update is
		 * available even if the installation is up-to-date.
		 */
		RepentogonMonitor<CheckRepentogonUpdatesResult> CheckRepentogonUpdates(
			rapidjson::Document& document, bool allowPreReleases, bool force);

		inline RepentogonInstallationState const& GetState() const {
			return _installationState;
		}

		inline void CancelInstallation() {
			_cancelRequested.store(true, std::memory_order_release);
		}

	private:
		inline bool CancelRequested() const {
			return _cancelRequested.load(std::memory_order_acquire);
		}

		bool HandleLegacyInstallation();

		void PushNotification(std::string string, bool error);
		void PushFileRemovalNotification(std::string name, bool success);
		void PushNotification(bool isError, const char* fmt, ...) _Printf_format_string_;
		void PushFileDownloadNotification(std::string name, uint32_t size, uint32_t id);
		bool CreateRepentogonFolder(const char* name);
		bool CreateRepentogonMarker(const char* marker);

		std::string GetDsoundDLLPath() const;
		bool NeedRemoveDsoundDLL() const;

		DownloadInstallRepentogonResult InstallLatestRepentogonThread(bool force, bool allowPreReleases);
		CheckRepentogonUpdatesResult CheckRepentogonUpdatesThread(rapidjson::Document& document, bool allowPreReleases, bool force);

		/* Check that the latest release of Repentogon contains a hash and
		 * the archive itself.
		 *
		 * Return true if the release is well-formed, false otherwise.
		 */
		bool CheckRepentogonAssets(rapidjson::Document const& release);

		/* Download the latest release of Repentogon: hash and archive.
		 *
		 * Return true if the download completes without issue, false otherwise.
		 */
		bool DownloadRepentogon();

		/* Check that the hash of the downloaded archive matches the hash of
		 * the release.
		 *
		 * Return true if the hash matches, false otherwise.
		 */
		bool CheckRepentogonIntegrity();

		/* Extract the content of the Repentogon archive.
		 *
		 * The content of the archive are extracted in outputDir.
		 *
		 * Return true if the extraction is successful, false otherwise.
		 */
		bool ExtractRepentogon(const char* outputDir);

		/* Update the installation of Repentogon.
		 *
		 * The update is performed by downloading the latest release from
		 * GitHub, checking that the hash of REPENTOGON.zip matches the content
		 * of hash.txt and then unpacking REPENTOGON.zip into the current
		 * folder.
		 *
		 * The base parameter should be the result of querying the latest
		 * release, as provided by a successful call to CheckRepentogonUpdates().
		 *
		 * Return true if the installation is successful, false otherwise.
		 */
		bool InstallRepentogonThread(rapidjson::Document& document);

		/* Check if a Repentogon update is available.
		 *
		 * An update is available if the hash of any of the DLLs is different
		 * from its hash in the latest available release.
		 *
		 * The response parameter is updated to store the possible answer to
		 * the request.
		 */
		Github::VersionCheckResult CheckRepentogonUpdatesThread_Updater(rapidjson::Document& response,
			bool allowPreReleases);

		/* Fetch the JSON of the latest Repentogon release. */
		Github::ReleaseInfoResult FetchRepentogonUpdates(rapidjson::Document& result,
			bool allowPreReleases);

		template<typename T>
		std::optional<T> GithubToRepInstall(curl::DownloadMonitor* monitor,
			std::future<T>& future) {
			while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
				if (CancelRequested()) {
					return std::nullopt;
				}

				while (std::optional<curl::DownloadNotification> message = monitor->Get()) {
					if (CancelRequested()) {
						return std::nullopt;
					}

					switch (message->type) {
					case curl::DOWNLOAD_INIT_CURL:
						PushNotification(false, "[RepentogonUpdater] Initializing cURL connection to %s", std::get<std::string>(message->data).c_str());
						break;

					case curl::DOWNLOAD_INIT_CURL_DONE:
						PushNotification(false, "[RepentogonUpdater] Initialized cURL connection to %s", std::get<std::string>(message->data).c_str());
						break;

					case curl::DOWNLOAD_CURL_PERFORM:
						PushNotification(false, "[RepentogonUpdater] Performing cURL request to %s", std::get<std::string>(message->data).c_str());
						break;

					case curl::DOWNLOAD_CURL_PERFORM_DONE:
						PushNotification(false, "[RepentogonUpdater] Performed cURL request to %s", std::get<std::string>(message->data).c_str());
						break;

					case curl::DOWNLOAD_DATA_RECEIVED:
						PushFileDownloadNotification(std::move(message->name), std::get<size_t>(message->data), message->id);
						break;

					case curl::DOWNLOAD_DONE:
						PushNotification(false, "[RepentogonUpdater] Successfully downloaded content from %s", std::get<std::string>(message->data).c_str());
						break;

					default:
						PushNotification(true, "[RepentogonUpdater] Unexpected asynchronous notification (id = %d)", message->type);
						break;
					}
				}
			}

			return std::make_optional(future.get());
		}

		Installation* _installation;

		/* Prior to update, for logging purpose. */
		std::string _zhlVersion;
		std::string _repentogonVersion;
		std::string _loaderVersion;
		std::atomic<bool> _cancelRequested = false;

		Threading::Monitor<RepentogonInstallationNotification> _monitor;

		RepentogonInstallationState _installationState;
	};

}