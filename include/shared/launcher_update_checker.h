#pragma once

#include <cstdint>

#include <optional>
#include <string>
#include <variant>

#include "shared/github.h"

namespace Shared {
	/* No OK result as the process terminates if everything goes well. */
	enum SelfUpdateResult {
		/* Everything up-to-date. */
		SELF_UPDATE_UP_TO_DATE,
		/* General error when checking for updates. */
		SELF_UPDATE_UPDATE_CHECK_FAILED,
		/* Forced update has a valid candidate. */
		SELF_UPDATE_CANDIDATE
	};

	struct CandidateVersion {
		std::string version;
		std::string url;
	};

	struct SelfUpdateErrorCode {
		SelfUpdateResult base;
		std::variant<curl::DownloadStringResult,
			CandidateVersion> detail;
	};

	class LauncherUpdateChecker {
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
			std::string& version, std::string& url, curl::DownloadStringResult* fetchReleaseResult);

		/* Select the target release for a self update.
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
		 * multiple points of failure in the entire process.
		 */
		SelfUpdateErrorCode SelectReleaseTarget(bool allowDrafts, bool force);

	private:
		rapidjson::Document _releasesInfo;
		bool _hasRelease = false;
	};
}