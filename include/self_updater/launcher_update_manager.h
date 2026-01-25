#pragma once

#include "self_updater/launcher_updater.h"
#include "shared/launcher_update_checker.h"

namespace Updater {
	class LauncherUpdateManager {
	public:
		enum SelfUpdateCheckResult {
			SELF_UPDATE_CHECK_UP_TO_DATE,
			SELF_UPDATE_CHECK_UPDATE_AVAILABLE,
			SELF_UPDATE_CHECK_ERR_GENERIC,
			SELF_UPDATE_CHECK_STEAM_METHOD_FAILED,
		};
	public:
		static constexpr const size_t BUFF_SIZE = 4096;

		SelfUpdateCheckResult CheckSelfUpdateAvailability(bool allowPreRelease, std::string& version, std::string& url);
		bool DownloadUpdate(const std::string& url, std::string& zipFilename);

	private:
		LauncherUpdater _updater;
		Shared::LauncherUpdateChecker _updateChecker;

		void LogGithubDownloadAsString(const char* prefix, curl::DownloadStringResult code);

		bool DoPreUpdateChecks();
		bool DownloadUpdate(LauncherUpdateData* data);
		bool PostDownloadChecks(bool downloadOk, LauncherUpdateData* data);
	};
}