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
		std::shared_ptr<curl::AsynchronousDownloadFileDescriptor> _zipDownloadDesc;
		std::optional<curl::DownloadFileDescriptor> _zipDescriptor;
		std::string _zipUrl;
		std::string _zipFilename;

		std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> _hashDownloadDesc;
		std::optional<curl::DownloadStringDescriptor> _hashDescriptor;
		std::string _hashUrl;
	};

	class LauncherUpdater {
	public:
		// Nop.
		LauncherUpdater() { }
		LauncherUpdater(const char* url);

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

		/* Asynchronously download the update.
		 *
		 * Descriptors inside @a data are set accordingly.
		 */
		void DownloadUpdate(LauncherUpdateData* data);

		ExtractArchiveResult ExtractArchive(const char* name);

		bool CheckHashConsistency(const char* zipFile, const char* hash);

		curl::DownloadStringResult GetReleaseDownloadResult() const;
		rapidjson::Document const& GetReleaseInfo() const;
		ReleaseInfoState GetReleaseInfoState() const;

	private:
		std::string _lockFileName;
		std::string _url;
		std::string _hashUrl, _zipUrl;

		rapidjson::Document _releaseInfo;
		curl::DownloadStringResult _releaseDownloadResult = curl::DOWNLOAD_STRING_OK;
		ReleaseInfoState _releaseInfoState = ReleaseInfoState::RELEASE_INFO_STATE_OK;
		UpdateState _state = UPDATE_STATE_NONE;

		void GenerateArchiveFilename(LauncherUpdateData* data);
		bool ValidateReleaseInfo();
		void PopulateUpdateData(LauncherUpdateData* data);
	};
}