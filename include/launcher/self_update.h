#pragma once

#include <cstdint>

#include <optional>
#include <string>
#include <variant>

#include "shared/github.h"

namespace Launcher {
	extern const char* SelfUpdaterExePath;

	enum SelfUpdateExtractionResult {
		SELF_UPDATE_EXTRACTION_OK,
		SELF_UPDATE_EXTRACTION_ERR_RESOURCE_NOT_FOUND,
		SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOAD_FAILED,
		SELF_UPDATE_EXTRACTION_ERR_BAD_RESOURCE_SIZE,
		SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOCK_FAILED,
		SELF_UPDATE_EXTRACTION_ERR_OPEN_TEMPORARY_FILE,
		SELF_UPDATE_EXTRACTION_ERR_WRITTEN_SIZE
	};

	/* No OK result as the process terminates if everything goes well. */
	enum SelfUpdateRunUpdaterResult {
		SELF_UPDATE_RUN_UPDATER_ERR_NO_PIPE,
		SELF_UPDATE_RUN_UPDATER_ERR_OPEN_LOCK_FILE,
		SELF_UPDATE_RUN_UPDATER_ERR_GENERATE_CLI,
		SELF_UPDATE_RUN_UPDATER_ERR_CREATE_PROCESS,
		SELF_UPDATE_RUN_UPDATER_ERR_OPEN_PROCESS,
		SELF_UPDATE_RUN_UPDATER_ERR_CONNECT_IO_PENDING,
		SELF_UPDATE_RUN_UPDATER_ERR_READFILE_ERROR,
		SELF_UPDATE_RUN_UPDATER_ERR_INVALID_PING,
		SELF_UPDATE_RUN_UPDATER_ERR_INVALID_RESUME,
		SELF_UPDATE_RUN_UPDATER_ERR_READ_OVERFLOW,
		SELF_UPDATE_RUN_UPDATER_INFO_WAIT_TIMEOUT,
		SELF_UPDATE_RUN_UPDATER_INFO_READFILE_IO_PENDING
	};

	/* No OK result as the process terminates if everything goes well. */
	enum SelfUpdateResult {
		/* Everything up-to-date. */
		SELF_UPDATE_UP_TO_DATE,
		/* Attempted a full update while previous attempt is still in progress. */
		SELF_UPDATE_STILLBORN_CHILD,
		/* General error when checking for updates. */
		SELF_UPDATE_UPDATE_CHECK_FAILED,
		/* General error when extracting the self updater. */
		SELF_UPDATE_EXTRACTION_FAILED,
		/* General error when running the self updater. */
		SELF_UPDATE_SELF_UPDATE_FAILED
	};

	struct SelfUpdateErrorCode {
		SelfUpdateResult base : 3;
		union {
			Github::DownloadAsStringResult fetchUpdatesResult : 8;
			SelfUpdateExtractionResult extractionResult : 8;
			SelfUpdateRunUpdaterResult runUpdateResult : 8;
		} detail;
	};

	class SelfUpdater {
	public:
		/* Check if an update of the launcher is available.
		 *
		 *   allowDrafts indicates if a prerelease is considered a valid update.
		 *   force causes the function to pick the first available release, 
		 * regardless of allowDrafts and regardless of whether said release is
		 * new than the current one.
		 *   version receives the name of new available version, if any.
		 *   url receives the url to download the newest available version, if any.
		 *
		 * Return true if an update is available, false otherwise. false is 
		 * also returned on error.
		 */
		bool IsSelfUpdateAvailable(bool allowDrafts, bool force, 
			std::string& version, std::string& url, Github::DownloadAsStringResult* fetchReleaseResult);

		/* Perform a self-update.
		 * 
		 * The function will fetch the release data from GitHub, iterate through
		 * the releases and pick the first valid one depending on whether it is
		 * a prerelease or not, and whether it is newer than the currently 
		 * installed release. This can be controlled through the two 
		 * parameters allowDrafts and force.
		 * 
		 *   allowDrafts indicates if a prerelease is considered a valid update.
		 *   force causes the function to pick the first available release, 
		 * regardless of allowDrafts and regardless of wheter said release
		 * if newer than the current one.
		 * 
		 * The function returns an extended error code that can designate 
		 * multiple points of failure in the entire process, from fetching
		 * the release data to starting the updater.
		 */
		SelfUpdateErrorCode DoSelfUpdate(bool allowDrafts, bool force);

		/* Perform a self-update.
		 * 
		 * The function does not perform any checks: it extracts the updater and
		 * launches it to update with the release available at the specified url.
		 * 
		 * The function returns an extended error code similar to the other 
		 * version of DoSelfUpdate.
		 */
		SelfUpdateErrorCode DoSelfUpdate(std::string const& version, std::string const& url);

		/* Resume a self update that timed out while waiting for the updater. */
		SelfUpdateErrorCode ResumeSelfUpdate();

		void AbortSelfUpdate(bool reset);

	private:
		rapidjson::Document _releasesInfo;
		bool _hasRelease = false;
	};
}