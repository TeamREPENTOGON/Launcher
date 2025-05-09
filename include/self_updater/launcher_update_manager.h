#pragma once

#include "self_updater/launcher_updater.h"
#include "shared/launcher_update_checker.h"
#include "shared/loggable_gui.h"

namespace Updater {
	class LauncherUpdateManager {
	public:
		enum SelfUpdateCheckResult {
			SELF_UPDATE_CHECK_UP_TO_DATE,
			SELF_UPDATE_CHECK_UPDATE_AVAILABLE,
			SELF_UPDATE_CHECK_ERR_GENERIC
		};
	public:
		static constexpr const size_t BUFF_SIZE = 4096;

		LauncherUpdateManager(ILoggableGUI* gui);

		SelfUpdateCheckResult CheckSelfUpdateAvailability(bool allowPreRelease, std::string& version, std::string& url);
		bool DownloadAndExtractUpdate(const char* url);

	private:
		ILoggableGUI* _gui;
		bool _logError = false;
		static char _printBuffer[BUFF_SIZE];
		LauncherUpdater _updater;
		Shared::LauncherUpdateChecker _updateChecker;

		void LogGithubDownloadAsString(const char* prefix, curl::DownloadStringResult code);

		bool DoPreUpdateChecks();
		bool DownloadUpdate(LauncherUpdateData* data);
		bool PostDownloadChecks(bool downloadOk, LauncherUpdateData* data);
		bool ExtractArchive(LauncherUpdateData* data);
	};
}