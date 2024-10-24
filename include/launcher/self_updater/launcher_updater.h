#pragma once

#include <cstdio>

#include <string>
#include <vector>

#include "shared/github.h"
#include "shared/monitor.h"
#include "shared/sha256.h"
#include "shared/zip.h"

#include "zip.h"

namespace Updater {
	enum UpdateState {
		/* Backend is not yet initialized. */
		UPDATE_STATE_NONE = -1,
		/* Launcher has created the updater, and is ready to be updated. 
		 * Nothing stored inside.
		 */
		UPDATE_STATE_INIT,
		/* RepentogonUpdater is ready to perform the download. 
		 * CLI params stored within.
		 */
		UPDATE_STATE_READY,
		/* RepentogonUpdater has downloaded the update. 
		 * Name of the zip is contained within. 
		 */
		UPDATE_STATE_DOWNLOADED,
		/* RepentogonUpdater received invalid URL, update abandonned. 
		 * URL contained within. 
		 */
		UPDATE_STATE_BAD_URL,
		/* RepentogonUpdater received lock file in an invalid state.
		 * Previous state contained within. 
		 */
		UPDATE_STATE_INVALID_STATE
	};

	enum UpdateStartupCheckResult {
		UPDATE_STARTUP_CHECK_OK,
		UPDATE_STARTUP_CHECK_CANNOT_FETCH_RELEASE,
		UPDATE_STARTUP_CHECK_INVALID_RELEASE_INFO
	};

	enum ReleaseInfoState {
		RELEASE_INFO_STATE_OK,
		RELEASE_INFO_STATE_NO_HASH,
		RELEASE_INFO_STATE_NO_ZIP,
		RELEASE_INFO_STATE_NO_ASSETS
	};

	enum ExtractArchiveResultCode {
		EXTRACT_ARCHIVE_OK,
		EXTRACT_ARCHIVE_ERR_FWRITE,
		EXTRACT_ARCHIVE_ERR_CANNOT_OPEN_ZIP,
		EXTRACT_ARCHIVE_ERR_CANNOT_OPEN_BINARY_OUTPUT,
		EXTRACT_ARCHIVE_ERR_ZIP_ERROR,
		EXTRACT_ARCHIVE_ERR_FILE_EXTRACT,
		EXTRACT_ARCHIVE_ERR_NO_EXE,
		EXTRACT_ARCHIVE_ERR_OTHER
	};

	struct ExtractArchiveResult {
		ExtractArchiveResultCode errCode;
		std::string zipError;
		std::vector<std::tuple<std::string, Zip::ExtractFileResult>> files;

		void SetZipError(zip_error_t* z);
	};

	static constexpr const char* LauncherFileNameTemplate = "Launcher_%s.zip";

	struct LauncherUpdateData {
		Threading::Monitor<Github::GithubDownloadNotification> _zipMonitor;
		std::string _zipUrl;
		std::string _zipFileName;
		Github::DownloadFileResult _zipDownloadResult;

		Threading::Monitor<Github::GithubDownloadNotification> _hashMonitor;
		std::string _hashUrl;
		std::string _hash;
		Github::DownloadAsStringResult _hashDownloadResult;

		inline void TrimHash() {
			Sha256::Trim(_hash);
		}
	};

	class LauncherUpdater {
	public:
		// Nop.
		LauncherUpdater() { }
		LauncherUpdater(const char* from, const char* to, const char* url);

		LauncherUpdater(LauncherUpdater const&) = delete;
		LauncherUpdater(LauncherUpdater&&) = delete;

		LauncherUpdater& operator=(LauncherUpdater const&) = delete;
		LauncherUpdater& operator=(LauncherUpdater&&) = delete;

		/* Check that the backend is properly configured.
		 * 
		 * This ensures that the lock file exists and that it is in the appropriate
		 * state. The function also checks that it can fetch the release data.
		 * 
		 * This functions sets the current state to the one read from the file, 
		 * if any.
		 */
		UpdateStartupCheckResult DoStartupCheck();

		UpdateState GetUpdateState() const;

		/* Download the update. 
		 *
		 * The content of data is set to the appropriate information depending
		 * on what happened during the download.
		 * 
		 * The function returns true if everything went well, false otherwise.
		 */
		bool DownloadUpdate(LauncherUpdateData* data);

		ExtractArchiveResult ExtractArchive(const char* name);

		bool CheckHashConsistency(const char* zipFile, const char* hash);

		Github::DownloadAsStringResult GetReleaseDownloadResult() const;
		rapidjson::Document const& GetReleaseInfo() const;
		ReleaseInfoState GetReleaseInfoState() const;

	private:
		std::string _lockFileName;
		std::string _fromVersion;
		std::string _toVersion;
		std::string _url;
		std::string _hashUrl, _zipUrl;

		rapidjson::Document _releaseInfo;
		Github::DownloadAsStringResult _releaseDownloadResult = Github::DOWNLOAD_AS_STRING_OK;
		ReleaseInfoState _releaseInfoState = ReleaseInfoState::RELEASE_INFO_STATE_OK;
		UpdateState _state = UPDATE_STATE_NONE;

		void GenerateArchiveFilename(LauncherUpdateData* data);
		bool ValidateReleaseInfo();
		void PopulateUpdateData(LauncherUpdateData* data);
	};
}